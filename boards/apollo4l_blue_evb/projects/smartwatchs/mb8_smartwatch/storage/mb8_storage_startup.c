/* SPDX-License-Identifier: GPL-2.0-only
 * Permanent startup worker/report. Temporary diagnostics are a separate unit.
 */
#include "mb8_storage.h"
#include "mb8_nand_board.h"
#include "FreeRTOS.h"
#include "task.h"
#include "am_util.h"
#include "watch_gpu.h"
#if WATCH_NAND_STARTUP_TESTS
#include "mb8_storage_startup_tests.h"
#endif

static TaskHandle_t worker;

static void print_size(const char *label, uint64_t bytes)
{
    am_util_stdio_printf("[NAND] %s=%llu bytes (%llu KiB, %llu.%03llu MiB)\r\n",
        label,
        (unsigned long long)bytes,
        (unsigned long long)(bytes / 1024u),
        (unsigned long long)(bytes / 1048576u),
        (unsigned long long)((bytes % 1048576u) * 1000u / 1048576u));
}

static void report(void)
{
    struct mb8_storage_usage u = {0};
    int                      r = mb8_storage_get_usage(&u);
    if (u.raw_identified)
    {
        am_util_stdio_printf("[NAND] ID=E5 A1; raw capacity from confirmed geometry\r\n");
        if (u.status_valid)
            am_util_stdio_printf("[NAND] sampled status=0x%02x (not a live health guarantee)\r\n",
                (unsigned)u.status_register);
        else
            am_util_stdio_printf("[NAND] status unavailable: status read did not complete\r\n");
        print_size("raw chip MAIN (not filesystem free space)", u.raw_main_bytes);
    }
    else
        am_util_stdio_printf("[NAND] raw capacity unavailable: ID not successfully probed\r\n");
    if (r < 0)
    {
        am_util_stdio_printf(
            "[NAND] filesystem used / total - free: unavailable: %s\r\n", mb8_storage_status());
        return;
    }
    am_util_stdio_printf("[NAND] YAFFS /nand %s used_or_unavailable / total - free: "
                         "%llu / %llu - %llu bytes\r\n",
        u.read_only ? "RO" : "RW",
        (unsigned long long)u.used_or_unavailable_bytes,
        (unsigned long long)u.total_bytes,
        (unsigned long long)u.free_bytes);
    print_size("used_or_unavailable", u.used_or_unavailable_bytes);
    print_size("YAFFS API total", u.total_bytes);
    print_size("YAFFS allocatable free", u.free_bytes);
    print_size("selected partition MAIN", u.partition_main_bytes);
    am_util_stdio_printf(
        "[NAND] partition dead blocks=%u GC-reserved=%u checkpoint-present=%u; "
        "total excludes GC reserve; used includes metadata/bad/checkpoint/other unavailable space, "
        "NOT sum of file lengths.\r\n",
        (unsigned)u.dead_blocks,
        (unsigned)u.reserved_blocks,
        (unsigned)u.checkpoint_blocks);
}

static void startup_worker(void *unused)
{
    (void)unused;
    while (!watch_gpu_ready)
        vTaskDelay(pdMS_TO_TICKS(10));
    am_util_stdio_printf("[NAND] test-write request=%u (default0); actual writes also require "
                         "confirmed provisioned partition + authorization\r\n",
        (unsigned)WATCH_NAND_TEST_WRITE);
    am_util_stdio_printf(
        "[NAND] initial phase: RO; volatile reset/QE/ECC setup only; no protection unlock, "
        "program, erase or format. Bring-up clock12MHz;48MHz requires board validation.\r\n");
    if (mb8_storage_probe_only() < 0)
    {
        am_util_stdio_printf("[NAND] probe SKIP/FAIL: %s\r\n", mb8_storage_status());
        report();
#if WATCH_NAND_STARTUP_TESTS
        am_util_stdio_printf("[NAND test] RO checks SKIP: probe/electrical guards failed\r\n");
#endif
    }
    else
    {
        if (mb8_storage_mount(true) < 0)
        {
            am_util_stdio_printf("[NAND] RO mount SKIP/FAIL: %s\r\n", mb8_storage_status());
            report();
#if WATCH_NAND_STARTUP_TESTS
            am_util_stdio_printf(
                "[NAND test] RO checks SKIP: no confirmed readable YAFFS partition\r\n");
#endif
        }
        else
        {
            report();
#if WATCH_NAND_STARTUP_TESTS
            mb8_storage_startup_read_tests();
#endif
            int closed = mb8_storage_unmount();
            if (closed < 0)
                am_util_stdio_printf("[NAND] RO unmount FAIL: %s\r\n", mb8_storage_status());
#if WATCH_NAND_STARTUP_TESTS && WATCH_NAND_TEST_WRITE
            if (closed == 0 && mb8_nand_board.writes_authorized &&
                mb8_nand_board.yaffs_provisioned_confirmed)
            {
                if (mb8_storage_mount(false) < 0)
                    am_util_stdio_printf(
                        "[NAND test] write SKIP/FAIL: %s\r\n", mb8_storage_status());
                else
                {
                    mb8_storage_startup_write_test();
                    report();
                    if (mb8_storage_unmount() < 0)
                        am_util_stdio_printf(
                            "[NAND test] RW unmount FAIL: %s\r\n", mb8_storage_status());
                }
            }
            else
                am_util_stdio_printf(
                    "[NAND test] write SKIP: authorization absent or RO close failed\r\n");
#endif
        }
    }
#if WATCH_NAND_STARTUP_TESTS && !WATCH_NAND_TEST_WRITE
    am_util_stdio_printf("[NAND test] write/delete SKIP: WATCH_NAND_TEST_WRITE=0\r\n");
#endif
    /* Keep the owner TaskHandle valid even on failed unmount. All future storage
     * work must be dispatched to this worker, never a second YAFFS client task.
     * No future requests are scheduled by this one-shot startup diagnostic. */
    for (;;)
        vTaskSuspend(NULL);
}

void mb8_storage_startup(void)
{
    if (worker)
        return;
    /* Called at the END of mcu_init, after UART and Nema/shared-SRAM setup. */
    if (xTaskCreate(startup_worker, "NAND storage", 1536, NULL, tskIDLE_PRIORITY + 1, &worker) !=
        pdPASS)
        am_util_stdio_printf(
            "[NAND] filesystem used / total - free: unavailable: worker allocation failed\r\n");
}
