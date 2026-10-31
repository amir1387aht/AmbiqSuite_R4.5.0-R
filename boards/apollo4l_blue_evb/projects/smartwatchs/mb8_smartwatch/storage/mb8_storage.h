/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MB8_STORAGE_H
#define MB8_STORAGE_H
#include <stdbool.h>
#ifndef WATCH_NAND_YAFFS2
#define WATCH_NAND_YAFFS2 0
#endif
/* Call once from a dedicated, low-priority FreeRTOS storage worker, NOT ISR,
 * timer daemon, pre-scheduler startup, or the LVGL/render task. All yaffs_*
 * calls and unmount must run in that same worker; queue requests to it.
 * No mount/format/erase is added to watch startup. No automatic format API.
 * After success use yaffs_open/read/write/fsync/close under /nand.
 * Writes require board authorization AND read_only=false. A YAFFS partition
 * must be independently identified/provisioned; never use a stock-firmware
 * partition merely because the chip geometry matches.
 */
#if WATCH_NAND_YAFFS2
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
