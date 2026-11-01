/* SPDX-License-Identifier: GPL-2.0-only
 * TEMPORARY destructive boot mode. Never selected by an ordinary build.
 * Runtime provisioning is deliberately not a persistent format/adoption marker.
 */
#include "mb8_storage.h"
#include "mb8_storage_log.h"
#include "ds35m1gb.h"
#include "yaffsfs.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>
#include <errno.h>

#if WATCH_NAND_FULL_FORMAT
static uint8_t    file_buffer[256];
static const char directory[] = "/nand/format-check";
static const char filename[]  = "/nand/format-check/known-pattern.bin";

static uint8_t file_byte(unsigned offset)
{
    uint32_t x = offset * 0x45d9f3bu + 0x6d2b79f5u;
    return (uint8_t)(x ^ (x >> 9) ^ (x >> 21));
}

static void transfer_rate(const char *label, unsigned bytes, TickType_t ticks)
{
    if (!ticks)
        mb8_storage_log(
            "[NAND FORMAT] %s %u bytes; below one tick, rate unavailable\r\n", label, bytes);
    else
        mb8_storage_log(
            "[NAND FORMAT] %s %u bytes ticks=%u tick-Hz=%u measured=%llu bytes/s; warm filesystem test, NOT raw maximum\r\n",
            label,
            bytes,
            (unsigned)ticks,
            (unsigned)configTICK_RATE_HZ,
            (unsigned long long)((uint64_t)bytes * configTICK_RATE_HZ / ticks));
}

static bool file_case(unsigned bytes)
{
    int         r;
    bool        mounted = false, pass = false, close_failed = false;
    int         fd    = -1;
    const char *phase = "mount newly erased own OOB4 layout RW";
    if (mb8_storage_mount(false) < 0)
        goto done;
    mounted = true;
    phase   = "mkdir/exclusive create";
    if (yaffs_mkdir(directory, 0700) < 0)
        goto done;
    fd = yaffs_open(filename, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd < 0)
        goto done;
    TickType_t start = xTaskGetTickCount();
    phase            = "write deterministic file/fsync/close";
    for (unsigned offset = 0; offset < bytes; offset += sizeof(file_buffer))
    {
        for (unsigned i = 0; i < sizeof(file_buffer); i++)
            file_buffer[i] = file_byte(offset + i);
        unsigned chunk =
            bytes - offset < sizeof(file_buffer) ? bytes - offset : sizeof(file_buffer);
        if (yaffs_write(fd, file_buffer, chunk) != (int)chunk)
            goto done;
        if ((offset & 1023u) == 0)
            vTaskDelay(1);
    }
    if (yaffs_fsync(fd) < 0)
        goto done;
    r  = yaffs_close(fd);
    fd = -1;
    if (r < 0)
        goto done;
    transfer_rate("file write+fsync+close", bytes, xTaskGetTickCount() - start);
    phase = "warm unmount/remount RO (discard YAFFS data cache)";
    if (mb8_storage_unmount() < 0)
    {
        close_failed = true;
        goto done;
    }
    mounted = false;
    if (mb8_storage_mount(true) < 0)
        goto done;
    mounted = true;
    fd      = yaffs_open(filename, O_RDONLY, 0);
    if (fd < 0)
        goto done;
    phase = "actual-media file readback";
    start = xTaskGetTickCount();
    for (unsigned offset = 0; offset < bytes; offset += sizeof(file_buffer))
    {
        unsigned chunk =
            bytes - offset < sizeof(file_buffer) ? bytes - offset : sizeof(file_buffer);
        if (yaffs_read(fd, file_buffer, chunk) != (int)chunk)
            goto done;
        for (unsigned i = 0; i < chunk; i++)
            if (file_buffer[i] != file_byte(offset + i))
                goto done;
        if ((offset & 1023u) == 0)
            vTaskDelay(1);
    }
    if (yaffs_read(fd, file_buffer, 1) != 0)
        goto done;
    r  = yaffs_close(fd);
    fd = -1;
    if (r < 0)
        goto done;
    transfer_rate("file RO read+verify+close", bytes, xTaskGetTickCount() - start);
    phase = "close RO then reopen RW to delete only test file";
    if (mb8_storage_unmount() < 0)
    {
        close_failed = true;
        goto done;
    }
    mounted = false;
    if (mb8_storage_mount(false) < 0)
        goto done;
    mounted = true;
    if (yaffs_unlink(filename) < 0 || yaffs_rmdir(directory) < 0 || yaffs_sync("/nand") < 0)
        goto done;
    phase = "warm remount verify deletion persisted";
    if (mb8_storage_unmount() < 0)
    {
        close_failed = true;
        goto done;
    }
    mounted = false;
    if (mb8_storage_mount(true) < 0)
        goto done;
    mounted = true;
    struct yaffs_stat st;
    if (yaffs_lstat(filename, &st) == 0 || yaffs_get_error() != -ENOENT)
        goto done;
    if (yaffs_lstat(directory, &st) == 0 || yaffs_get_error() != -ENOENT)
        goto done;
    struct mb8_storage_usage u;
    if (mb8_storage_get_usage(&u) < 0)
        goto done;
    mb8_storage_log(
        "[NAND FORMAT] own YAFFS used-or-unavailable / total - free: %llu / %llu - %llu bytes; reserved=%u dead=%u checkpoint=%u\r\n",
        (unsigned long long)u.used_or_unavailable_bytes,
        (unsigned long long)u.total_bytes,
        (unsigned long long)u.free_bytes,
        (unsigned)u.reserved_blocks,
        (unsigned)u.dead_blocks,
        (unsigned)u.checkpoint_blocks);
    phase = "final RO close / A0+B0 restoration";
    if (mb8_storage_unmount() < 0)
    {
        close_failed = true;
        goto done;
    }
    mounted = false;
    pass    = true;
done:
    if (fd >= 0 && yaffs_close(fd) < 0)
        pass = false;
    if (mounted && !close_failed && mb8_storage_unmount() < 0)
    {
        close_failed = true;
        pass         = false;
    }
    if (close_failed)
        mb8_storage_log(
            "[NAND FORMAT] unmount failure; no retry; cleanup state: %s\r\n", mb8_storage_status());
    mb8_storage_log(
        "[NAND FORMAT] file test bytes=%u %s phase=%s; warm remount is NOT power-loss proof; deletion is NOT proof of physical erase\r\n",
        bytes,
        pass ? "PASS" : "FAIL",
        phase);
    mb8_storage_log(
        "[NAND FORMAT] runtime provision is THIS BOOT ONLY. Reflash FULL_FORMAT=0 to prevent repeated erasure; ordinary profile remains RO/unprovisioned.\r\n");
    if (!pass)
        mb8_storage_log("[NAND FORMAT] filesystem failure reason=%s YAFFS-error=%d\r\n",
            mb8_storage_status(),
            yaffs_get_error());
    return pass;
}

bool mb8_storage_format_boot(void)
{
    if (mb8_storage_full_erase() < 0)
        return false;
    const unsigned sizes[] = {0, 1, 2047, 2048, 2049, 262144};
    for (unsigned i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++)
        if (!file_case(sizes[i]))
            return false;
    return true;
}
#endif
