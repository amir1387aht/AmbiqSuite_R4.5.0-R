/* SPDX-License-Identifier: GPL-2.0-only
 * Temporary automatic write diagnostic, not production provisioning. */
#include "mb8_nand_board.h"
#if WATCH_NAND_WRITE_TRACE
#include "mb8_storage.h"
#include "mb8_storage_log.h"
#include "yaffsfs.h"
#include "yaffs_osglue.h"
#include <stdint.h>
#include <errno.h>

static const char original[] = "/nand/format-check/known-pattern.bin";
#if WATCH_NAND_TRACE_CLOCK == 48000000
static const char previous_test[] = "/nand/wfix2";
static const char test[] = "/nand/wpio48a";
static const char bulk[] = "/nand/wpio48bulk";
static const char earlier_bulk[] = "/nand/wfixbulk";
#else
static const char previous_test[] = "/nand/write-trace-12.bin";
static const char test[] = "/nand/wtrace12b";
#endif
static uint8_t buf[2048];
static uint8_t pattern(unsigned offset)
{
    uint32_t x = offset * 0x45d9f3bu + 0x6d2b79f5u;
    return (uint8_t)(x ^ (x >> 9) ^ (x >> 21));
}
void mb8_storage_write_trace(void)
{
    bool mounted = false, writable = false, ok = false;
    int fd = -1, rc;
    const char *phase = "RO preflight";
    struct yaffs_stat st;
#if WATCH_NAND_TRACE_CLOCK == 48000000
    mb8_storage_log("[NAND WRITE] EXPERIMENT: 48MHz program-load PIO TXNEG=0, 48MHz read; new files only, bulk test deleted. No full format. YAFFS may reclaim empty blocks and old checkpoints.\r\n");
#else
    mb8_storage_log("[NAND WRITE] automatic test: read-target=%uHz, program-load <=12MHz; creates test files. No full format. YAFFS may reclaim empty blocks and old checkpoints.\r\n", (unsigned)WATCH_NAND_TRACE_CLOCK);
#endif
    if (mb8_storage_mount(true) < 0) goto done;
    mounted = true;
    mb8_storage_log("[NAND WRITE] read-only recovery scan PASS; array writes prohibited.\r\n");
#if WATCH_NAND_TRACE_CLOCK == 48000000
    phase = "read-only cross-check of failed page 131";
    rc = mb8_storage_debug_compare_page(131u);
    mb8_storage_log("[NAND WRITE] prior failed page 131 serial/quad rc=%d\r\n", rc);
    if (rc < 0 || mb8_storage_debug_failed()) goto done;
#endif
    yaffsfs_SetError(0);
    rc = yaffs_lstat(original, &st);
    if (rc < 0 || st.st_size != 2049 || mb8_storage_debug_failed()) goto done;
    phase = "verify original failed-file fingerprint";
    fd = yaffs_open(original, O_RDONLY, 0);
    if (fd < 0 || mb8_storage_debug_failed()) goto done;
    mb8_storage_debug_trace(true);
    rc = yaffs_read(fd, buf, sizeof(buf));
    if (rc != (int)sizeof(buf) || mb8_storage_debug_failed()) goto done;
    for (unsigned i=0; i<sizeof(buf); i++) if (buf[i] != pattern(i)) goto done;
    rc = yaffs_read(fd, buf, 1);
    if (rc != 1 || buf[0] != 0x9c || mb8_storage_debug_failed() ||
        mb8_storage_debug_last_page() != 46u) goto done;
    mb8_storage_debug_trace(false);
    rc = yaffs_close(fd); fd = -1;
    if (rc < 0) goto done;
    mb8_storage_log("[NAND WRITE] preflight original page=46 byte=9c; first page matches.\r\n");
    phase = "inspect previous test";
    yaffsfs_SetError(0);
    rc = yaffs_lstat(previous_test, &st);
    int prior_error = yaffs_get_error();
    mb8_storage_log("[NAND WRITE] previous-test stat rc=%d errno=%d length=%lld read-error=%u\r\n",
        rc, prior_error, rc == 0 ? (long long)st.st_size : -1LL, (unsigned)mb8_storage_debug_failed());
    if (mb8_storage_debug_failed() || (rc < 0 && prior_error != -ENOENT)) goto done;
#if WATCH_NAND_TRACE_CLOCK == 48000000
    if (rc != 0 || st.st_size != 2049) {
        phase = "previous mixed-speed file missing or wrong size; no writes"; goto done;
    }
#endif
    if (rc == 0 && st.st_size == 2049) {
        fd = yaffs_open(previous_test, O_RDONLY, 0);
        if (fd < 0 || mb8_storage_debug_failed()) goto done;
        bool prior_match = true;
        for (unsigned offset=0; offset<2049; offset+=sizeof(buf)) {
            unsigned n = 2049-offset < sizeof(buf) ? 2049-offset : sizeof(buf);
            yaffsfs_SetError(0);
            rc = yaffs_read(fd, buf, n);
            mb8_storage_log("[NAND WRITE] previous-test read offset=%u requested=%u rc=%d errno=%d read-error=%u\r\n",
                offset, n, rc, yaffs_get_error(), (unsigned)mb8_storage_debug_failed());
            if (rc != (int)n || mb8_storage_debug_failed()) goto done;
            for (unsigned i=0; i<n; i++) if (buf[i] != pattern(offset+i)) {
                mb8_storage_log("[NAND WRITE] previous-test mismatch offset=%u expected=%02x actual=%02x\r\n",
                    offset+i, (unsigned)pattern(offset+i), (unsigned)buf[i]);
                prior_match = false;
                break;
            }
            if (!prior_match) break;
        }
        mb8_storage_log("[NAND WRITE] previous-test pattern=%s\r\n", prior_match ? "matches" : "mismatch");
        rc = yaffs_close(fd); fd = -1;
        if (rc < 0) goto done;
#if WATCH_NAND_TRACE_CLOCK == 48000000
        if (!prior_match) { phase = "previous mixed-speed file mismatch; no writes"; goto done; }
#endif
    }
#if WATCH_NAND_TRACE_CLOCK == 48000000
    phase = "inspect previous delete target";
    yaffsfs_SetError(0);
    rc = yaffs_lstat(earlier_bulk, &st);
    int earlier_err = yaffs_get_error();
    mb8_storage_log("[NAND WRITE] previous bulk stat rc=%d errno=%d length=%lld; not explicitly deleted\r\n",
        rc, earlier_err, rc == 0 ? (long long)st.st_size : -1LL);
    if (mb8_storage_debug_failed() || (rc < 0 && earlier_err != -ENOENT)) goto done;
#endif
    phase = "check exclusive test path";
    yaffsfs_SetError(0);
    rc = yaffs_lstat(test, &st);
    int test_error = yaffs_get_error();
    mb8_storage_log("[NAND WRITE] new-test stat rc=%d errno=%d length=%lld read-error=%u\r\n",
        rc, test_error, rc == 0 ? (long long)st.st_size : -1LL, (unsigned)mb8_storage_debug_failed());
    if (rc == 0 || test_error != -ENOENT || mb8_storage_debug_failed()) {
        phase = "test file already exists or preflight failed; will not overwrite"; goto done;
    }
#if WATCH_NAND_TRACE_CLOCK == 48000000
    yaffsfs_SetError(0);
    rc = yaffs_lstat(bulk, &st);
    if (rc == 0 || yaffs_get_error() != -ENOENT || mb8_storage_debug_failed()) {
        phase = "bulk test already exists or preflight failed; will not overwrite"; goto done;
    }
#endif
    if (mb8_storage_unmount() < 0) { phase = "RO shutdown failed"; goto done; }
    mounted = false;
    phase = "RW mount";
    if (mb8_storage_mount(false) < 0) goto done;
    mounted = writable = true;
    phase = "exclusive create";
    fd = yaffs_open(test, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd < 0 || mb8_storage_debug_failed()) goto done;
    for (unsigned offset=0; offset<2049; offset+=sizeof(buf)) {
        unsigned n = 2049-offset < sizeof(buf) ? 2049-offset : sizeof(buf);
        for (unsigned i=0; i<n; i++) buf[i] = pattern(offset+i);
        mb8_storage_log("[NAND WRITE] file offset=%u size=%u input0=%02x\r\n", offset, n, (unsigned)buf[0]);
        phase = "file write";
        yaffsfs_SetError(0);
        rc = yaffs_write(fd, buf, n);
        mb8_storage_log("[NAND WRITE] file write rc=%d errno=%d failed=%u\r\n", rc, yaffs_get_error(), (unsigned)mb8_storage_debug_failed());
        if (rc != (int)n || mb8_storage_debug_failed()) goto done;
    }
    phase = "fsync";
    yaffsfs_SetError(0); rc = yaffs_fsync(fd);
    mb8_storage_log("[NAND WRITE] fsync rc=%d errno=%d failed=%u\r\n", rc, yaffs_get_error(), (unsigned)mb8_storage_debug_failed());
    if (rc < 0 || mb8_storage_debug_failed()) goto done;
    phase = "close RW file";
    rc = yaffs_close(fd); fd = -1;
    if (rc < 0 || mb8_storage_debug_failed()) goto done;
    phase = "RW unmount";
    rc = mb8_storage_unmount();
    if (rc < 0) goto done;
    mounted = writable = false;
    phase = "RO remount";
    if (mb8_storage_mount(true) < 0) goto done;
    mounted = true;
    phase = "RO verify file";
    fd = yaffs_open(test, O_RDONLY, 0);
    if (fd < 0 || mb8_storage_debug_failed()) goto done;
    for (unsigned offset=0; offset<2049; offset+=sizeof(buf)) {
        unsigned n = 2049-offset < sizeof(buf) ? 2049-offset : sizeof(buf);
#if WATCH_NAND_TRACE_CLOCK == 48000000
        if (offset == 2048) mb8_storage_debug_trace(true);
#endif
        yaffsfs_SetError(0); rc=yaffs_read(fd, buf, n);
        mb8_storage_log("[NAND WRITE] readback offset=%u requested=%u rc=%d errno=%d failed=%u\r\n", offset,n,rc,yaffs_get_error(),(unsigned)mb8_storage_debug_failed());
        if (rc != (int)n || mb8_storage_debug_failed()) goto done;
        for (unsigned i=0; i<n; i++) if (buf[i] != pattern(offset+i)) {
            mb8_storage_log("[NAND WRITE] MISMATCH offset=%u expected=%02x actual=%02x\r\n", offset+i,(unsigned)pattern(offset+i),(unsigned)buf[i]); goto done;
        }
#if WATCH_NAND_TRACE_CLOCK == 48000000
        if (offset == 2048) {
            mb8_storage_debug_trace(false);
            phase = "RO serial/quad cross-check";
            rc = mb8_storage_debug_compare_last_page();
            mb8_storage_log("[NAND WRITE] serial/quad cross-check rc=%d\r\n", rc);
            if (rc < 0 || mb8_storage_debug_failed()) goto done;
            phase = "RO verify file";
        }
#endif
    }
    rc=yaffs_read(fd,buf,1); if (rc != 0 || mb8_storage_debug_failed()) goto done;
    phase = "final RO close";
    rc=yaffs_close(fd); fd=-1; if (rc < 0) goto done;
#if WATCH_NAND_TRACE_CLOCK == 48000000
    /* Exercise a multi-page file and a delete, without touching existing
     * files. Each actual page program still has full data/tag verification. */
    phase = "RO unmount before bulk test";
    rc = mb8_storage_unmount(); if (rc < 0) goto done; mounted = false;
    phase = "bulk RW mount";
    if (mb8_storage_mount(false) < 0) goto done;
    mounted = writable = true;
    phase = "bulk exclusive create";
    fd = yaffs_open(bulk, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd < 0 || mb8_storage_debug_failed()) goto done;
    phase = "bulk write 65536 bytes";
    for (unsigned offset=0; offset<65536; offset+=sizeof(buf)) {
        for (unsigned i=0; i<sizeof(buf); i++) buf[i]=pattern(offset+i);
        rc = yaffs_write(fd, buf, sizeof(buf));
        if (rc != (int)sizeof(buf) || mb8_storage_debug_failed()) goto done;
    }
    rc=yaffs_fsync(fd); if (rc < 0 || mb8_storage_debug_failed()) goto done;
    rc=yaffs_close(fd); fd=-1; if (rc < 0 || mb8_storage_debug_failed()) goto done;
    phase = "bulk RW unmount";
    rc=mb8_storage_unmount(); if (rc < 0) goto done; mounted=writable=false;
    phase = "bulk RO remount";
    if (mb8_storage_mount(true) < 0) goto done;
    mounted=true;
    phase = "bulk read verify";
    fd=yaffs_open(bulk,O_RDONLY,0); if (fd < 0 || mb8_storage_debug_failed()) goto done;
    for (unsigned offset=0; offset<65536; offset+=sizeof(buf)) {
        rc=yaffs_read(fd,buf,sizeof(buf));
        if (rc != (int)sizeof(buf) || mb8_storage_debug_failed()) goto done;
        for (unsigned i=0; i<sizeof(buf); i++) if (buf[i] != pattern(offset+i)) {
            mb8_storage_log("[NAND WRITE] bulk mismatch offset=%u expected=%02x actual=%02x\r\n",offset+i,(unsigned)pattern(offset+i),(unsigned)buf[i]); goto done;
        }
    }
    rc=yaffs_read(fd,buf,1); if (rc != 0 || mb8_storage_debug_failed()) goto done;
    rc=yaffs_close(fd); fd=-1; if (rc < 0 || mb8_storage_debug_failed()) goto done;
    mb8_storage_log("[NAND WRITE] bulk 65536-byte warm-remount readback PASS\r\n");
    phase = "RO unmount before delete";
    rc=mb8_storage_unmount(); if (rc < 0) goto done; mounted=false;
    phase = "delete RW mount";
    if (mb8_storage_mount(false) < 0) goto done;
    mounted=writable=true;
    phase = "delete bulk test only";
    rc=yaffs_unlink(bulk); if (rc < 0 || mb8_storage_debug_failed()) goto done;
    rc=yaffs_sync("/nand"); if (rc < 0 || mb8_storage_debug_failed()) goto done;
    phase = "delete RW unmount";
    rc=mb8_storage_unmount(); if (rc < 0) goto done; mounted=writable=false;
    phase = "delete RO remount";
    if (mb8_storage_mount(true) < 0) goto done;
    mounted=true;
    phase = "verify bulk deleted";
    yaffsfs_SetError(0); rc=yaffs_lstat(bulk,&st);
    if (rc == 0 || yaffs_get_error() != -ENOENT || mb8_storage_debug_failed()) goto done;
    mb8_storage_log("[NAND WRITE] bulk delete warm-remount PASS\r\n");
    phase = "verify retained files after delete";
    const char *retained[] = {previous_test, test};
    for (unsigned k=0; k<2; k++) {
        fd=yaffs_open(retained[k],O_RDONLY,0);
        if (fd < 0 || mb8_storage_debug_failed()) goto done;
        for (unsigned offset=0; offset<2049; offset+=sizeof(buf)) {
            unsigned n = 2049-offset < sizeof(buf) ? 2049-offset : sizeof(buf);
            rc=yaffs_read(fd,buf,n);
            if (rc != (int)n || mb8_storage_debug_failed()) goto done;
            for (unsigned i=0; i<n; i++) if (buf[i] != pattern(offset+i)) {
                mb8_storage_log("[NAND WRITE] retained-file mismatch file=%s offset=%u\r\n",retained[k],offset+i);
                goto done;
            }
        }
        rc=yaffs_read(fd,buf,1); if (rc != 0 || mb8_storage_debug_failed()) goto done;
        rc=yaffs_close(fd); fd=-1; if (rc < 0 || mb8_storage_debug_failed()) goto done;
    }
    mb8_storage_log("[NAND WRITE] retained files after delete PASS\r\n");
#endif
    phase = "final RO unmount";
    rc=mb8_storage_unmount(); if (rc < 0) goto done; mounted=false;
    ok=true;
done:
    mb8_storage_debug_trace(false);
    mb8_storage_log("[NAND WRITE] RESULT=%s phase=%s status=%s errno=%d; test file retained if created.\r\n",ok?"PASS":"FAIL",phase,mb8_storage_status(),yaffs_get_error());
    if (!ok && writable) mb8_storage_write_trace_abort();
    if (fd >= 0) { rc=yaffs_close(fd); mb8_storage_log("[NAND WRITE] cleanup close rc=%d\r\n",rc); }
    if (mounted) { rc=mb8_storage_unmount(); mb8_storage_log("[NAND WRITE] cleanup unmount rc=%d status=%s\r\n",rc,mb8_storage_status()); }
}
#endif
