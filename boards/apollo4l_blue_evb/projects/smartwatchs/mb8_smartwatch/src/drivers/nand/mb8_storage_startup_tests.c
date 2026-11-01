/* SPDX-License-Identifier: GPL-2.0-only
 * TEMPORARY, bounded startup diagnostics; no formatting/raw programming.
 * All calls execute in the permanent storage worker, never another task.
 */
#include "mb8_storage_startup_tests.h"
#include "mb8_storage.h"
#include "yaffsfs.h"
#include "yaffs_osglue.h"
#include "mb8_nand_board.h"
#include "ds35m1gb.h"
#include "mb8_storage_log.h"
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

/* One directory context, no recursive C calls. YAFFS Direct has no seekdir:
 * reopen a parent and replay its saved entry count after visiting a child.
 * The sole storage worker owns this read-only mount, so cursors cannot race
 * application mutations. Explicit budgets also bound corrupt sibling lists.
 * Static workspace is temporary-test RAM, not the worker's small stack. */
#define TREE_PATH_BYTES  768u
#define TREE_MAX_DEPTH   24u
#define TREE_MAX_ENTRIES 65536u
#define TREE_MAX_READDIR 262144u

struct tree_frame
{
    size_t   path_length;
    unsigned consumed;
    int      device, inode;
};
static struct tree_frame tree_frames[TREE_MAX_DEPTH + 1];
static char              tree_path[TREE_PATH_BYTES];
static char              tree_name[4 * 256 + 1];

static void tree_print_name(const char *name, unsigned depth, const char *kind, long long size)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t            n     = 0;
    for (const unsigned char *p = (const unsigned char *)name; *p; p++)
    {
        unsigned char c = *p;
        if (c == '\\' || c == '"')
        {
            tree_name[n++] = '\\';
            tree_name[n++] = (char)c;
        }
        else if (c < 0x20 || c >= 0x7f)
        {
            tree_name[n++] = '\\';
            tree_name[n++] = 'x';
            tree_name[n++] = hex[c >> 4];
            tree_name[n++] = hex[c & 15];
        }
        else
            tree_name[n++] = (char)c;
    }
    tree_name[n] = 0;
    /* Each complete log call stays below the private logger's 512-byte limit.
     * Very long escaped names use explicitly labelled continuation lines;
     * concatenate the quoted fragments (no filename bytes are omitted). */
    size_t offset = 0;
    do
    {
        size_t part = n - offset;
        if (part > 240)
            part = 240;
        mb8_storage_log("[NAND tree] %*s%s \"%.*s\"%s\r\n",
            (int)(depth * 2),
            "",
            offset ? "    name continued:" : "+--",
            (int)part,
            tree_name + offset,
            offset + part < n ? " [continued]" : "");
        offset += part;
    } while (offset < n);
    mb8_storage_log("[NAND tree] %*s    %s size=%lld bytes\r\n", (int)(depth * 2), "", kind, size);
}

static void repeat_file_read(const char *path)
{
    int fd = yaffs_open(path, O_RDONLY, 0);
    if (fd < 0)
    {
        mb8_storage_log("[NAND test] read-open FAIL errno=%d\r\n", yaffs_get_error());
        return;
    }
    int  a       = yaffs_read(fd, first, sizeof(first));
    bool seek_ok = yaffs_lseek(fd, 0, SEEK_SET) == 0;
    int  b       = seek_ok ? yaffs_read(fd, second, sizeof(second)) : -1;
    bool ok      = a > 0 && b == a && !memcmp(first, second, (size_t)a);
    mb8_storage_log("[NAND test] existing-file RO repeat %s bytes=%d checksum=%08x\r\n",
        ok ? "PASS" : "FAIL",
        a,
        ok ? (unsigned)checksum(first, (size_t)a) : 0u);
    if (yaffs_close(fd) < 0)
        mb8_storage_log("[NAND test] RO close FAIL errno=%d\r\n", yaffs_get_error());
}

static void file_read_test(void)
{
    struct yaffs_stat        st;
    unsigned                 depth = 0, entries = 0, reads = 0;
    bool                     complete = true, tested = false;
    yaffs_DIR               *dir = NULL;
    struct mb8_storage_usage usage;
    unsigned                 dropped_before = mb8_storage_log_dropped();
    if (mb8_storage_get_usage(&usage) < 0 || !usage.read_only)
    {
        mb8_storage_log("[NAND tree] SKIP: requires verified read-only YAFFS mount\r\n");
        return;
    }
    strcpy(tree_path, "/nand");
    mb8_storage_log("[NAND tree] /nand (read-only; symlinks NOT followed)\r\n");
    if (yaffs_lstat(tree_path, &st) < 0 || !S_ISDIR(st.st_mode))
    {
        mb8_storage_log("[NAND tree] INCOMPLETE: root stat/type errno=%d\r\n", yaffs_get_error());
        return;
    }
    tree_frames[0] = (struct tree_frame){5, 0, st.st_dev, st.st_ino};
    for (;;)
    {
        if (!dir)
        {
            dir = yaffs_opendir(tree_path);
            if (!dir)
            {
                mb8_storage_log("[NAND tree] INCOMPLETE: opendir depth=%u errno=%d\r\n",
                    depth,
                    yaffs_get_error());
                complete = false;
                goto ascend;
            }
            for (unsigned skip = 0; skip < tree_frames[depth].consumed; skip++)
            {
                if (reads++ >= TREE_MAX_READDIR)
                    goto budget;
                yaffsfs_SetError(0);
                if (!yaffs_readdir(dir))
                {
                    mb8_storage_log(
                        "[NAND tree] INCOMPLETE: parent replay changed/error errno=%d\r\n",
                        yaffs_get_error());
                    complete = false;
                    goto finish;
                }
                if (!(reads % 16))
                    vTaskDelay(1);
            }
        }
        if (reads++ >= TREE_MAX_READDIR)
            goto budget;
        yaffsfs_SetError(0);
        struct yaffs_dirent *e = yaffs_readdir(dir);
        if (!(reads % 16))
            vTaskDelay(1);
        if (!e)
        {
            if (yaffs_get_error())
            {
                mb8_storage_log("[NAND tree] INCOMPLETE: readdir depth=%u errno=%d\r\n",
                    depth,
                    yaffs_get_error());
                complete = false;
            }
            if (yaffs_closedir(dir) < 0)
            {
                dir = NULL;
                mb8_storage_log(
                    "[NAND tree] INCOMPLETE: closedir errno=%d; stopping\r\n", yaffs_get_error());
                complete = false;
                goto finish;
            }
            dir = NULL;
            goto ascend;
        }
        tree_frames[depth].consumed++;
        if (entries >= TREE_MAX_ENTRIES)
            goto budget;
        entries++;
        /* Never trust a corrupt name as a path segment. NAME_MAX is 255 in
         * this port; inspect its complete storage before any string use. */
        size_t length = 0;
        while (length < sizeof(e->d_name) && e->d_name[length])
            length++;
        if (!length || length >= sizeof(e->d_name) || length > 255 || strchr(e->d_name, '/'))
        {
            mb8_storage_log(
                "[NAND tree] INCOMPLETE: invalid directory-entry name depth=%u\r\n", depth);
            complete = false;
            continue;
        }
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
        {
            entries--;
            continue;
        }
        size_t parent_length = tree_frames[depth].path_length;
        if (parent_length + 1 + length >= sizeof(tree_path))
        {
            tree_print_name(e->d_name, depth, "UNINSPECTED (path limit)", -1);
            mb8_storage_log("[NAND tree] INCOMPLETE: path limit=%u bytes; subtree skipped\r\n",
                TREE_PATH_BYTES);
            complete = false;
            continue;
        }
        tree_path[parent_length] = '/';
        memcpy(tree_path + parent_length + 1, e->d_name, length + 1);
        if (yaffs_lstat(tree_path, &st) < 0)
        {
            tree_print_name(e->d_name, depth, "UNINSPECTED (lstat failed)", -1);
            mb8_storage_log("[NAND tree] INCOMPLETE: lstat errno=%d\r\n", yaffs_get_error());
            tree_path[parent_length] = 0;
            complete                 = false;
            continue;
        }
        bool        directory = S_ISDIR(st.st_mode);
        const char *kind      = directory             ? "directory/"
                                : S_ISLNK(st.st_mode) ? "symlink (not followed)"
                                : S_ISREG(st.st_mode) ? "file"
                                                      : "special (not opened)";
        tree_print_name(e->d_name, depth, kind, (long long)st.st_size);
        if (!tested && S_ISREG(st.st_mode) && st.st_size > 0)
        {
            tested = true;
            repeat_file_read(tree_path);
        }
        if (directory)
        {
            bool cycle = false;
            for (unsigned i = 0; i <= depth; i++)
                if (tree_frames[i].device == st.st_dev && tree_frames[i].inode == st.st_ino)
                    cycle = true;
            if (cycle || depth == TREE_MAX_DEPTH)
            {
                mb8_storage_log("[NAND tree] INCOMPLETE: %s; subtree skipped\r\n",
                    cycle ? "ancestor inode cycle" : "depth limit=24");
                complete = false;
            }
            else
            {
                if (yaffs_closedir(dir) < 0)
                {
                    dir = NULL;
                    mb8_storage_log(
                        "[NAND tree] INCOMPLETE: descent closedir errno=%d; stopping\r\n",
                        yaffs_get_error());
                    complete = false;
                    goto finish;
                }
                dir = NULL;
                depth++;
                tree_frames[depth] =
                    (struct tree_frame){parent_length + 1 + length, 0, st.st_dev, st.st_ino};
                vTaskDelay(1);
                continue;
            }
        }
        tree_path[parent_length] = 0;
        continue;
    ascend:
        if (!depth)
            break;
        depth--;
        tree_path[tree_frames[depth].path_length] = 0;
        vTaskDelay(1);
    }
    goto finish;
budget:
    mb8_storage_log("[NAND tree] INCOMPLETE: safety budget entries=%u/%u readdir=%u/%u\r\n",
        entries,
        TREE_MAX_ENTRIES,
        reads,
        TREE_MAX_READDIR);
    complete = false;
finish:
    if (dir && yaffs_closedir(dir) < 0)
    {
        mb8_storage_log("[NAND tree] INCOMPLETE: cleanup closedir errno=%d\r\n", yaffs_get_error());
        complete = false;
    }
    if (mb8_storage_log_dropped() != dropped_before)
    {
        complete = false;
        mb8_storage_log("[NAND tree] INCOMPLETE: UART log lines dropped\r\n");
    }
    mb8_storage_log("[NAND tree] %s entries=%u; existing-file repeat %s\r\n",
        complete ? "COMPLETE" : "INCOMPLETE",
        entries,
        tested ? "attempted (<=256B, no contents printed)"
               : "SKIP (no accessible nonempty regular file)");
}

bool mb8_storage_startup_raw_tests(void)
{
    const uint32_t pages[] = {0, 1, DS35_BLOCKS * DS35_PAGES_PER_BLOCK - 1};
    for (unsigned i = 0; i < sizeof(pages) / sizeof(pages[0]); i++)
    {
        int r = mb8_storage_raw_read_pair(pages[i], first, second, sizeof(first));
        if (r == DS35_BAD)
        {
            mb8_storage_log(
                "[NAND test] RAW physical-page%u SKIP: factory/runtime bad block; no data read\r\n",
                (unsigned)pages[i]);
            continue;
        }
        bool ok = r >= 0 && !memcmp(first, second, sizeof(first));
        mb8_storage_log(
            "[NAND test] RAW physical-page%u 256B repeat %s rc=%d checksum=%08x; NOT filesystem/free-space inference\r\n",
            (unsigned)pages[i],
            ok ? "PASS" : "FAIL",
            r,
            ok ? (unsigned)checksum(first, sizeof(first)) : 0u);
        if (!ok)
        {
            const struct mb8_storage_raw_diagnostic *d = mb8_storage_raw_diagnostic();
            mb8_storage_log("[NAND RAW] page=%u phase=%s primary=%d shutdown=%d bus=%d\r\n",
                (unsigned)d->page,
                d->phase,
                d->primary_result,
                d->shutdown_result,
                d->bus_result);
            mb8_storage_report_diagnostics(); /* BEFORE any new open resets snapshots. */
            mb8_storage_log(
                "[NAND test] remaining RAW/filesystem checks SKIP after first failure; no retry\r\n");
            return false;
        }
    }
    return true;
}

void mb8_storage_startup_read_tests(void)
{
    const uint32_t pages[] = {0, 1, 63};
    for (unsigned i = 0; i < sizeof(pages) / sizeof(pages[0]); i++)
    {
        int  a  = mb8_storage_read_sample(pages[i], first, sizeof(first));
        int  b  = a >= 0 ? mb8_storage_read_sample(pages[i], second, sizeof(second)) : a;
        bool ok = a >= 0 && b >= 0 && !memcmp(first, second, sizeof(first));
        mb8_storage_log("[NAND test] partition-page%u RO repeat %s rc=%d/%d checksum=%08x\r\n",
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
        mb8_storage_log("[NAND test] write SKIP: no confirmed writable YAFFS mount\r\n");
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
        mb8_storage_log(
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
            mb8_storage_log("[NAND test] write FAIL bytes=%d errno=%d\r\n", n, yaffs_get_error());
            ok = false;
        }
    }
    if (ok && yaffs_fsync(fd) < 0)
    {
        mb8_storage_log("[NAND test] fsync FAIL errno=%d\r\n", yaffs_get_error());
        ok = false;
    }
    /* Warm unmount/remount discards ALL YAFFS caches. fsync + same-fd read
     * alone can hit RAM cache (close does not always discard a clean cache).
     * This tests subsequent NAND reads, NOT arbitrary power-cut durability. */
    if (yaffs_close(fd) < 0)
    {
        mb8_storage_log("[NAND test] pre-readback close FAIL errno=%d\r\n", yaffs_get_error());
        ok = false;
    }
    fd = -1;
    if (ok)
    {
        if (mb8_storage_unmount() < 0 || mb8_storage_mount(true) < 0)
        {
            mb8_storage_log("[NAND test] warm RO remount FAIL: %s; residual test path=%s\r\n",
                mb8_storage_status(),
                path);
            return;
        }
        fd = yaffs_open(path, O_RDONLY, 0);
        if (fd < 0)
        {
            mb8_storage_log("[NAND test] reopened media read FAIL errno=%d\r\n", yaffs_get_error());
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
            mb8_storage_log("[NAND test] readback FAIL offset=%u bytes=%d errno=%d\r\n",
                offset,
                n,
                yaffs_get_error());
    }
    if (fd >= 0 && yaffs_close(fd) < 0)
    {
        mb8_storage_log("[NAND test] close FAIL errno=%d\r\n", yaffs_get_error());
        ok = false;
    }
    /* Remount writable only if readback actually transitioned to RO. */
    struct mb8_storage_usage current;
    if (mb8_storage_get_usage(&current) < 0 ||
        (current.read_only && (mb8_storage_unmount() < 0 || mb8_storage_mount(false) < 0)))
    {
        mb8_storage_log("[NAND test] cleanup mount FAIL: %s; residual test path=%s\r\n",
            mb8_storage_status(),
            path);
        return;
    }
    /* Cleanup only the file successfully exclusively created by THIS call. */
    if (yaffs_unlink(path) < 0)
    {
        mb8_storage_log(
            "[NAND test] delete FAIL errno=%d; residual test path=%s\r\n", yaffs_get_error(), path);
        ok = false;
    }
    else
    {
        struct yaffs_stat st;
        int               absent = yaffs_lstat(path, &st);
        if (absent != -1 || yaffs_get_error() != -ENOENT)
        {
            mb8_storage_log(
                "[NAND test] absence verification FAIL errno=%d\r\n", yaffs_get_error());
            ok = false;
        }
        if (yaffs_sync("/nand") < 0)
        {
            mb8_storage_log("[NAND test] deletion sync FAIL errno=%d\r\n", yaffs_get_error());
            ok = false;
        }
    }
    mb8_storage_log("[NAND test] exclusive1024B write/fsync/warm-remount-media-read/delete %s; "
                    "unlink is logical deletion, NOT guaranteed physical erase\r\n",
        ok ? "PASS" : "FAIL");
}
#endif
