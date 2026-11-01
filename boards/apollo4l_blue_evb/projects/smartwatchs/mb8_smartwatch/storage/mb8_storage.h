/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MB8_STORAGE_H
#define MB8_STORAGE_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#ifndef WATCH_NAND_YAFFS2
#define WATCH_NAND_YAFFS2 0
#endif
/* Call once from a dedicated, low-priority FreeRTOS storage worker, NOT ISR,
 * timer daemon, pre-scheduler startup, or the LVGL/render task. All yaffs_*
 * calls and unmount must run in that same worker; queue requests to it.
 * Startup creates its own worker after MCU/GPU initialization. No format API.
 * After success use yaffs_open/read/write/fsync/close under /nand.
 * Writes require board authorization AND read_only=false. A YAFFS partition
 * must be independently identified/provisioned; never use a stock-firmware
 * partition merely because the chip geometry matches.
 */
struct mb8_storage_usage
{
    bool     raw_identified, status_valid, mounted, read_only;
    uint8_t  status_register;
    uint64_t raw_main_bytes, partition_main_bytes;
    uint64_t total_bytes, free_bytes, used_or_unavailable_bytes;
    uint32_t reserved_blocks, dead_blocks, checkpoint_blocks;
};
#if WATCH_NAND_YAFFS2
/* Startup worker only. ID-only probe has no accessible array range. */
void mb8_storage_startup(void);
int  mb8_storage_probe_only(void);
/* Same storage worker only, snapshot valid until next NAND probe. */
struct ds35_shutdown_diagnostic;
const struct ds35_shutdown_diagnostic *mb8_storage_shutdown_diagnostic(void);
int                                    mb8_storage_get_usage(struct mb8_storage_usage *);
#if WATCH_NAND_STARTUP_TESTS
/* Explicit RAW diagnostic only: <=256B twice, closed read-only transaction.
 * Does NOT infer a filesystem partition or alter the ID-only empty-range API. */
int mb8_storage_raw_read_pair(uint32_t physical_page, void *, void *, size_t);
#endif
/* Diagnostics only: mounted partition-relative page, bounded <=2048B read. */
int         mb8_storage_read_sample(uint32_t relative_page, void *, size_t);
int         mb8_storage_mount(bool read_only);
int         mb8_storage_unmount(void);
const char *mb8_storage_status(void);
#else
static inline int mb8_storage_mount(bool read_only)
{
    (void)read_only;
    return -1;
}

static inline int mb8_storage_unmount(void)
{
    return -1;
}

static inline const char *mb8_storage_status(void)
{
    return "NAND support disabled (WATCH_NAND_YAFFS2=0)";
}
#endif
#endif
