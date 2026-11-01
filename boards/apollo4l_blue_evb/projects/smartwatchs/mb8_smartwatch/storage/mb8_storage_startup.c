/* SPDX-License-Identifier: GPL-2.0-only
 * Permanent startup worker/report. Temporary diagnostics are a separate unit.
 */
#include "mb8_storage.h"
#include "mb8_nand_board.h"
#include "mb8_nand_transport.h"
#include "ds35m1gb.h"
#include "FreeRTOS.h"
#include "task.h"
#include "mb8_storage_log.h"
#include "watch_gpu.h"
#if WATCH_NAND_STARTUP_TESTS
#include "mb8_storage_startup_tests.h"
#endif

static TaskHandle_t worker;

static void print_size(const char *label, uint64_t bytes)
{
    mb8_storage_log("[NAND] %s=%llu bytes (%llu KiB, %llu.%03llu MiB)\r\n",
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
        mb8_storage_log("[NAND] ID=E5 A1; raw capacity from E5 A1 profile geometry\r\n");
        if (u.status_valid)
            mb8_storage_log("[NAND] sampled status=0x%02x (not a live health guarantee)\r\n",
                (unsigned)u.status_register);
        else
            mb8_storage_log("[NAND] status unavailable: status read did not complete\r\n");
        print_size("raw chip MAIN (not filesystem free space)", u.raw_main_bytes);
    }
    else
        mb8_storage_log("[NAND] raw capacity unavailable: ID not successfully probed\r\n");
    if (r < 0)
    {
        mb8_storage_log(
            "[NAND] filesystem used / total - free: unavailable: %s\r\n", mb8_storage_status());
        return;
    }
    mb8_storage_log("[NAND] YAFFS /nand %s used_or_unavailable / total - free: "
                    "%llu / %llu - %llu bytes\r\n",
        u.read_only ? "RO" : "RW",
        (unsigned long long)u.used_or_unavailable_bytes,
        (unsigned long long)u.total_bytes,
        (unsigned long long)u.free_bytes);
    print_size("used_or_unavailable", u.used_or_unavailable_bytes);
    print_size("YAFFS API total", u.total_bytes);
    print_size("YAFFS allocatable free", u.free_bytes);
    print_size("selected partition MAIN", u.partition_main_bytes);
    mb8_storage_log(
        "[NAND] partition dead blocks=%u GC-reserved=%u checkpoint-present=%u; "
        "total excludes GC reserve; used includes metadata/bad/checkpoint/other unavailable space, "
        "NOT sum of file lengths.\r\n",
        (unsigned)u.dead_blocks,
        (unsigned)u.reserved_blocks,
        (unsigned)u.checkpoint_blocks);
}

static void report_bus_failure(void)
{
    const struct mb8_nand_diagnostic *d = mb8_nand_bus_diagnostic();
    if (!d->attempted || d->result >= 0)
        return;
    mb8_storage_log("[NAND bus] backend=%s stage=%s DS35=%d reason=%s\r\n",
        d->backend,
        d->stage,
        d->result,
        d->reason);
    if (d->hal_valid)
        mb8_storage_log("[NAND bus] first HAL status=%u (0x%08x), retained before cleanup\r\n",
            (unsigned)d->hal_status,
            (unsigned)d->hal_status);
    else
        mb8_storage_log("[NAND bus] HAL status=N/A (no failed HAL call recorded)\r\n");
    mb8_storage_log("[NAND bus] JEDEC probe %s; %s\r\n",
        d->probe_started ? "started (runtime/shutdown failure possible)" : "not started",
        d->hardware_attempted
            ? "controller/pad initialization attempted; cleanup attempted"
            : "this open rejected in preflight; no hardware touched by this attempt");
    mb8_storage_log("[NAND cfg] MSPI=%u IOM=%u CE=%u requested=%uHz selected_target=%uHz "
                    "initial_config=%uHz profile=%u CLKOND4=%u WP_HOLD_high=%u (IOM-only)\r\n",
        (unsigned)mb8_nand_board.mspi,
        (unsigned)mb8_nand_board.iom,
        (unsigned)mb8_nand_board.cs_channel,
        (unsigned)mb8_nand_board.clock_hz,
        (unsigned)d->selected_hz,
        (unsigned)(WATCH_NAND_MSPI_QUAD ? 12000000u : mb8_nand_board.clock_hz),
        (unsigned)mb8_nand_board.profile,
        (unsigned)mb8_nand_board.mspi_clkond4,
        (unsigned)mb8_nand_board.wp_hold_high_confirmed);
    mb8_storage_log(
        "[NAND cfg] GPIO/function: IO0=%u/%u IO1=%u/%u IO2=%u/%u IO3=%u/%u SCK=%u/%u CS=%u/%u\r\n",
        (unsigned)mb8_nand_board.mosi_pin,
        (unsigned)mb8_nand_board.mosi_func,
        (unsigned)mb8_nand_board.miso_pin,
        (unsigned)mb8_nand_board.miso_func,
        (unsigned)mb8_nand_board.io2_pin,
        (unsigned)mb8_nand_board.io2_func,
        (unsigned)mb8_nand_board.io3_pin,
        (unsigned)mb8_nand_board.io3_func,
        (unsigned)mb8_nand_board.sck_pin,
        (unsigned)mb8_nand_board.sck_func,
        (unsigned)mb8_nand_board.cs_pin,
        (unsigned)mb8_nand_board.cs_func);
    mb8_storage_log(
        "[NAND bus] last successful HAL clock=%uHz (not measured)\r\n", (unsigned)d->active_hz);
    if (d->pin_valid)
    {
        mb8_storage_log("[NAND bus] failing signal=%s GPIO=%u selector=%u\r\n",
            d->signal,
            (unsigned)d->pin,
            (unsigned)d->selector);
        if (d->matrix_valid)
            mb8_storage_log(
                "[NAND bus] SDK function enum expected=%u actual=%u; expected MSPI%u signal=%s (SCK lane8 or4, CS=NCE)\r\n",
                (unsigned)d->expected_fn,
                (unsigned)d->actual_fn,
                (unsigned)mb8_nand_board.mspi,
                d->signal);
    }
}

static void report_shutdown_failure(void)
{
    const struct ds35_shutdown_diagnostic *d = mb8_storage_shutdown_diagnostic();
    if (!d->attempted || d->result >= 0)
        return;
    mb8_storage_log(
        "[NAND restore] phase=%s DS35=%d; reset required; no replay\r\n", d->phase, d->result);
    if (d->expected_valid)
        mb8_storage_log(
            "[NAND restore] B0 saved=0x%02x expected=0x%02x; FULL-byte compare; defined-mask=0xd1\r\n",
            (unsigned)d->saved,
            (unsigned)d->expected);
    else
        mb8_storage_log("[NAND restore] B0 saved/expected unavailable\r\n");
    if (d->observed_valid)
        mb8_storage_log("[NAND restore] B0 observed=0x%02x xor=0x%02x defined-bit-diff=0x%02x\r\n",
            (unsigned)d->observed,
            (unsigned)(d->observed ^ d->expected),
            (unsigned)((d->observed ^ d->expected) & 0xd1u));
    else
        mb8_storage_log("[NAND restore] B0 observed unavailable: GET did not complete\r\n");
}

#if WATCH_NAND_STARTUP_TESTS
void mb8_storage_report_diagnostics(void)
{
    const struct ds35_operation_diagnostic *o = mb8_storage_operation_diagnostic();
    if (o->phase)
    {
        mb8_storage_log("[NAND core] phase=%s physical-page=%u primary=%d\r\n",
            o->phase,
            (unsigned)o->page,
            o->result);
        if (o->expected_valid)
        {
            mb8_storage_log(
                "[NAND core] B0 expected=0x%02x comparison-mask=0x%02x (BBM uses full byte)\r\n",
                (unsigned)o->expected,
                (unsigned)o->compare_mask);
            if (o->observed_valid)
                mb8_storage_log("[NAND core] B0 observed=0x%02x xor=0x%02x\r\n",
                    (unsigned)o->observed,
                    (unsigned)(o->expected ^ o->observed));
            else
                mb8_storage_log("[NAND core] B0 observed unavailable (GET not completed)\r\n");
        }
        if (!o->expected_valid && o->observed_valid)
            mb8_storage_log("[NAND core] B0 observed=0x%02x (mode check, no expected value)\r\n",
                (unsigned)o->observed);
        if (o->status_valid)
            mb8_storage_log("[NAND core] last completed status=0x%02x\r\n", (unsigned)o->status);
    }
    report_bus_failure();
    const struct mb8_nand_diagnostic *b = mb8_nand_bus_diagnostic();
    if (b->result >= 0)
        mb8_storage_log(
            "[NAND bus] no failed HAL call recorded; inspect RAW/core protocol or readback evidence\r\n");
    if (b->wire_valid)
        mb8_storage_log(
            "[NAND wire] opcode=%02x addr=%u bytes=%u data-lanes=%u clock=%uHz (configured)\r\n",
            (unsigned)b->wire_opcode,
            (unsigned)b->wire_address,
            (unsigned)b->wire_bytes,
            (unsigned)b->wire_width,
            (unsigned)b->active_hz);
    if (b->registers_valid)
        mb8_storage_log(
            "[NAND regs] BEFORE abort: IRQlast=%08x IRQraw=%08x CQSTAT=%08x DMASTAT=%08x DEV0CFG=%08x CTRL1=%08x\r\n",
            (unsigned)b->irq_last,
            (unsigned)b->irq_raw,
            (unsigned)b->cqstat,
            (unsigned)b->dmastat,
            (unsigned)b->dev0cfg,
            (unsigned)b->ctrl1);
    report_shutdown_failure();
}
#endif

static void startup_worker(void *unused)
{
    (void)unused;
    while (!watch_gpu_ready)
        vTaskDelay(pdMS_TO_TICKS(10));
    mb8_storage_log("[NAND] test-write request=%u (default0); actual writes also require "
                    "confirmed provisioned partition + authorization\r\n",
        (unsigned)WATCH_NAND_TEST_WRITE);
    mb8_storage_log("[NAND] stock MiBand8 initial phase: RO; volatile reset/QE/ECC setup only; "
                    "no protection unlock, program, erase or format.\r\n");
    mb8_storage_log("[NAND] clock: probe=12000000Hz target=%uHz after E5 A1 + QE/ECC; "
                    "engineering selection, NOT recovered Xiaomi operating clock\r\n",
        (unsigned)mb8_nand_board.clock_hz);
    if (mb8_storage_probe_only() < 0)
    {
        mb8_storage_log("[NAND] probe SKIP/FAIL: %s\r\n", mb8_storage_status());
        report_bus_failure();
        report_shutdown_failure();
        report();
#if WATCH_NAND_STARTUP_TESTS
        mb8_storage_log(
            "[NAND test] RO checks SKIP: bus/probe/status/restoration failed; no further reads\r\n");
#endif
    }
    else
    {
#if WATCH_NAND_STARTUP_TESTS
        if (!mb8_storage_startup_raw_tests())
        {
            report();
            goto startup_done;
        }
#endif
        if (mb8_storage_mount(true) < 0)
        {
            mb8_storage_log("[NAND] RO mount SKIP/FAIL: %s\r\n", mb8_storage_status());
            report_bus_failure();
            report_shutdown_failure();
            report();
#if WATCH_NAND_STARTUP_TESTS
            mb8_storage_log(
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
                mb8_storage_log("[NAND] RO unmount FAIL: %s\r\n", mb8_storage_status());
#if WATCH_NAND_STARTUP_TESTS && WATCH_NAND_TEST_WRITE
            if (closed == 0 && mb8_nand_board.writes_authorized &&
                mb8_nand_board.yaffs_provisioned_confirmed)
            {
                if (mb8_storage_mount(false) < 0)
                    mb8_storage_log("[NAND test] write SKIP/FAIL: %s\r\n", mb8_storage_status());
                else
                {
                    mb8_storage_startup_write_test();
                    report();
                    if (mb8_storage_unmount() < 0)
                        mb8_storage_log(
                            "[NAND test] RW unmount FAIL: %s\r\n", mb8_storage_status());
                }
            }
            else
                mb8_storage_log(
                    "[NAND test] write SKIP: authorization absent or RO close failed\r\n");
#endif
        }
    }
#if WATCH_NAND_STARTUP_TESTS
startup_done:; /* C11 label precedes a statement even when write-test logging is off. */
#endif
#if WATCH_NAND_STARTUP_TESTS && !WATCH_NAND_TEST_WRITE
    mb8_storage_log("[NAND test] write/delete SKIP: WATCH_NAND_TEST_WRITE=0\r\n");
#endif
    unsigned lost = mb8_storage_log_dropped();
    if (lost)
        mb8_storage_log(
            "[NAND] diagnostic log lines dropped=%u (format/length/UART failure)\r\n", lost);
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
        mb8_storage_log(
            "[NAND] filesystem used / total - free: unavailable: worker allocation failed\r\n");
}
