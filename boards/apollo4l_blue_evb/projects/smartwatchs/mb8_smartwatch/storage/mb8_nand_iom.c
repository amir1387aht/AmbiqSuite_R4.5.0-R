/* SPDX-License-Identifier: GPL-2.0-only
 * Dedicated Apollo4l IOM x1 SPI transport. Bulk DMA, polled command-queue
 * completion, no new ISR vector and no dependency on display/MSPI allocation.
 * Pins and 1.8V compatibility MUST be measured before enabling this file.
 */
#include "mb8_nand_iom.h"
#include "mb8_nand_board.h"
#include "am_mcu_apollo.h"
#include "am_util.h"
#include <string.h>

static void    *iom;
static bool     poisoned;
static uint32_t cq[256] __attribute__((section(".shared.nand"), aligned(32)));
static uint32_t bounce[(DS35_PAGE + DS35_OOB + 3) / 4]
    __attribute__((section(".shared.nand"), aligned(32)));
static volatile bool     completed;
static volatile uint32_t completion_status;

void mb8_nand_iom_close(void)
{
    if (iom)
    {
        am_hal_iom_interrupt_disable(iom, AM_HAL_IOM_INT_ALL);
        if (poisoned)
        {
            /* HAL disable refuses outstanding CQ transactions. On a timeout,
             * explicitly stop CQ/DMA/submodule BEFORE power gating; never free
             * or reuse these static DMA buffers or HAL handle after a fault.
             * Firmware reset is required to reopen a poisoned controller.
             */
            IOMn(mb8_nand_board.iom)->CQCFG_b.CQEN   = 0;
            IOMn(mb8_nand_board.iom)->DMACFG_b.DMAEN = 0;
            IOMn(mb8_nand_board.iom)->SUBMODCTRL     = 0;
            __DSB();
            am_hal_pwrctrl_periph_disable(
                (am_hal_pwrctrl_periph_e)(AM_HAL_PWRCTRL_PERIPH_IOM0 + mb8_nand_board.iom));
            iom = NULL;
            return;
        }
        am_hal_iom_disable(iom);
        am_hal_iom_power_ctrl(iom, AM_HAL_SYSCTRL_DEEPSLEEP, false);
        am_hal_iom_uninitialize(iom);
        iom = NULL;
    }
}

static void done(void *ctx, uint32_t status)
{
    (void)ctx;
    completion_status = status;
    completed         = true;
}

static int xfer(
    void *ctx, const uint8_t *header, size_t hn, const uint8_t *tx, uint8_t *rx, size_t len)
{
    (void)ctx;
    if (!iom || poisoned)
        return DS35_NOT_READY;
    if (!header || !hn || hn > 5 || len > sizeof(bounce) || (tx && rx) || (len && !tx && !rx))
        return DS35_EINVAL;
    am_hal_iom_transfer_t t       = {0};
    t.uPeerInfo.ui32SpiChipSelect = mb8_nand_board.cs_channel;
    t.ui32InstrLen                = hn;
    /* IOM transmits multi-byte offsets most-significant byte first, as used
     * by SDK am_devices_spipsram's 24-bit address transfers. */
    for (size_t i = 0; i < hn; i++)
        t.ui64Instr = (t.ui64Instr << 8) | header[i];
    t.ui32NumBytes  = len;
    t.eDirection    = rx ? AM_HAL_IOM_RX : AM_HAL_IOM_TX;
    t.pui32TxBuffer = bounce;
    t.pui32RxBuffer = bounce;
    if (tx)
        memcpy(bounce, tx, len);
    completed         = false;
    completion_status = AM_HAL_STATUS_SUCCESS;
    am_hal_daxi_control(AM_HAL_DAXI_CONTROL_FLUSH, 0);
    __DMB();
    uint32_t status = am_hal_iom_nonblocking_transfer(iom, &t, done, NULL);
    /* Even short commands use CQ, avoiding the HAL's long PIO timeout. */
    for (uint32_t elapsed = 0; status == AM_HAL_STATUS_SUCCESS && !completed && elapsed < 10000;
        elapsed++)
    {
        uint32_t irq = 0;
        status       = am_hal_iom_interrupt_status_get(iom, false, &irq);
        if (status)
            break;
        if (irq)
        {
            status = am_hal_iom_interrupt_clear(iom, irq);
            if (!status)
                status = am_hal_iom_interrupt_service(iom, irq);
        }
        if (!completed)
            am_util_delay_us(1);
    }
    if (status || !completed || completion_status)
    {
        int r    = completed ? DS35_IO : DS35_TIMEOUT;
        poisoned = true;
        mb8_nand_iom_close();
        return r;
    }
    __DMB();
    if (rx)
        memcpy(rx, bounce, len);
    /* Datasheet tCS minimum100ns. Conservative 1us between commands. */
    am_util_delay_us(1);
    return DS35_OK;
}

static void delay(void *ctx, uint32_t us)
{
    (void)ctx;
    am_util_delay_us(us);
}

int mb8_nand_iom_open(struct ds35_bus *bus)
{
    if (poisoned)
        return DS35_NOT_READY;
    if (!bus || iom)
        return DS35_EINVAL;
    if (!mb8_nand_board.pinout_confirmed || !mb8_nand_board.voltage_confirmed ||
        !mb8_nand_board.wp_hold_high_confirmed || mb8_nand_board.iom >= 8 ||
        mb8_nand_board.cs_channel >= 4 || !mb8_nand_board.clock_hz ||
        mb8_nand_board.clock_hz > 48000000u)
        return DS35_NOT_READY;
    const uint32_t pins[]  = {mb8_nand_board.sck_pin,
        mb8_nand_board.mosi_pin,
        mb8_nand_board.miso_pin,
        mb8_nand_board.cs_pin};
    const uint32_t funcs[] = {mb8_nand_board.sck_func,
        mb8_nand_board.mosi_func,
        mb8_nand_board.miso_func,
        mb8_nand_board.cs_func};
    for (unsigned i = 0; i < 4; i++)
    {
        if (pins[i] >= 128 || funcs[i] >= 8)
            return DS35_NOT_READY;
        for (unsigned j = 0; j < i; j++)
            if (pins[i] == pins[j])
                return DS35_EINVAL;
    }
    poisoned        = false;
    uint32_t status = am_hal_iom_initialize(mb8_nand_board.iom, &iom);
    if (!status)
        status = am_hal_iom_power_ctrl(iom, AM_HAL_SYSCTRL_WAKE, false);
    am_hal_iom_config_t cfg = {.eInterfaceMode = AM_HAL_IOM_SPI_MODE,
        .ui32ClockFreq                         = mb8_nand_board.clock_hz,
        .eSpiMode                              = AM_HAL_IOM_SPI_MODE_0,
        .pNBTxnBuf                             = cq,
        .ui32NBTxnBufLength                    = sizeof(cq) / sizeof(cq[0])};
    if (!status)
        status = am_hal_iom_configure(iom, &cfg);
    /* Board owner must verify selected pad functions and voltage domains.
     * Preserve other pad fields at safe zero defaults; use no internal pulls.
     */
    for (unsigned i = 0; i < 4 && !status; i++)
    {
        am_hal_gpio_pincfg_t pin = {0};
        pin.GP.cfg_b.uFuncSel    = funcs[i];
        pin.GP.cfg_b.eDriveStrength =
            i == 2 ? AM_HAL_GPIO_PIN_DRIVESTRENGTH_0P1X : AM_HAL_GPIO_PIN_DRIVESTRENGTH_0P5X;
        if (i == 3)
        {
            _Static_assert(AM_HAL_GPIO_NCE_IOM7CE3 - AM_HAL_GPIO_NCE_IOM0CE0 == 31,
                "IOM NCE encoding changed");
            pin.GP.cfg_b.uNCE =
                AM_HAL_GPIO_NCE_IOM0CE0 + mb8_nand_board.iom * 4 + mb8_nand_board.cs_channel;
            pin.GP.cfg_b.eGPOutCfg = AM_HAL_GPIO_PIN_OUTCFG_PUSHPULL;
            pin.GP.cfg_b.eCEpol    = AM_HAL_GPIO_PIN_CEPOL_ACTIVELOW;
        }
        status = am_hal_gpio_pinconfig(pins[i], pin);
    }
    if (!status)
        status = am_hal_iom_enable(iom);
    if (status)
    {
        mb8_nand_iom_close();
        return DS35_IO;
    }
    am_util_delay_us(5000); /* VCC must already be stable; tPU >=5ms. */
    *bus = (struct ds35_bus){.transfer = xfer, .delay_us = delay};
    return DS35_OK;
}
