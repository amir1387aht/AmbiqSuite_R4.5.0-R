/* SPDX-License-Identifier: GPL-2.0-only
 * YAFFS Direct adapter for DS35M1GB. See storage/INTEGRATION.txt and yaffs2/LICENSE.
 */
#include "mb8_storage.h"
#include "mb8_nand_board.h"
#include "mb8_nand_transport.h"
#include "yaffs_guts.h"
#include "yaffsfs.h"
#include "yaffs_packedtags2.h"
#include "am_mcu_apollo.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static struct ds35_nand  nand __attribute__((section(".shared.nand"), aligned(32)));
static struct yaffs_dev  dev;
static SemaphoreHandle_t mutex;
static TaskHandle_t      owner;
static bool              registered, scan_error;
static const char       *status_text = "NAND opt-in configuration not confirmed";
static unsigned          allocated, high_water;
static int               last_error;
unsigned int             yaffs_trace_mask;
extern void              yaffs_remove_device(struct yaffs_dev *);
_Static_assert(
    sizeof(struct yaffs_packed_tags2) <= DS35_TAG_CAPACITY, "YAFFS tags overlap NAND ECC");

static bool task_context(void)
{
    return !__get_IPSR() && xTaskGetSchedulerState() == taskSCHEDULER_RUNNING;
}

void yaffsfs_Lock(void)
{
    /* YAFFS's last-error state is global; enforce one worker, even though the
     * filesystem has a mutex. Do not call raw YAFFS before mb8_storage_mount. */
    configASSERT(task_context() && owner == xTaskGetCurrentTaskHandle() && mutex);
    configASSERT(xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE);
}

void yaffsfs_Unlock(void)
{
    configASSERT(xSemaphoreGive(mutex) == pdTRUE);
}

void yaffsfs_SetError(int err)
{
    last_error = err;
}

int yaffsfs_GetLastError(void)
{
    return last_error;
}

u32 yaffsfs_CurrentTime(void)
{
    /* No guessed wall-clock timestamp. Supply an RTC epoch here when known. */
    return 0;
}

void yaffsfs_OSInitialisation(void)
{
}

int yaffs_start_up(void)
{
    return 0;
}

void yaffs_bug_fn(const char *file, int line)
{
    (void)file;
    (void)line;
    configASSERT(0);
    for (;;)
    {
    }
}

/* Calls occur under the single YAFFS worker lock. A bounded allocation budget
 * prevents an unexpected filesystem scan from consuming the UI's entire heap.
 * libc malloc is the existing project allocator, not a new sbrk arena. */
union allocation_header
{
    size_t   size;
    uint64_t alignment;
};

void *yaffsfs_malloc(size_t size)
{
    if (size > MB8_NAND_HEAP_LIMIT || allocated > MB8_NAND_HEAP_LIMIT - size ||
        size > SIZE_MAX - sizeof(union allocation_header))
        return NULL;
    union allocation_header *p = malloc(sizeof(*p) + size);
    if (!p)
        return NULL;
    p->size = size;
    allocated += size;
    if (allocated > high_water)
        high_water = allocated;
    return p + 1;
}

void yaffsfs_free(void *ptr)
{
    if (ptr)
    {
        union allocation_header *p = (union allocation_header *)ptr - 1;
        allocated -= p->size;
        free(p);
    }
}

void yaffsfs_get_malloc_values(unsigned *current, unsigned *peak)
{
    if (current)
        *current = allocated;
    if (peak)
        *peak = high_water;
}

int yaffsfs_CheckMemRegion(const void *addr, size_t size, int write_request)
{
    uintptr_t a = (uintptr_t)addr;
    if (!a || size > UINTPTR_MAX - a)
        return -1;
    /* The Direct port runs trusted worker code, not user processes. Enforce
     * real MRAM/TCM/shared-SRAM bounds; writes must be to SRAM. */
    if (a >= 0x10000000u && a < 0x10160000u && size <= 0x10160000u - a)
        return 0;
    if (!write_request && a >= 0x00018000u && a < 0x00200000u && size <= 0x00200000u - a)
        return 0;
    return -1;
}

static int read_chunk(
    struct yaffs_dev *d, int page, u8 *data, int bytes, u8 *oob, int on, enum yaffs_ecc_result *ecc)
{
    (void)d;
    *ecc  = YAFFS_ECC_RESULT_UNFIXED;
    int r = ds35_read(&nand, (uint32_t)page, data, (size_t)bytes, oob, (size_t)on);
    if (r < 0)
    {
        scan_error = true;
        return YAFFS_FAIL;
    }
    *ecc = r == DS35_CORRECTED ? YAFFS_ECC_RESULT_FIXED : YAFFS_ECC_RESULT_NO_ERROR;
    return YAFFS_OK;
}

static int write_chunk(
    struct yaffs_dev *d, int page, const u8 *data, int bytes, const u8 *oob, int on)
{
    if (d->read_only)
        return YAFFS_FAIL;
    return ds35_program(&nand, (uint32_t)page, data, (size_t)bytes, oob, (size_t)on) == DS35_OK
               ? YAFFS_OK
               : YAFFS_FAIL;
}

static int erase_block(struct yaffs_dev *d, int block)
{
    return !d->read_only && ds35_erase(&nand, (uint32_t)block) == DS35_OK ? YAFFS_OK : YAFFS_FAIL;
}

static int mark_bad(struct yaffs_dev *d, int block)
{
    return !d->read_only && ds35_mark_bad(&nand, (uint32_t)block) == DS35_OK ? YAFFS_OK
                                                                             : YAFFS_FAIL;
}

static int check_bad(struct yaffs_dev *d, int block)
{
    (void)d;
    int r = ds35_check_bad(&nand, (uint32_t)block);
    if (r != DS35_OK && r != DS35_BAD)
        scan_error = true;
    return r == DS35_OK ? YAFFS_OK : YAFFS_FAIL;
}

/* Upstream marshaller query ignores the read return and can inspect uninitialized
 * tags on transport failure. Override just the callback, retain upstream intact.
 * Full mount is rejected if ANY scan I/O failed; never treat unreadable as empty.
 */
static int query_block(struct yaffs_dev *d, int block, enum yaffs_block_state *state, u32 *seq)
{
    *state = YAFFS_BLOCK_STATE_DEAD;
    *seq   = 0;
    int r  = ds35_check_bad(&nand, (uint32_t)block);
    if (r == DS35_BAD)
        return YAFFS_OK;
    if (r != DS35_OK)
    {
        scan_error = true;
        return YAFFS_FAIL;
    }
    struct yaffs_ext_tags tags = {0};
    if (d->tagger.read_chunk_tags_fn(d, block * 64, NULL, &tags) != YAFFS_OK)
    {
        scan_error = true;
        return YAFFS_FAIL;
    }
    *state = tags.chunk_used ? YAFFS_BLOCK_STATE_NEEDS_SCAN : YAFFS_BLOCK_STATE_EMPTY;
    *seq   = tags.chunk_used ? tags.seq_number : 0;
    return YAFFS_OK;
}

static int initialise(struct yaffs_dev *d)
{
    (void)d;
    return nand.ready && !nand.fault ? YAFFS_OK : YAFFS_FAIL;
}

const char *mb8_storage_status(void)
{
    return status_text;
}

int mb8_storage_mount(bool read_only)
{
    if (!task_context())
    {
        status_text = "NAND mount requires a running storage worker task";
        return -1;
    }
    if (owner && owner != xTaskGetCurrentTaskHandle())
        return -1;
    if (dev.is_mounted)
        return -1;
    if (!mb8_nand_board.pinout_confirmed || !mb8_nand_board.voltage_confirmed ||
        !mb8_nand_board.part_geometry_confirmed || !mb8_nand_board.partition_confirmed ||
        mb8_nand_board.first_block >= DS35_BLOCKS || mb8_nand_board.block_count < 16 ||
        mb8_nand_board.block_count > DS35_BLOCKS - mb8_nand_board.first_block)
    {
        status_text = "NAND pins/1.8V/part geometry/partition unconfirmed; no hardware touched";
        return -1;
    }
    if (!read_only && !mb8_nand_board.writes_authorized)
    {
        status_text = "NAND writes not explicitly authorized";
        return -1;
    }
    if (!mutex)
        mutex = xSemaphoreCreateMutex();
    if (!mutex)
    {
        status_text = "NAND lock allocation failed";
        return -1;
    }
    owner = xTaskGetCurrentTaskHandle();
    struct ds35_bus bus;
    int             r = mb8_nand_bus_open(&bus);
    if (r < 0)
    {
        status_text = "NAND dedicated bus/pin/clock configuration unconfirmed or unavailable";
        return -1;
    }
    r = ds35_probe(&nand, &bus, mb8_nand_board.first_block, mb8_nand_board.block_count);
    if (r < 0)
    {
        status_text = "NAND probe/JEDEC ID mismatch (expected E5 A1)";
        ds35_shutdown(&nand);
        mb8_nand_bus_close();
        return -1;
    }
    /* Always scan read-only FIRST, with writes locked at the NAND layer too.
     * This prevents interrupted/failed scans from causing any media writes.
     */
    if (!registered)
    {
        memset(&dev, 0, sizeof(dev));
        dev.param.name                  = "/nand";
        dev.param.is_yaffs2             = 1;
        dev.param.total_bytes_per_chunk = DS35_PAGE;
        dev.param.chunks_per_block      = 64;
        dev.param.spare_bytes_per_chunk = DS35_TAG_CAPACITY;
        dev.param.start_block           = mb8_nand_board.first_block;
        dev.param.end_block         = mb8_nand_board.first_block + mb8_nand_board.block_count - 1;
        dev.param.n_reserved_blocks = 5;
        dev.param.n_caches          = MB8_NAND_CACHE_PAGES;
        dev.param.cache_bypass_aligned = 1;
        dev.param.use_nand_ecc         = 1;
        dev.param.no_tags_ecc          = 0;
        dev.param.stored_endian        = 1;
        dev.param.max_objects          = MB8_NAND_MAX_OBJECTS;
        dev.param.refresh_period  = 1000; /* Maintain ECC scrub, not a no-integrity speed hack. */
        dev.drv                   = (struct yaffs_driver){.drv_write_chunk_fn = write_chunk,
            .drv_read_chunk_fn                              = read_chunk,
            .drv_erase_fn                                   = erase_block,
            .drv_mark_bad_fn                                = mark_bad,
            .drv_check_bad_fn                               = check_bad,
            .drv_initialise_fn                              = initialise};
        dev.tagger.query_block_fn = query_block;
        yaffs_add_device(&dev);
        registered = true;
    }
    scan_error = false;
    r          = yaffs_mount2("/nand", 1);
    if (r < 0 || scan_error || nand.fault)
    {
        if (dev.is_mounted)
            yaffs_unmount2("/nand", 1);
        status_text = "NAND read-only scan/checkpoint failed; no writes enabled";
        ds35_shutdown(&nand);
        mb8_nand_bus_close();
        return -1;
    }
    if (!read_only)
    {
        /* Remount after a successful read-only validation. YAFFS remount does
         * not format. Blank partition provisioning must be explicitly approved.
         */
        if (ds35_allow_writes(&nand) != DS35_OK || yaffs_remount("/nand", 0, 0) < 0)
        {
            nand.writable = false;
            yaffs_unmount2("/nand", 1);
            ds35_shutdown(&nand);
            mb8_nand_bus_close();
            status_text = "NAND unlock/remount failed";
            return -1;
        }
    }
    status_text = read_only ? "YAFFS2 /nand mounted read-only" : "YAFFS2 /nand mounted read-write";
    return 0;
}

int mb8_storage_unmount(void)
{
    if (!task_context() || owner != xTaskGetCurrentTaskHandle() || !dev.is_mounted)
        return -1;
    /* Normal unmount writes a checkpoint only for a writable healthy mount.
     * Close open handles first. Never force away a healthy mounted device.
     */
    if (yaffs_unmount("/nand") < 0)
        return -1;
    nand.writable       = false;
    int shutdown_result = ds35_shutdown(&nand);
    mb8_nand_bus_close();
    if (shutdown_result < 0)
    {
        status_text = "NAND QE/ECC restore failed; reset/recovery required";
        return -1;
    }
    status_text = "NAND unmounted";
    return 0;
}
