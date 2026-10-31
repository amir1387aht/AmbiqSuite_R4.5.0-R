/* SPDX-License-Identifier: GPL-2.0-only
 * TEMPORARY, bounded startup diagnostics; no formatting/raw programming.
 * All calls execute in the permanent storage worker, never another task.
 */
#include "mb8_storage_startup_tests.h"
#include "mb8_storage.h"
#include "yaffsfs.h"
#include "yaffs_osglue.h"
#include "mb8_nand_board.h"
#include "am_util.h"
#include "FreeRTOS.h"
#include "task.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>

static unsigned char first[256], second[256];

static uint32_t checksum(const unsigned char *p, size_t n)
{
    uint32_t h = 2166136261u;
    while (n--)
        h = (h ^ *p++) * 16777619u;
    return h;
}

static void file_read_test(void)
{
    yaffs_DIR *dir = yaffs_opendir("/nand");
    if (!dir)
    {
        am_util_stdio_printf("[NAND test] directory FAIL errno=%d\r\n", yaffs_get_error());
        return;
    }
    unsigned             seen   = 0;
    bool                 tested = false;
    struct yaffs_dirent *e;
    /* Bounded enumeration, no filenames or user content printed. Never follow
     * symlinks; select at most one existing regular file, read <=256 bytes twice. */
    while (seen < 16)
    {
        yaffsfs_SetError(0);
        e = yaffs_readdir(dir);
        if (!e)
        {
            if (yaffs_get_error())
                am_util_stdio_printf("[NAND test] readdir FAIL errno=%d\r\n", yaffs_get_error());
            break;
        }
        seen++;
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;
        char              path[sizeof(e->d_name) + 7];
        int               n = snprintf(path, sizeof(path), "/nand/%s", e->d_name);
        struct yaffs_stat st;
        if (n < 0 || (size_t)n >= sizeof(path))
            continue;
        if (yaffs_lstat(path, &st) < 0)
        {
            am_util_stdio_printf("[NAND test] lstat FAIL errno=%d\r\n", yaffs_get_error());
            continue;
        }
        if (!S_ISREG(st.st_mode) || st.st_size <= 0)
            continue;
        int fd = yaffs_open(path, O_RDONLY, 0);
        if (fd < 0)
        {
            am_util_stdio_printf("[NAND test] read-open FAIL errno=%d\r\n", yaffs_get_error());
            continue;
        }
        int  a       = yaffs_read(fd, first, sizeof(first));
        bool seek_ok = yaffs_lseek(fd, 0, SEEK_SET) == 0;
        int  b       = seek_ok ? yaffs_read(fd, second, sizeof(second)) : -1;
        bool ok      = a > 0 && b == a && !memcmp(first, second, (size_t)a);
        am_util_stdio_printf("[NAND test] existing-file RO repeat %s bytes=%d checksum=%08x\r\n",
            ok ? "PASS" : "FAIL",
            a,
            ok ? (unsigned)checksum(first, (size_t)a) : 0u);
        if (yaffs_close(fd) < 0)
            am_util_stdio_printf("[NAND test] RO close FAIL errno=%d\r\n", yaffs_get_error());
        tested = true;
        break;
    }
    if (yaffs_closedir(dir) < 0)
        am_util_stdio_printf("[NAND test] closedir FAIL errno=%d\r\n", yaffs_get_error());
    am_util_stdio_printf("[NAND test] directory read completed entries<=%u; file %s\r\n",
        seen,
        tested ? "attempted" : "SKIP (no nonempty regular file in first16 entries)");
}

void mb8_storage_startup_read_tests(void)
{
    const uint32_t pages[] = {0, 1, 63};
    for (unsigned i = 0; i < sizeof(pages) / sizeof(pages[0]); i++)
    {
        int  a  = mb8_storage_read_sample(pages[i], first, sizeof(first));
        int  b  = a >= 0 ? mb8_storage_read_sample(pages[i], second, sizeof(second)) : a;
        bool ok = a >= 0 && b >= 0 && !memcmp(first, second, sizeof(first));
        am_util_stdio_printf("[NAND test] partition-page%u RO repeat %s rc=%d/%d checksum=%08x\r\n",
            (unsigned)pages[i],
            ok ? "PASS" : "SKIP/FAIL",
            a,
            b,
            ok ? (unsigned)checksum(first, sizeof(first)) : 0u);
    }
    file_read_test();
}

#if WATCH_NAND_TEST_WRITE
static unsigned char pattern(unsigned position)
{
    return (unsigned char)((position * 37u + 0x5au) ^ (position >> 3));
}

void mb8_storage_startup_write_test(void)
{
    struct mb8_storage_usage usage;
    if (!mb8_nand_board.writes_authorized || !mb8_nand_board.yaffs_provisioned_confirmed ||
        mb8_storage_get_usage(&usage) < 0 || usage.read_only)
    {
        am_util_stdio_printf("[NAND test] write SKIP: no confirmed writable YAFFS mount\r\n");
        return;
    }
    /* One <=1024B exclusive file, bounded collision attempts. Never overwrite
     * or remove a pre-existing file, even if its name resembles our test name. */
    char     path[80];
    int      fd      = -1;
    bool     created = false, ok = true;
    unsigned seed = (unsigned)xTaskGetTickCount();
    for (unsigned attempt = 0; attempt < 8; attempt++)
    {
        snprintf(path, sizeof(path), "/nand/.mb8-startup-test-%08x-%u", seed, attempt);
        fd = yaffs_open(path, O_CREAT | O_EXCL | O_RDWR, 0600);
        if (fd >= 0)
        {
            created = true;
            break;
        }
        if (yaffs_get_error() != -EEXIST)
            break;
    }
    if (!created)
    {
        am_util_stdio_printf(
            "[NAND test] exclusive create SKIP/FAIL errno=%d; no existing file changed\r\n",
            yaffs_get_error());
        return;
    }
    for (unsigned offset = 0; offset < 1024 && ok; offset += sizeof(first))
    {
        for (unsigned i = 0; i < sizeof(first); i++)
            first[i] = pattern(offset + i);
        int n = yaffs_write(fd, first, sizeof(first));
        if (n != (int)sizeof(first))
        {
            am_util_stdio_printf(
                "[NAND test] write FAIL bytes=%d errno=%d\r\n", n, yaffs_get_error());
            ok = false;
        }
    }
    if (ok && yaffs_fsync(fd) < 0)
    {
        am_util_stdio_printf("[NAND test] fsync FAIL errno=%d\r\n", yaffs_get_error());
        ok = false;
    }
    /* Warm unmount/remount discards ALL YAFFS caches. fsync + same-fd read
     * alone can hit RAM cache (close does not always discard a clean cache).
     * This tests subsequent NAND reads, NOT arbitrary power-cut durability. */
    if (yaffs_close(fd) < 0)
    {
        am_util_stdio_printf("[NAND test] pre-readback close FAIL errno=%d\r\n", yaffs_get_error());
        ok = false;
    }
    fd = -1;
    if (ok)
    {
        if (mb8_storage_unmount() < 0 || mb8_storage_mount(true) < 0)
        {
            am_util_stdio_printf("[NAND test] warm RO remount FAIL: %s; residual test path=%s\r\n",
                mb8_storage_status(),
                path);
            return;
        }
        fd = yaffs_open(path, O_RDONLY, 0);
        if (fd < 0)
        {
            am_util_stdio_printf(
                "[NAND test] reopened media read FAIL errno=%d\r\n", yaffs_get_error());
            ok = false;
        }
    }
    for (unsigned offset = 0; offset < 1024 && ok; offset += sizeof(second))
    {
        int n = yaffs_read(fd, second, sizeof(second));
        if (n != (int)sizeof(second))
            ok = false;
        else
            for (unsigned i = 0; i < sizeof(second); i++)
                if (second[i] != pattern(offset + i))
                    ok = false;
        if (!ok)
            am_util_stdio_printf("[NAND test] readback FAIL offset=%u bytes=%d errno=%d\r\n",
                offset,
                n,
                yaffs_get_error());
    }
    if (fd >= 0 && yaffs_close(fd) < 0)
    {
        am_util_stdio_printf("[NAND test] close FAIL errno=%d\r\n", yaffs_get_error());
        ok = false;
    }
    /* Remount writable only if readback actually transitioned to RO. */
    struct mb8_storage_usage current;
    if (mb8_storage_get_usage(&current) < 0 ||
        (current.read_only && (mb8_storage_unmount() < 0 || mb8_storage_mount(false) < 0)))
    {
        am_util_stdio_printf("[NAND test] cleanup mount FAIL: %s; residual test path=%s\r\n",
            mb8_storage_status(),
            path);
        return;
    }
    /* Cleanup only the file successfully exclusively created by THIS call. */
    if (yaffs_unlink(path) < 0)
    {
        am_util_stdio_printf(
            "[NAND test] delete FAIL errno=%d; residual test path=%s\r\n", yaffs_get_error(), path);
        ok = false;
    }
    else
    {
        struct yaffs_stat st;
        int               absent = yaffs_lstat(path, &st);
        if (absent != -1 || yaffs_get_error() != -ENOENT)
        {
            am_util_stdio_printf(
                "[NAND test] absence verification FAIL errno=%d\r\n", yaffs_get_error());
            ok = false;
        }
        if (yaffs_sync("/nand") < 0)
        {
            am_util_stdio_printf("[NAND test] deletion sync FAIL errno=%d\r\n", yaffs_get_error());
            ok = false;
        }
    }
    am_util_stdio_printf(
        "[NAND test] exclusive1024B write/fsync/warm-remount-media-read/delete %s; "
        "unlink is logical deletion, NOT guaranteed physical erase\r\n",
        ok ? "PASS" : "FAIL");
}
#endif
