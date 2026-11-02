/* SPDX-License-Identifier: GPL-2.0-only
 * Temporary read-only diagnosis of the existing failed 2049-byte test file.
 * No unlock, program, erase, delete, format, or test-file creation.
 */
#include "mb8_nand_board.h"
#if WATCH_NAND_DEBUG_REPRO
#include "mb8_storage.h"
#include "mb8_storage_log.h"
#include "mb8_nand_diagnostics.h"
#include "ds35m1gb.h"
#include "yaffsfs.h"
#include "yaffs_osglue.h"
#include <stdint.h>

static uint8_t buffer[2048];
static const char path[] = "/nand/format-check/known-pattern.bin";
static uint8_t expected(unsigned offset)
{
    uint32_t x = offset * 0x45d9f3bu + 0x6d2b79f5u;
    return (uint8_t)(x ^ (x >> 9) ^ (x >> 21));
}

void mb8_storage_debug_repro(void)
{
    int fd = -1;
    bool mounted = false, clean = true;
    mb8_storage_log("[NAND DEBUG] read-only inspection of existing 2049-byte test; no NAND write/erase/delete.\r\n");
    if (mb8_storage_mount(true) < 0)
    {
        const struct mb8_nand_diagnostic *b = mb8_nand_bus_diagnostic();
        mb8_storage_log("[NAND DEBUG] mount FAIL status=%s bus=%d stage=%s HAL-valid=%u HAL=%u\r\n",
            mb8_storage_status(), b->result, b->stage ? b->stage : "none", (unsigned)b->hal_valid, (unsigned)b->hal_status);
        return;
    }
    mounted = true;
    struct yaffs_stat st;
    yaffsfs_SetError(0);
    int stat_rc = yaffs_lstat(path, &st);
    mb8_storage_log("[NAND DEBUG] lstat rc=%d errno=%d length=%lld read-error=%u\r\n", stat_rc,
        yaffs_get_error(), stat_rc == 0 ? (long long)st.st_size : -1LL, (unsigned)mb8_storage_debug_failed());
    if (stat_rc < 0 || mb8_storage_debug_failed()) goto done;
    yaffsfs_SetError(0);
    fd = yaffs_open(path, O_RDONLY, 0);
    if (fd < 0 || mb8_storage_debug_failed())
    {
        mb8_storage_log("[NAND DEBUG] open FAIL fd=%d errno=%d read-error=%u\r\n", fd,
            yaffs_get_error(), (unsigned)mb8_storage_debug_failed());
        goto done;
    }
    mb8_storage_debug_trace(true);
    const unsigned offsets[] = {0, 2048};
    const unsigned sizes[] = {2048, 1};
    for (unsigned k = 0; k < 2; k++)
    {
        yaffsfs_SetError(0);
        int n = yaffs_read(fd, buffer, sizes[k]);
        int err = yaffs_get_error();
        bool ioerr = mb8_storage_debug_failed();
        mb8_storage_log("[NAND DEBUG] read offset=%u requested=%u returned=%d errno=%d read-error=%u\r\n",
            offsets[k], sizes[k], n, err, (unsigned)ioerr);
        if (ioerr)
        {
            const struct mb8_nand_diagnostic *b = mb8_nand_bus_diagnostic();
            const struct ds35_operation_diagnostic *op = mb8_storage_operation_diagnostic();
            mb8_storage_log("[NAND DEBUG] IO failure bus=%d stage=%s HAL-valid=%u HAL=%u op=%s page=%u rc=%d status-valid=%u C0=%02x\r\n",
                b->result, b->stage ? b->stage : "none", (unsigned)b->hal_valid, (unsigned)b->hal_status,
                op->phase ? op->phase : "none", (unsigned)op->page, op->result,
                (unsigned)op->status_valid, (unsigned)op->status);
        }
        if (ioerr || n != (int)sizes[k]) { clean = false; break; }
        for (unsigned i = 0; i < sizes[k]; i++)
            if (buffer[i] != expected(offsets[k] + i))
            {
                mb8_storage_log("[NAND DEBUG] DATA MISMATCH offset=%u expected=%02x actual=%02x\r\n",
                    offsets[k] + i, (unsigned)expected(offsets[k] + i), (unsigned)buffer[i]);
                mb8_storage_debug_compare_last_page();
                clean = false;
                break;
            }
        if (!clean) break;
    }
    if (clean)
    {
        yaffsfs_SetError(0);
        int n = yaffs_read(fd, buffer, 1);
        mb8_storage_log("[NAND DEBUG] EOF returned=%d errno=%d read-error=%u\r\n",
            n, yaffs_get_error(), (unsigned)mb8_storage_debug_failed());
        clean = n == 0 && !mb8_storage_debug_failed() && stat_rc == 0 && st.st_size == 2049;
    }
    mb8_storage_log("[NAND DEBUG] recheck=%s; does NOT prove original failure cause or production readiness.\r\n",
        clean ? "matches" : "FAIL");
done:
    mb8_storage_debug_trace(false);
    if (fd >= 0)
    {
        int rc = yaffs_close(fd);
        mb8_storage_log("[NAND DEBUG] close rc=%d errno=%d\r\n", rc, yaffs_get_error());
    }
    if (mounted)
    {
        int rc = mb8_storage_unmount();
        mb8_storage_log("[NAND DEBUG] unmount rc=%d status=%s\r\n", rc, mb8_storage_status());
    }
}
#endif
