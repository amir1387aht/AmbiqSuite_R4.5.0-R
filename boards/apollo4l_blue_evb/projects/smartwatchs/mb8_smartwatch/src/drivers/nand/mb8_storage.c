/* SPDX-License-Identifier: GPL-2.0-only
 * YAFFS Direct adapter for DS35M1GB. See src/drivers/nand/INTEGRATION.txt and yaffs2/LICENSE.
 */
#include "mb8_storage.h"
#include "mb8_nand_board.h"
#include "mb8_nand_transport.h"
#include "yaffs_guts.h"
#include "yaffsfs.h"
#include "yaffs_packedtags2.h"
#include "yaffs_getblockinfo.h"
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
#if WATCH_NAND_FULL_FORMAT
static bool format_provisioned, format_io_failed;
#endif
static uint32_t partition_first(void)
{
#if WATCH_NAND_FULL_FORMAT
    if (format_provisioned)
        return 0;
#endif
    return mb8_nand_board.first_block;
}

static uint32_t partition_count(void)
{
#if WATCH_NAND_FULL_FORMAT
    if (format_provisioned)
        return DS35_BLOCKS;
#endif
    return mb8_nand_board.block_count;
}

static bool partition_authorized(void)
{
#if WATCH_NAND_FULL_FORMAT
    if (format_provisioned)
        return true;
#endif
    return mb8_nand_board.partition_confirmed && mb8_nand_board.yaffs_provisioned_confirmed;
}

static bool writes_authorized(void)
{
#if WATCH_NAND_FULL_FORMAT
    if (format_io_failed)
        return false;
    if (format_provisioned)
        return true;
#endif
    return mb8_nand_board.writes_authorized;
}

static const char *status_text = "NAND opt-in configuration not confirmed";
static unsigned    allocated, high_water;
static bool        raw_identified, raw_status_valid;
static uint8_t     raw_status;
static int         last_error;
unsigned int       yaffs_trace_mask;
extern void        yaffs_remove_device(struct yaffs_dev *);
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
#if WATCH_NAND_FULL_FORMAT
        format_io_failed = true;
        nand.writable    = false;
#endif
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
#if WATCH_NAND_FULL_FORMAT
    if (format_io_failed || scan_error)
    {
        format_io_failed = true;
        nand.writable    = false;
        return YAFFS_FAIL;
    }
#endif
    int r = ds35_program(&nand, (uint32_t)page, data, (size_t)bytes, oob, (size_t)on);
#if WATCH_NAND_FULL_FORMAT
    if (r != DS35_OK)
    {
        format_io_failed = scan_error = true;
        nand.writable                 = false;
    }
#endif
    return r == DS35_OK ? YAFFS_OK : YAFFS_FAIL;
}

static int erase_block(struct yaffs_dev *d, int block)
{
    if (d->read_only)
        return YAFFS_FAIL;
#if WATCH_NAND_FULL_FORMAT
    if (format_io_failed || scan_error)
    {
        format_io_failed = true;
        nand.writable    = false;
        return YAFFS_FAIL;
    }
#endif
    int r = ds35_erase(&nand, (uint32_t)block);
#if WATCH_NAND_FULL_FORMAT
    if (r != DS35_OK)
    {
        format_io_failed = scan_error = true;
        nand.writable                 = false;
    }
#endif
    return r == DS35_OK ? YAFFS_OK : YAFFS_FAIL;
}

static int mark_bad(struct yaffs_dev *d, int block)
{
#if WATCH_NAND_FULL_FORMAT
    /* YAFFS may request retirement after a failed write. This diagnostic stops
     * instead: never turn an ambiguous transport failure into a persistent BBM. */
    (void)d;
    (void)block;
    format_io_failed = scan_error = true;
    nand.writable                 = false;
    return YAFFS_FAIL;
#else
    return !d->read_only && ds35_mark_bad(&nand, (uint32_t)block) == DS35_OK ? YAFFS_OK
                                                                             : YAFFS_FAIL;
#endif
}

static int check_bad(struct yaffs_dev *d, int block)
{
    (void)d;
    int r = ds35_check_bad(&nand, (uint32_t)block);
    if (r != DS35_OK && r != DS35_BAD)
    {
        scan_error = true;
#if WATCH_NAND_FULL_FORMAT
        format_io_failed = true;
        nand.writable    = false;
#endif
    }
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
#if WATCH_NAND_FULL_FORMAT
        format_io_failed = true;
        nand.writable    = false;
#endif
        return YAFFS_FAIL;
    }
    struct yaffs_ext_tags tags = {0};
    if (d->tagger.read_chunk_tags_fn(d, block * 64, NULL, &tags) != YAFFS_OK)
    {
        scan_error = true;
#if WATCH_NAND_FULL_FORMAT
        format_io_failed = true;
        nand.writable    = false;
#endif
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

/* All these entry points share one worker. No separate diagnostic task may
 * race filesystem state or YAFFS's global errno. */
static bool claim_worker(void)
{
    if (!task_context() || (owner && owner != xTaskGetCurrentTaskHandle()))
        return false;
    owner = xTaskGetCurrentTaskHandle();
    return true;
}

const struct ds35_shutdown_diagnostic *mb8_storage_shutdown_diagnostic(void)
{
    return &nand.shutdown;
}

const struct ds35_operation_diagnostic *mb8_storage_operation_diagnostic(void)
{
    return &nand.operation;
}

int mb8_storage_probe_only(void)
{
    raw_identified   = false;
    raw_status_valid = false;
    if (!claim_worker() || dev.is_mounted)
    {
        status_text = "ID-only probe requires the storage worker and no mounted filesystem";
        return -1;
    }
    struct ds35_bus bus;
    int             r = mb8_nand_bus_open(&bus);
    if (r < 0)
    {
        status_text = mb8_nand_bus_diagnostic()->reason;
        return -1;
    }
    /* Zero count is an empty array-access range, NOT a guessed partition. */
    mb8_nand_diag_probe_started();
    r = ds35_probe(&nand, &bus, 0, 0);
    if (r == DS35_OK)
    {
        raw_identified   = true;
        r                = ds35_read_status(&nand, &raw_status);
        raw_status_valid = r == DS35_OK;
    }
    int shutdown = ds35_shutdown(&nand);
    mb8_nand_bus_close();
    status_text = r < 0          ? "ID/status probe failed (expected E5 A1); no filesystem access"
                  : shutdown < 0 ? "ID verified; feature restoration failed, reset required"
                                 : "E5 A1 ID/status verified; no array access or protection unlock";
    if (r >= 0 && shutdown >= 0 && mb8_nand_bus_diagnostic()->result < 0)
    {
        status_text = "ID/status verified; controller cleanup failed, reset required";
        return -1;
    }
    return r < 0 || shutdown < 0 ? -1 : 0;
}

int mb8_storage_get_usage(struct mb8_storage_usage *u)
{
    if (!u)
        return -1;
    memset(u, 0, sizeof(*u));
    if (!claim_worker())
        return -1;
    u->raw_identified = raw_identified;
    if (raw_identified)
    {
        u->raw_main_bytes  = (uint64_t)DS35_PAGE * DS35_PAGES_PER_BLOCK * DS35_BLOCKS;
        u->status_valid    = raw_status_valid;
        u->status_register = raw_status;
    }
    if (nand.fault || scan_error)
    {
        status_text = "NAND transport/scan fault; filesystem capacity unavailable";
        return -1;
    }
    if (!dev.is_mounted)
        return -1;
    Y_LOFF_T total      = yaffs_totalspace("/nand");
    Y_LOFF_T free_bytes = yaffs_freespace("/nand");
    /* Never cast negative error returns to unsigned or silently clamp counts. */
    if (total <= 0 || free_bytes < 0 || free_bytes > total)
    {
        status_text = "YAFFS capacity API returned error/inconsistent counts";
        return -1;
    }
    u->mounted                   = true;
    u->read_only                 = dev.read_only;
    u->total_bytes               = (uint64_t)total;
    u->free_bytes                = (uint64_t)free_bytes;
    u->used_or_unavailable_bytes = (uint64_t)(total - free_bytes);
    u->partition_main_bytes      = (uint64_t)partition_count() * DS35_PAGES_PER_BLOCK * DS35_PAGE;
    u->reserved_blocks           = dev.param.n_reserved_blocks;
    u->checkpoint_blocks         = dev.blocks_in_checkpt;
    for (uint32_t b = dev.internal_start_block; b <= dev.internal_end_block; b++)
        if (yaffs_get_block_info(&dev, (int)b)->block_state == YAFFS_BLOCK_STATE_DEAD)
            u->dead_blocks++;
    return 0;
}

int mb8_storage_read_sample(uint32_t relative_page, void *data, size_t bytes)
{
    if (!claim_worker() || !dev.is_mounted || !dev.read_only || !data || !bytes ||
        bytes > DS35_PAGE || relative_page >= partition_count() * DS35_PAGES_PER_BLOCK)
        return DS35_EINVAL;
    uint32_t page = partition_first() * DS35_PAGES_PER_BLOCK + relative_page;
    int      r    = ds35_check_bad(&nand, page / DS35_PAGES_PER_BLOCK);
    if (r != DS35_OK)
        return r;
    return ds35_read(&nand, page, data, bytes, NULL, 0);
}

#if WATCH_NAND_STARTUP_TESTS
static struct mb8_storage_raw_diagnostic raw_diagnostic;

const struct mb8_storage_raw_diagnostic *mb8_storage_raw_diagnostic(void)
{
    return &raw_diagnostic;
}

int mb8_storage_raw_read_pair(uint32_t physical_page, void *first, void *second, size_t bytes)
{
    if (!claim_worker() || dev.is_mounted || !first || !second || !bytes || bytes > 256 ||
        physical_page >= DS35_BLOCKS * DS35_PAGES_PER_BLOCK)
        return DS35_EINVAL;
    /* No automatic retry, and no replacement of first evidence by a later
     * poisoned-open NOT_READY. A firmware boot resets this temporary state. */
    if ((raw_diagnostic.primary_result < 0 && raw_diagnostic.primary_result != DS35_BAD) ||
        raw_diagnostic.shutdown_result < 0 || raw_diagnostic.bus_result < 0)
    {
        if (raw_diagnostic.primary_result < 0 && raw_diagnostic.primary_result != DS35_BAD)
            return raw_diagnostic.primary_result;
        return raw_diagnostic.shutdown_result < 0 ? raw_diagnostic.shutdown_result
                                                  : raw_diagnostic.bus_result;
    }
    raw_diagnostic =
        (struct mb8_storage_raw_diagnostic){.page = physical_page, .phase = "bus-open"};
    struct ds35_bus bus;
    int             r = mb8_nand_bus_open(&bus);
    if (r < 0)
    {
        raw_diagnostic.primary_result = r;
        raw_diagnostic.bus_result     = mb8_nand_bus_diagnostic()->result;
        status_text                   = "RAW bus open failed; no later RAW/filesystem checks";
        return r;
    }
    /* Explicit RAW diagnostic block, NOT a filesystem partition. */
    raw_diagnostic.phase = "probe";
    mb8_nand_diag_probe_started();
    r = ds35_probe(&nand, &bus, physical_page / DS35_PAGES_PER_BLOCK, 1);
    if (r == DS35_OK)
    {
        raw_diagnostic.phase = "bad-block-markers";
        r                    = ds35_check_bad(&nand, physical_page / DS35_PAGES_PER_BLOCK);
    }
    if (r == DS35_OK)
    {
        raw_diagnostic.phase = "first-data-read";
        r                    = ds35_read(&nand, physical_page, first, bytes, NULL, 0);
    }
    if (r >= 0)
    {
        raw_diagnostic.phase = "repeat-data-read";
        r                    = ds35_read(&nand, physical_page, second, bytes, NULL, 0);
    }
    if (r >= 0 && memcmp(first, second, bytes))
    {
        raw_diagnostic.phase = "repeat-consistency";
        r                    = DS35_IO;
    }
    raw_diagnostic.primary_result  = r;
    raw_diagnostic.shutdown_result = ds35_shutdown(&nand);
    mb8_nand_bus_close();
    raw_diagnostic.bus_result = mb8_nand_bus_diagnostic()->result;
    if ((r < 0 && r != DS35_BAD) || raw_diagnostic.shutdown_result < 0 ||
        raw_diagnostic.bus_result < 0)
        status_text =
            "RAW operation/cleanup failed; no later RAW/filesystem checks (see diagnostics)";
    /* Keep a real primary error; NOT_READY during cleanup must not erase it.
     * BAD alone is harmless ONLY when both feature restore and close succeed. */
    if (r < 0 && r != DS35_BAD)
        return r;
    if (raw_diagnostic.shutdown_result < 0)
        return raw_diagnostic.shutdown_result;
    if (raw_diagnostic.bus_result < 0)
        return raw_diagnostic.bus_result;
    return r;
}

static uint32_t raw_fingerprint(const uint8_t *data)
{
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < DS35_PAGE; i++)
        hash = (hash ^ data[i]) * 16777619u;
    return hash;
}

static int raw_reference(
    uint32_t page, const uint8_t *quad, uint8_t *serial, struct mb8_stock_scan *summary)
{
    raw_diagnostic.page  = page;
    raw_diagnostic.phase = "inventory-serial03-reference";
    int r                = ds35_read_x1_reference(&nand, page, serial, DS35_PAGE);
    if (r < 0)
        return r;
    for (uint32_t offset = 0; offset < DS35_PAGE; offset++)
    {
        if (quad[offset] == serial[offset])
            continue;
        summary->mismatch           = true;
        summary->mismatch_page      = page;
        summary->mismatch_offset    = offset;
        summary->quad_fingerprint   = raw_fingerprint(quad);
        summary->serial_fingerprint = raw_fingerprint(serial);
        raw_diagnostic.phase        = "inventory-serial-quad-disagreement";
        nand.operation.phase        = raw_diagnostic.phase;
        nand.operation.page         = page;
        nand.operation.result       = DS35_IO;
        scan_error = true; /* Data not trusted. Still attempt clean feature restoration. */
        return DS35_IO;
    }
    summary->reference_agree++;
    return DS35_OK;
}

int mb8_storage_raw_inventory(
    mb8_stock_page_visitor visitor, void *context, struct mb8_stock_scan *summary)
{
    _Static_assert(MB8_STOCK_SCAN_FIRST_PAGE < DS35_BLOCKS * DS35_PAGES_PER_BLOCK,
        "Stock census resume page outside chip");
    static uint8_t main_data[DS35_PAGE], serial_data[DS35_PAGE];
    if (!summary)
        return DS35_EINVAL;
    memset(summary, 0, sizeof(*summary));
    summary->first_page = summary->next_page = MB8_STOCK_SCAN_FIRST_PAGE;
    if (!claim_worker() || dev.is_mounted || !visitor)
        return DS35_EINVAL;
    if ((raw_diagnostic.primary_result < 0 && raw_diagnostic.primary_result != DS35_BAD) ||
        raw_diagnostic.shutdown_result < 0 || raw_diagnostic.bus_result < 0)
        return DS35_NOT_READY;
    raw_diagnostic = (struct mb8_storage_raw_diagnostic){.phase = "inventory-bus-open"};
    struct ds35_bus bus;
    int             r = mb8_nand_bus_open(&bus);
    if (r < 0)
    {
        raw_diagnostic.primary_result = r;
        raw_diagnostic.bus_result     = mb8_nand_bus_diagnostic()->result;
        status_text                   = "RAW census bus open failed; no later filesystem checks";
        return r;
    }
    /* Explicit whole-chip READ permission for diagnostics, not a partition or
     * filesystem profile. Neither this code nor reference reads authorize writes. */
    raw_diagnostic.phase = "inventory-probe";
    mb8_nand_diag_probe_started();
    r                            = ds35_probe(&nand, &bus, 0, DS35_BLOCKS);
    TickType_t started           = xTaskGetTickCount();
    summary->reference_available = bus.data_width == 4;
    const uint32_t references[]  = {0, 1, DS35_BLOCKS * DS35_PAGES_PER_BLOCK - 1};
    for (unsigned i = 0; r >= 0 && summary->reference_available && i < 3; i++)
    {
        if ((TickType_t)(xTaskGetTickCount() - started) >= pdMS_TO_TICKS(MB8_STOCK_SCAN_MS))
        {
            summary->time_limited = true;
            break;
        }
        raw_diagnostic.page  = references[i];
        raw_diagnostic.phase = "inventory-reference-bad-markers";
        r                    = ds35_check_bad(&nand, references[i] / DS35_PAGES_PER_BLOCK);
        if (r == DS35_BAD)
        {
            summary->reference_bad++;
            r = DS35_OK;
            continue;
        }
        if (r >= 0)
        {
            raw_diagnostic.phase = "inventory-reference-quad2048";
            r                    = ds35_read(&nand, references[i], main_data, DS35_PAGE, NULL, 0);
        }
        if (r >= 0)
            r = raw_reference(references[i], main_data, serial_data, summary);
        vTaskDelay(1);
    }
    unsigned extra_references = 0;
    uint32_t checked_block    = DS35_BLOCKS;
    while (r >= 0 && summary->next_page < DS35_BLOCKS * DS35_PAGES_PER_BLOCK)
    {
        if ((TickType_t)(xTaskGetTickCount() - started) >= pdMS_TO_TICKS(MB8_STOCK_SCAN_MS))
        {
            summary->time_limited = true;
            break;
        }
        uint32_t page = summary->next_page, block = page / DS35_PAGES_PER_BLOCK;
        raw_diagnostic.page = page;
        if (block != checked_block)
        {
            raw_diagnostic.phase = "inventory-bad-block-markers";
            r                    = ds35_check_bad(&nand, block);
            if (r == DS35_BAD)
            {
                summary->bad_blocks++;
                summary->next_page = (block + 1) * DS35_PAGES_PER_BLOCK;
                summary->bad_pages += summary->next_page - page;
                r = DS35_OK;
                vTaskDelay(1);
                continue;
            }
            if (r < 0)
                break;
            checked_block = block;
        }
        raw_diagnostic.phase = "inventory-ECC-main-read";
        r                    = ds35_read(&nand, page, main_data, sizeof(main_data), NULL, 0);
        if (r < 0)
            break;
        summary->corrected_pages += r == DS35_CORRECTED;
        /* Cross-check up to4 nonempty census pages beyond the3 preflight pages.
         * No comparison of the two buffers is printed as payload. */
        if (summary->reference_available && extra_references < 4 && page > 1 && page != 65535)
        {
            bool erased = true, zero = true;
            for (size_t i = 0; i < DS35_PAGE; i++)
            {
                erased &= main_data[i] == 0xff;
                zero &= main_data[i] == 0;
            }
            if (!erased && !zero)
            {
                r = raw_reference(page, main_data, serial_data, summary);
                if (r < 0)
                    break;
                extra_references++;
            }
        }
        summary->pages++;
        summary->next_page = page + 1;
        if (!visitor(page, main_data, context))
        {
            summary->visitor_stopped = true;
            break;
        }
        if ((summary->pages & 15u) == 0)
            vTaskDelay(1);
    }
    raw_diagnostic.primary_result  = r;
    raw_diagnostic.shutdown_result = ds35_shutdown(&nand);
    mb8_nand_bus_close();
    raw_diagnostic.bus_result = mb8_nand_bus_diagnostic()->result;
    if (r < 0 || raw_diagnostic.shutdown_result < 0 || raw_diagnostic.bus_result < 0)
        status_text = "RAW census/reference operation/cleanup failed; no later filesystem checks";
    if (r < 0)
        return r;
    if (raw_diagnostic.shutdown_result < 0)
        return raw_diagnostic.shutdown_result;
    if (raw_diagnostic.bus_result < 0)
        return raw_diagnostic.bus_result;
    return DS35_OK;
}

#endif
#if WATCH_NAND_FULL_FORMAT
#include "mb8_storage_log.h"
static uint8_t format_main[DS35_PAGE], format_tags[28];

static void consensus_evidence(uint32_t page, bool fast, int r, const struct ds35_read_consensus *e)
{
    mb8_storage_log(
        "[NAND FORMAT] %s page=%u rc=%d pass=%u mode=%u offset=%u serial-page-fnv=%08x compared-window-fnv=%08x\r\n",
        fast ? "optional48 diagnostic" : "MANDATORY12 consensus",
        (unsigned)page,
        r,
        e->pass,
        e->mode,
        e->offset,
        (unsigned)e->serial_hash,
        (unsigned)e->compared_hash);
    if (e->policy_reset_result < 0)
        mb8_storage_log("[NAND FORMAT] read-policy reset-to12 FAILED rc=%d; no unlock/erase even if48 data mismatch is otherwise optional\r\n", e->policy_reset_result);
    if (e->mismatch)
    {
        /* Bounded read-debug bytes only. The user authorized discarding stock
         * data; no full file/page dump. Both arrays start at the failed window. */
        for (unsigned i = 0; i < sizeof(e->serial_bytes); i += 4)
            mb8_storage_log("[NAND FORMAT] window+%u x1=%02x%02x%02x%02x x4=%02x%02x%02x%02x\r\n",
                i,
                e->serial_bytes[i],
                e->serial_bytes[i + 1],
                e->serial_bytes[i + 2],
                e->serial_bytes[i + 3],
                e->compared_bytes[i],
                e->compared_bytes[i + 1],
                e->compared_bytes[i + 2],
                e->compared_bytes[i + 3]);
    }
}

static uint8_t pattern_byte(uint32_t page, unsigned i)
{
    /* Word/lane/order-sensitive, not a uniform FF/00 repeat. */
    uint32_t x = (page + 1u) * 0x9e3779b9u + i * 0x45d9f3bu;
    return (uint8_t)(x ^ (x >> 11) ^ (x >> 23));
}

int mb8_storage_full_erase(void)
{
    if (!claim_worker() || dev.is_mounted || registered)
        return DS35_NOT_READY;
    format_provisioned = false;
    format_io_failed   = false;
    struct ds35_bus bus;
    int             r = mb8_nand_bus_open(&bus);
    if (r < 0)
    {
        status_text = mb8_nand_bus_diagnostic()->reason;
        return r;
    }
    mb8_nand_diag_probe_started();
    r                = ds35_probe(&nand, &bus, 0, DS35_BLOCKS);
    uint32_t    good = 0, bad = 0, first = DS35_BLOCKS, last = 0, erased = 0, verified = 0;
    TickType_t  started = xTaskGetTickCount();
    const char *phase   = "probe";
    if (r < 0)
        goto close;
    raw_identified   = true;
    raw_status_valid = false;
    phase            = "factory bad-block inventory BEFORE unlock";
    for (uint32_t b = 0; b < DS35_BLOCKS; b++)
    {
        r = ds35_check_bad(&nand, b);
        if (r == DS35_BAD)
            bad++;
        else if (r == DS35_OK)
        {
            if (first == DS35_BLOCKS)
                first = b;
            last = b;
            good++;
        }
        else
            goto close;
        if ((b & 15u) == 15u)
            vTaskDelay(1);
    }
    if (good < 16)
    {
        r = DS35_BAD;
        goto close;
    }
    mb8_storage_log("[NAND FORMAT] factory-good=%u factory-bad=%u; no unlock/program/erase yet\r\n",
        (unsigned)good,
        (unsigned)bad);
    const uint32_t samples[] = {first * DS35_PAGES_PER_BLOCK,
        first * DS35_PAGES_PER_BLOCK + 1u,
        last * DS35_PAGES_PER_BLOCK + 63u};
    phase                    = "mandatory12 same-cache preflight BEFORE unlock";
    for (unsigned i = 0; i < sizeof(samples) / sizeof(samples[0]); i++)
    {
        struct ds35_read_consensus e;
        r = ds35_read_consensus(&nand, samples[i], false, &e);
        consensus_evidence(samples[i], false, r, &e);
        if (r < 0)
            goto close;
    }
    if (mb8_nand_board.clock_hz == 48000000u)
    {
        struct ds35_read_consensus e;
        r = ds35_read_consensus(&nand, samples[0], true, &e);
        consensus_evidence(samples[0], true, r, &e);
        /* A healthy48 data mismatch is diagnostic, not a12MHz gate. A HAL or
         * ECC failure still stops: never replay a poisoned transport. */
        if (r < 0 && (!e.mismatch || nand.fault))
            goto close;
        r = DS35_OK;
    }
    phase = "authorized A0 unlock AFTER12 consensus";
    r     = ds35_allow_writes(&nand);
    if (r < 0)
        goto close;
    mb8_storage_log(
        "[NAND FORMAT] READY:12 consensus passed; destructive scratch block=%u, then all factory-good blocks0..1023;12MHz only\r\n",
        (unsigned)first);
    phase = "scratch erase/program/read known pattern";
    r     = ds35_erase(&nand, first);
    if (r < 0)
        goto close;
    /* Test first two pages, protecting marker bytes: program writes main plus
     * OOB4..31 only; all other user spare bytes remain FF. */
    for (uint32_t page = first * DS35_PAGES_PER_BLOCK; page < first * DS35_PAGES_PER_BLOCK + 2;
        page++)
    {
        for (unsigned i = 0; i < sizeof(format_main); i++)
            format_main[i] = pattern_byte(page, i);
        for (unsigned i = 0; i < sizeof(format_tags); i++)
            format_tags[i] = pattern_byte(page, i + DS35_PAGE);
        r = ds35_program(
            &nand, page, format_main, sizeof(format_main), format_tags, sizeof(format_tags));
        if (r < 0)
            goto close;
        r = ds35_read(
            &nand, page, format_main, sizeof(format_main), format_tags, sizeof(format_tags));
        if (r < 0)
            goto close;
        for (unsigned i = 0; i < sizeof(format_main); i++)
            if (format_main[i] != pattern_byte(page, i))
            {
                r = DS35_IO;
                goto close;
            }
        for (unsigned i = 0; i < sizeof(format_tags); i++)
            if (format_tags[i] != pattern_byte(page, i + DS35_PAGE))
            {
                r = DS35_IO;
                goto close;
            }
        struct ds35_read_consensus e;
        nand.writable = false; /* Comparison helper is always read-only. */
        r             = ds35_read_consensus(&nand, page, false, &e);
        consensus_evidence(page, false, r, &e);
        if (r < 0)
            goto close;
        nand.writable = true;
    }
    /* Physically re-read both factory-marker locations after scratch writes;
     * cached good-block state cannot prove marker preservation. */
    phase = "physical scratch factory-marker recheck";
    nand.checked[first / 8u] &= (uint8_t)~(1u << (first % 8u));
    r = ds35_check_bad(&nand, first);
    if (r != DS35_OK)
        goto close;
    mb8_storage_log(
        "[NAND FORMAT] known-pattern scratch PASS; now full usable-chip erase + every-page main/OOB4..63 FF verification\r\n");
    phase = "full erase and every-page verification";
    for (uint32_t b = 0; b < DS35_BLOCKS; b++)
    {
        if (nand.bad[b / 8] & (1u << (b % 8)))
            continue;
        r = ds35_erase(&nand, b);
        if (r < 0)
            goto close;
        erased++;
        for (uint32_t j = 0; j < DS35_PAGES_PER_BLOCK; j++)
        {
            uint8_t tags[DS35_TAG_CAPACITY];
            r = ds35_read(&nand,
                b * DS35_PAGES_PER_BLOCK + j,
                format_main,
                sizeof(format_main),
                tags,
                sizeof(tags));
            if (r < 0)
                goto close;
            for (unsigned i = 0; i < sizeof(format_main); i++)
                if (format_main[i] != 0xff)
                {
                    r = DS35_IO;
                    goto close;
                }
            for (unsigned i = 0; i < sizeof(tags); i++)
                if (tags[i] != 0xff)
                {
                    r = DS35_IO;
                    goto close;
                }
            verified++;
            if ((j & 15u) == 15u)
                vTaskDelay(1);
        }
        if ((b & 15u) == 15u)
            mb8_storage_log(
                "[NAND FORMAT] progress block=%u/1024 erased-good=%u verified-pages=%u\r\n",
                (unsigned)(b + 1),
                (unsigned)erased,
                (unsigned)verified);
    }
    r = DS35_OK;
close:;
    /* Primary error is never replaced by close error. No retries, no new bad
     * marks, no persistent provisioning marker. Healthy close restores A0/B0. */
    int primary   = r;
    nand.writable = false;
    int shutdown  = ds35_shutdown(&nand);
    mb8_nand_bus_close();
    int bus_result = mb8_nand_bus_diagnostic()->result;
    mb8_storage_log(
        "[NAND FORMAT] phase=%s primary=%d shutdown=%d bus=%d erased=%u/%u bad-skipped=%u verified-main+user-tag-pages=%u ticks=%u tick-Hz=%u\r\n",
        phase,
        primary,
        shutdown,
        bus_result,
        (unsigned)erased,
        (unsigned)good,
        (unsigned)bad,
        (unsigned)verified,
        (unsigned)(xTaskGetTickCount() - started),
        (unsigned)configTICK_RATE_HZ);
    if (primary < 0)
    {
        mb8_storage_log("[NAND FORMAT] core phase=%s physical-page=%u block=%u primary=%d\r\n",
            nand.operation.phase ? nand.operation.phase : "unavailable",
            (unsigned)nand.operation.page,
            (unsigned)(nand.operation.page / 64u),
            primary);
        if (nand.operation.status_valid)
            mb8_storage_log(
                "[NAND FORMAT] last completed C0=%02x\r\n", (unsigned)nand.operation.status);
    }
    if (primary >= 0 && shutdown >= 0 && bus_result >= 0 && erased == good &&
        verified == good * 64u)
    {
        format_provisioned = true;
        status_text =
            "full good-block erase verified; own OOB4 YAFFS profile authorized THIS BOOT ONLY";
        return DS35_OK;
    }
    format_io_failed = true;
    status_text =
        "FORMAT INCOMPLETE; no filesystem provisioning; inspect primary/protection/cleanup evidence";
    return primary < 0 ? primary : shutdown < 0 ? shutdown : bus_result < 0 ? bus_result : DS35_IO;
}
#endif

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
#if WATCH_NAND_FULL_FORMAT
    if (format_io_failed)
    {
        status_text = "format/test I/O failure latched; no more mounts or writes this boot";
        return -1;
    }
#endif
    if (partition_first() >= DS35_BLOCKS || partition_count() < 16 ||
        partition_count() > DS35_BLOCKS - partition_first())
    {
        status_text = "filesystem partition/bounds unconfirmed; no filesystem scan attempted";
        return -1;
    }
    if (!partition_authorized())
    {
        status_text = "YAFFS format/layout unconfirmed (stock/blank NAND is not proof); no scan";
        return -1;
    }
    if (!read_only && !writes_authorized())
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
        status_text = mb8_nand_bus_diagnostic()->reason;
        return -1;
    }
    mb8_nand_diag_probe_started();
    r = ds35_probe(&nand, &bus, partition_first(), partition_count());
    if (r < 0)
    {
        status_text = "NAND probe/JEDEC ID mismatch (expected E5 A1)";
        ds35_shutdown(&nand);
        mb8_nand_bus_close();
        return -1;
    }
    raw_identified   = true;
    raw_status_valid = false;
    if (ds35_read_status(&nand, &raw_status) < 0)
    {
        status_text = "NAND status read failed; no filesystem scan";
        ds35_shutdown(&nand);
        mb8_nand_bus_close();
        return -1;
    }
    raw_status_valid = true;
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
        dev.param.start_block           = partition_first();
        dev.param.end_block             = partition_first() + partition_count() - 1;
        dev.param.n_reserved_blocks     = 5;
        dev.param.n_caches              = MB8_NAND_CACHE_PAGES;
        dev.param.cache_bypass_aligned  = 1;
        dev.param.use_nand_ecc          = 1;
        dev.param.no_tags_ecc           = 0;
        dev.param.stored_endian         = 1;
        dev.param.max_objects           = MB8_NAND_MAX_OBJECTS;
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
        if (ds35_allow_writes(&nand) != DS35_OK || yaffs_remount("/nand", 0, 0) < 0
#if WATCH_NAND_FULL_FORMAT
            || format_io_failed
#endif
        )
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
    {
        status_text =
            "YAFFS unmount failed; cleanup NOT completed, handle/protection retained; power-cycle required";
        return -1;
    }
    nand.writable       = false;
    int shutdown_result = ds35_shutdown(&nand);
    mb8_nand_bus_close();
    if (shutdown_result < 0)
    {
        status_text = "NAND QE/ECC restore failed; reset/recovery required";
        return -1;
    }
#if WATCH_NAND_FULL_FORMAT
    if (format_io_failed || scan_error)
    {
        status_text = "format/test I/O failure retained despite successful hardware cleanup";
        return -1;
    }
#endif
    if (mb8_nand_bus_diagnostic()->result < 0)
    {
        status_text = "NAND bus close failed; reset required";
        return -1;
    }
    status_text = "NAND unmounted";
    return 0;
}

const struct ds35_nand *mb8_storage_protection_snapshot(void)
{
    return &nand;
}
