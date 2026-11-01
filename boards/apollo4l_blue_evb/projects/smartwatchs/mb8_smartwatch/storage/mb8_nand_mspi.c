/* SPDX-License-Identifier: GPL-2.0-only
 * Apollo4l dedicated MSPI NAND: serial controls + true1-1-4 quad DMA data.
 * No QPI/4-4-4, XIP, display sharing, guessed pin mux, or96MHz E5 A1 operation.
 */
#include "mb8_nand_mspi.h"
#include "mb8_nand_mspi_plan.h"
#include "mb8_nand_board.h"
#include "mb8_nand_diagnostics.h"
#include "am_mcu_apollo.h"
#include "am_util.h"
#include <string.h>

static void    *mspi;
static bool     poisoned, quad_enabled;
static unsigned current_width, current_address_bytes;
static uint32_t target_divider;
static uint32_t tcb[256] __attribute__((section(".shared.nand"), aligned(32)));
/* Bounded DMA bounce: caller may use TCM or an unaligned stack buffer. */
static uint32_t bounce[(DS35_PAGE + DS35_OOB + 3) / 4]
    __attribute__((section(".shared.nand"), aligned(32)));
static volatile bool     completed;
static volatile uint32_t completion_status;

static void abort_controller(void)
{
    /* HAL disable refuses pending entries. Stop the engines and reset the
     * active transfer exactly as the vendor HAL's error path does, then gate
     * power. Retain static buffers/handle allocation and require firmware reset.
     * No late DMA callback is allowed to write into reused memory.
     */
    if (mspi && !poisoned)
        mb8_nand_diag_registers(MSPIn(mb8_nand_board.mspi)->INTSTAT,
            MSPIn(mb8_nand_board.mspi)->CQSTAT,
            MSPIn(mb8_nand_board.mspi)->DMASTAT,
            MSPIn(mb8_nand_board.mspi)->DEV0CFG,
            MSPIn(mb8_nand_board.mspi)->CTRL1);
    poisoned     = true;
    quad_enabled = false;
    if (!mspi)
        return;
    am_hal_mspi_interrupt_disable(mspi, AM_HAL_MSPI_INT_ALL);
    MSPIn(mb8_nand_board.mspi)->CQCFG_b.CQEN     = 0;
    MSPIn(mb8_nand_board.mspi)->DMACFG_b.DMAEN   = 0;
    MSPIn(mb8_nand_board.mspi)->MSPICFG_b.IPRSTN = 0;
    __DSB();
    am_hal_pwrctrl_periph_disable(
        (am_hal_pwrctrl_periph_e)(AM_HAL_PWRCTRL_PERIPH_MSPI0 + mb8_nand_board.mspi));
    mspi = NULL;
}

void mb8_nand_mspi_close(void)
{
    if (!mspi || poisoned)
        return;
    uint32_t status = am_hal_mspi_disable(mspi);
    if (mb8_nand_diag_hal(status, "controller-close-disable") < 0)
    {
        abort_controller();
        return;
    }
    status = am_hal_mspi_interrupt_disable(mspi, AM_HAL_MSPI_INT_ALL);
    if (mb8_nand_diag_hal(status, "controller-close-interrupt-disable") < 0)
    {
        abort_controller();
        return;
    }
    status = am_hal_mspi_power_control(mspi, AM_HAL_SYSCTRL_DEEPSLEEP, false);
    if (mb8_nand_diag_hal(status, "controller-close-power") < 0)
    {
        /* Disable succeeded: no transfer is live. Power state is ambiguous;
         * do not access peripheral registers or release the HAL allocation. */
        poisoned     = true;
        quad_enabled = false;
        am_hal_pwrctrl_periph_disable(
            (am_hal_pwrctrl_periph_e)(AM_HAL_PWRCTRL_PERIPH_MSPI0 + mb8_nand_board.mspi));
        return;
    }
    status = am_hal_mspi_deinitialize(mspi);
    if (mb8_nand_diag_hal(status, "controller-close-deinitialize") < 0)
    {
        /* Already powered down; retain the handle and require firmware reset. */
        poisoned     = true;
        quad_enabled = false;
        return;
    }
    mspi                  = NULL;
    quad_enabled          = false;
    current_width         = 0;
    current_address_bytes = 0;
}

static int configure_phase(const struct mb8_nand_mspi_plan *p)
{
    uint32_t status = AM_HAL_STATUS_SUCCESS;
    if (current_width != p->data_width)
    {
        am_hal_mspi_device_e mode;
        if (p->data_width == 4)
            mode = mb8_nand_board.cs_channel ? AM_HAL_MSPI_FLASH_QUAD_CE1_1_1_4
                                             : AM_HAL_MSPI_FLASH_QUAD_CE0_1_1_4;
        else
            mode = mb8_nand_board.cs_channel ? AM_HAL_MSPI_FLASH_SERIAL_CE1
                                             : AM_HAL_MSPI_FLASH_SERIAL_CE0;
        /* DEVICE_CONFIG changes SEPIO, DMA mixed width and output-pad mask;
         * PIOMIXED alone does NOT restore separate serial SI/SO for controls.
         * Calls happen only after the previous CS transaction has completed.
         */
        status = am_hal_mspi_control(mspi, AM_HAL_MSPI_REQ_DEVICE_CONFIG, &mode);
        mb8_nand_diag_hal(status, "phase-device-config");
        if (!status)
        {
            status = am_hal_mspi_control(mspi, AM_HAL_MSPI_REQ_PIOMIXED_CONFIG, &mode);
            mb8_nand_diag_hal(status, "phase-PIO-mixed-config");
        }
        if (!status)
            current_width = p->data_width;
    }
    unsigned address_bytes = p->address_bytes ? p->address_bytes : 1;
    if (!status && current_address_bytes != address_bytes)
    {
        am_hal_mspi_instr_addr_t length = {.eInstrCfg = AM_HAL_MSPI_INSTR_1_BYTE,
            .eAddrCfg                                 = (am_hal_mspi_addr_e)(address_bytes - 1)};
        status = am_hal_mspi_control(mspi, AM_HAL_MSPI_REQ_SET_INSTR_ADDR_LEN, &length);
        mb8_nand_diag_hal(status, "phase-instruction-address-length");
        if (!status)
            current_address_bytes = address_bytes;
    }
    if (status)
    {
        abort_controller();
        return DS35_IO;
    }
    return DS35_OK;
}

static void done(void *ctx, uint32_t status)
{
    (void)ctx;
    completion_status = status;
    completed         = true;
}

static int transfer(void *ctx,
    const uint8_t        *header,
    size_t                hn,
    const uint8_t        *tx,
    uint8_t              *rx,
    size_t                len,
    unsigned              width)
{
    (void)ctx;
    if (poisoned || !mspi)
        return DS35_NOT_READY;
    struct mb8_nand_mspi_plan p;
    int r = mb8_nand_mspi_plan(header, hn, tx, rx, len, width, quad_enabled, &p);
    if (r < 0)
        return r;
    mb8_nand_diag_wire(p.opcode, p.address, (uint32_t)len, width);
    if ((r = configure_phase(&p)) < 0)
        return r;
    if (tx)
        memcpy(bounce, tx, len);
    uint32_t status;
    if (!p.dma)
    {
        /* Small serial controls, including8 serial dummy clocks in READ ID.
         * Never emit these on four lanes merely because QE is enabled.
         */
        am_hal_mspi_pio_transfer_t t = {.ui32NumBytes = len,
            .eDirection                               = p.receive ? AM_HAL_MSPI_RX : AM_HAL_MSPI_TX,
            .bSendAddr                                = p.address_bytes != 0,
            .ui32DeviceAddr                           = p.address,
            .bSendInstr                               = true,
            .ui16DeviceInstr                          = p.opcode,
            .pui32Buffer                              = bounce};
        status                       = am_hal_mspi_blocking_transfer(mspi, &t, 2000);
    }
    else
    {
        /* DMA uses persistent6B/32 instructions from device configuration;
         *2-byte address stays x1, read turnaround=8 CLOCKS, write latency=0.
         * Boundary/time breaks are disabled, preserving one CS for main+spare.
         */
        am_hal_mspi_dma_transfer_t t = {.ui8Priority = 0,
            .eDirection                              = p.receive ? AM_HAL_MSPI_RX : AM_HAL_MSPI_TX,
            .ui32TransferCount                       = len,
            .ui32DeviceAddress                       = p.address,
            .ui32SRAMAddress                         = (uint32_t)(uintptr_t)bounce};
        completed                    = false;
        completion_status            = AM_HAL_STATUS_SUCCESS;
        am_hal_daxi_control(AM_HAL_DAXI_CONTROL_FLUSH, 0);
        __DMB();
        status = am_hal_mspi_nonblocking_transfer(mspi, &t, AM_HAL_MSPI_TRANS_DMA, done, NULL);
        mb8_nand_diag_hal(status, "DMA-enqueue");
        for (unsigned waited = 0; !status && !completed && waited < 10000; waited++)
        {
            uint32_t irq = 0;
            status       = am_hal_mspi_interrupt_status_get(mspi, &irq, false);
            mb8_nand_diag_hal(status, "DMA-IRQ-status");
            if (!status)
                mb8_nand_diag_irq(irq);
            if (!status && irq)
            {
                status = am_hal_mspi_interrupt_clear(mspi, irq);
                mb8_nand_diag_hal(status, "DMA-IRQ-clear");
                if (!status)
                {
                    status = am_hal_mspi_interrupt_service(mspi, irq);
                    mb8_nand_diag_hal(status, "DMA-IRQ-service");
                }
                if (irq & AM_HAL_MSPI_INT_ERR)
                {
                    status = AM_HAL_STATUS_FAIL;
                    mb8_nand_diag_hal(status, "DMA-IRQ-error-bits");
                }
            }
            if (!completed)
                am_util_delay_us(1);
        }
        if (!status && !completed)
        {
            status = AM_HAL_STATUS_TIMEOUT;
            mb8_nand_diag_hal(status, "DMA-poll-timeout");
        }
        if (!status && completion_status)
        {
            status = completion_status;
            mb8_nand_diag_hal(status, "DMA-callback-status");
        }
        /* HAL callback invalidates DAXI after DMA before marking completion. */
        __DMB();
    }
    if (status)
    {
        mb8_nand_diag_hal(status, p.dma ? "quad-DMA-transfer" : "serial-PIO-transfer");
        r = status == AM_HAL_STATUS_TIMEOUT ? DS35_TIMEOUT : DS35_IO;
        abort_controller();
        return r;
    }
    if (rx)
        memcpy(rx, bounce, len);
    am_util_delay_us(1); /* Part tCS>=100ns, conservative margin. */
    return DS35_OK;
}

static void delay(void *ctx, uint32_t us)
{
    (void)ctx;
    am_util_delay_us(us);
}

static int quad_ready(void *ctx, bool enabled)
{
    (void)ctx;
    if (poisoned || !mspi)
        return DS35_NOT_READY;
    /* Enable only after exact ID/main-array/QE/ECC readback. Disable selects
     * the initial12MHz clock BEFORE restoring B0, also when feature setup
     * failed but serial transport remains healthy. It sends no NAND command.
     */
    am_hal_mspi_clock_e clock =
        (am_hal_mspi_clock_e)(enabled ? target_divider : AM_HAL_MSPI_CLK_12MHZ);
    uint32_t status = am_hal_mspi_control(mspi, AM_HAL_MSPI_REQ_CLOCK_CONFIG, &clock);
    if (mb8_nand_diag_hal(status, enabled ? "clock-up-after-QE" : "shutdown-clock-down") < 0)
    {
        abort_controller();
        return DS35_IO;
    }
    mb8_nand_diag_active_clock(enabled ? mb8_nand_board.clock_hz : 12000000u);
    quad_enabled = enabled;
    return DS35_OK;
}

static bool reserved_pin(uint32_t pin)
{
    const uint32_t reserved[] = MB8_NAND_RESERVED_PINS;
    for (size_t i = 0; i < sizeof(reserved) / sizeof(reserved[0]); i++)
        if (pin == reserved[i])
            return true;
    return false;
}

static const char *const signals[] = {"IO0", "IO1", "IO2", "IO3", "SCK", "CS"};

static int validate_pins(const uint32_t *pins, const uint32_t *functions)
{
    if (mb8_nand_board.mspi >= AM_REG_MSPI_NUM_MODULES)
        return mb8_nand_diag_fail(
            DS35_NOT_READY, "controller-guard", "MSPI instance outside SDK range");
    if (mb8_nand_board.mspi == 1)
        return mb8_nand_diag_fail(DS35_NOT_READY, "controller-guard", "MSPI1 reserved for display");
    if (mb8_nand_board.cs_channel > 1)
        return mb8_nand_diag_fail(DS35_NOT_READY, "ce-guard", "hardware CE must be0 or1");
    if (mb8_nand_board.mspi_clkond4 > 1)
        return mb8_nand_diag_fail(DS35_NOT_READY, "sck-guard", "CLKOND4 must be0 or1");
    am_hal_pin_function_e base =
        mb8_nand_board.mspi == 0 ? AM_HAL_PIN_FN_MSPI0_0 : AM_HAL_PIN_FN_MSPI2_0;
    for (unsigned i = 0; i < 6; i++)
    {
        mb8_nand_diag_pin(signals[i], pins[i], functions[i], false, 0, 0);
        if (pins[i] >= AM_HAL_PIN_TOTAL_GPIOS || functions[i] >= AM_HAL_PIN_NUMFUNCS)
            return mb8_nand_diag_fail(
                DS35_NOT_READY, "pin-range", "GPIO or function selector outside SDK table");
        if (reserved_pin(pins[i]))
            return mb8_nand_diag_fail(
                DS35_NOT_READY, "pin-reserved", "display/power/reset GPIO forbidden");
        for (unsigned j = 0; j < i; j++)
            if (pins[i] == pins[j])
                return mb8_nand_diag_fail(
                    DS35_EINVAL, "pin-duplicate", "two NAND signals share one GPIO");
        uint16_t wanted;
        if (i < 4)
            wanted = base + i;
        else if (i == 4)
            wanted = base + (mb8_nand_board.mspi_clkond4 ? 4 : 8);
        else
            wanted = AM_HAL_PIN_FN_NCE0 + pins[i];
        uint16_t actual = am_hal_pin_fn_list[pins[i]][functions[i]];
        if (actual != wanted)
        {
            mb8_nand_diag_pin(signals[i], pins[i], functions[i], true, wanted, actual);
            return mb8_nand_diag_fail(
                DS35_NOT_READY, "pin-matrix", "selector does not route expected MSPI lane/NCE");
        }
    }
    return DS35_OK;
}

int mb8_nand_mspi_open(struct ds35_bus *bus)
{
    if (poisoned)
        return DS35_NOT_READY; /* Preserve first failure until firmware reset. */
    mb8_nand_diag_begin("MSPI 1-1-4");
    if (!bus || mspi)
        return mb8_nand_diag_fail(
            DS35_EINVAL, "handle-guard", "NULL output bus or controller already open");
    if (mb8_nand_board.profile != MB8_NAND_PROFILE_STOCK_MB8)
        return mb8_nand_diag_fail(
            DS35_NOT_READY, "board-profile", "unsupported board profile; expected stock MiBand8");
    uint32_t hz = mb8_nand_board.clock_hz;
    int      r  = mb8_nand_mspi_clock_select(hz, &target_divider);
    if (r < 0)
        return mb8_nand_diag_fail(
            r, "clock-request", "clock unsupported by MCU or above DS35M1GB83MHz limit");
    mb8_nand_diag_clock(hz);
    const uint32_t pins[]      = {mb8_nand_board.mosi_pin,
        mb8_nand_board.miso_pin,
        mb8_nand_board.io2_pin,
        mb8_nand_board.io3_pin,
        mb8_nand_board.sck_pin,
        mb8_nand_board.cs_pin};
    const uint32_t functions[] = {mb8_nand_board.mosi_func,
        mb8_nand_board.miso_func,
        mb8_nand_board.io2_func,
        mb8_nand_board.io3_func,
        mb8_nand_board.sck_func,
        mb8_nand_board.cs_func};
    if ((r = validate_pins(pins, functions)) < 0)
        return r;
    mb8_nand_diag_pin(NULL, 0, 0, false, 0, 0);
    mb8_nand_diag_hardware();
    uint32_t status = am_hal_mspi_initialize(mb8_nand_board.mspi, &mspi);
    mb8_nand_diag_hal(status, "controller-initialize");
    if (!status)
    {
        status = am_hal_mspi_power_control(mspi, AM_HAL_SYSCTRL_WAKE, false);
        mb8_nand_diag_hal(status, "controller-power-wake");
    }
    am_hal_mspi_config_t cfg = {.ui32TCBSize = sizeof(tcb) / sizeof(tcb[0]),
        .pTCB                                = tcb,
        .bClkonD4                            = mb8_nand_board.mspi_clkond4 != 0};
    if (!status)
    {
        status = am_hal_mspi_configure(mspi, &cfg);
        mb8_nand_diag_hal(status, "controller-configure");
    }
    am_hal_mspi_dev_config_t device = {.ui8TurnAround = 8,
        .eAddrCfg                                     = AM_HAL_MSPI_ADDR_2_BYTE,
        .eInstrCfg                                    = AM_HAL_MSPI_INSTR_1_BYTE,
        .ui16ReadInstr                                = 0x6b,
        .ui16WriteInstr                               = 0x32,
        .eDeviceConfig =
            mb8_nand_board.cs_channel ? AM_HAL_MSPI_FLASH_SERIAL_CE1 : AM_HAL_MSPI_FLASH_SERIAL_CE0,
        .eSpiMode         = AM_HAL_MSPI_SPI_MODE_0,
        .eClockFreq       = AM_HAL_MSPI_CLK_12MHZ,
        .bSendAddr        = true,
        .bSendInstr       = true,
        .bTurnaround      = true,
        .ui8WriteLatency  = 0,
        .bEnWriteLatency  = false,
        .bEmulateDDR      = false,
        .ui16DMATimeLimit = 0,
        .eDMABoundary     = AM_HAL_MSPI_BOUNDARY_NONE};
    if (!status)
    {
        status = am_hal_mspi_device_configure(mspi, &device);
        mb8_nand_diag_hal(status, "device-configure");
    }
    if (!status)
    {
        status = am_hal_mspi_enable(mspi);
        mb8_nand_diag_hal(status, "controller-enable");
    }
    for (unsigned i = 0; i < 6 && !status; i++)
    {
        am_hal_gpio_pincfg_t pin    = {0};
        pin.GP.cfg_b.uFuncSel       = functions[i];
        pin.GP.cfg_b.eDriveStrength = AM_HAL_GPIO_PIN_DRIVESTRENGTH_0P5X;
        if (i == 5)
        {
            _Static_assert(AM_HAL_GPIO_NCE_MSPI2CEN1 - AM_HAL_GPIO_NCE_MSPI0CEN0 == 5,
                "MSPI NCE encoding changed");
            pin.GP.cfg_b.uNCE =
                AM_HAL_GPIO_NCE_MSPI0CEN0 + mb8_nand_board.mspi * 2 + mb8_nand_board.cs_channel;
            pin.GP.cfg_b.eGPOutCfg = AM_HAL_GPIO_PIN_OUTCFG_PUSHPULL;
            pin.GP.cfg_b.eCEpol    = AM_HAL_GPIO_PIN_CEPOL_ACTIVELOW;
        }
        status = am_hal_gpio_pinconfig(pins[i], pin);
        if (status)
        {
            mb8_nand_diag_pin(signals[i], pins[i], functions[i], false, 0, 0);
            mb8_nand_diag_hal(status, "pad-configure");
        }
    }
    if (status)
    {
        mb8_nand_mspi_close();
        return DS35_IO;
    }
    mb8_nand_diag_active_clock(12000000u);
    current_width         = 1;
    current_address_bytes = 2;
    quad_enabled          = false;
    am_util_delay_us(5000);
    *bus = (struct ds35_bus){
        .transfer = transfer, .delay_us = delay, .data_width = 4, .quad_ready = quad_ready};
    return DS35_OK;
}
