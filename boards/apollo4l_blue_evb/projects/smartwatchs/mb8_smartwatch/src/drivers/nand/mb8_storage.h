/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MB8_STORAGE_H
#define MB8_STORAGE_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#ifndef WATCH_NAND_YAFFS2
#define WATCH_NAND_YAFFS2 0
#endif
/* All YAFFS and storage operations must run in the dedicated storage task.
 * Use mb8_storage_execute() from a normal task; never call from ISR, a timer
 * daemon, or the UI/render task. The caller and context must remain alive and
 * the calling task must not be deleted until the synchronous request returns.
 * No automatic formatting or provisioning is performed. See PARTITIONS.txt. */
typedef int (*mb8_storage_job_fn)(void *context);
struct mb8_storage_usage
{
    bool     raw_identified, status_valid, mounted, read_only;
    uint8_t  status_register;
    uint64_t raw_main_bytes, partition_main_bytes;
    uint64_t total_bytes, free_bytes, used_or_unavailable_bytes;
    uint32_t reserved_blocks, dead_blocks, checkpoint_blocks;
};
#if WATCH_NAND_YAFFS2
struct ds35_nand;
const struct ds35_nand *mb8_storage_protection_snapshot(void);
/* Initializes the worker once, after platform initialization. */
void mb8_storage_startup(void);
/* Returns the job result, or -1 if dispatch is unavailable. Jobs execute
 * serially on the storage worker; a job may call this recursively. */
int mb8_storage_execute(mb8_storage_job_fn job, void *context);
bool mb8_storage_is_worker(void);
/* ID-only probe has no accessible array range. */
int  mb8_storage_probe_only(void);
/* Same storage worker only, snapshot valid until next NAND probe. */
struct ds35_shutdown_diagnostic;
const struct ds35_shutdown_diagnostic *mb8_storage_shutdown_diagnostic(void);
struct ds35_operation_diagnostic;
const struct ds35_operation_diagnostic *mb8_storage_operation_diagnostic(void);
int                                     mb8_storage_get_usage(struct mb8_storage_usage *);
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
