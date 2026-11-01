/* SPDX-License-Identifier: GPL-2.0-only */
#include "mb8_nand_diagnostics.h"
#include "ds35m1gb.h"
#include <string.h>
static struct mb8_nand_diagnostic diagnostic;

const struct mb8_nand_diagnostic *mb8_nand_bus_diagnostic(void)
{
    return &diagnostic;
}

void mb8_nand_diag_begin(const char *backend)
{
    memset(&diagnostic, 0, sizeof(diagnostic));
    diagnostic.backend   = backend;
    diagnostic.stage     = "preflight";
    diagnostic.reason    = "bus open has no recorded failure";
    diagnostic.attempted = true;
}

void mb8_nand_diag_hardware(void)
{
    diagnostic.hardware_attempted = true;
}

void mb8_nand_diag_clock(uint32_t hz)
{
    diagnostic.selected_hz = hz;
}

void mb8_nand_diag_active_clock(uint32_t hz)
{
    diagnostic.active_hz = hz;
}

void mb8_nand_diag_probe_started(void)
{
    diagnostic.probe_started = true;
}

void mb8_nand_diag_wire(uint32_t opcode, uint32_t address, uint32_t bytes, uint32_t width)
{
    if (diagnostic.result < 0)
        return;
    diagnostic.irq_last     = 0;
    diagnostic.wire_valid   = true;
    diagnostic.wire_opcode  = opcode;
    diagnostic.wire_address = address;
    diagnostic.wire_bytes   = bytes;
    diagnostic.wire_width   = width;
}

void mb8_nand_diag_irq(uint32_t irq)
{
    if (diagnostic.result >= 0)
        diagnostic.irq_last = irq;
}

void mb8_nand_diag_registers(uint32_t irq, uint32_t cq, uint32_t dma, uint32_t dev, uint32_t ctrl)
{
    if (diagnostic.registers_valid)
        return;
    diagnostic.registers_valid = true;
    diagnostic.irq_raw         = irq;
    diagnostic.cqstat          = cq;
    diagnostic.dmastat         = dma;
    diagnostic.dev0cfg         = dev;
    diagnostic.ctrl1           = ctrl;
}

int mb8_nand_diag_fail(int result, const char *stage, const char *reason)
{
    if (diagnostic.result >= 0)
    {
        diagnostic.result = result;
        diagnostic.stage  = stage;
        diagnostic.reason = reason;
    }
    return result;
}

int mb8_nand_diag_hal(uint32_t status, const char *stage)
{
    if (!status)
        return DS35_OK;
    if (diagnostic.result >= 0)
    {
        diagnostic.hal_status = status;
        diagnostic.hal_valid  = true;
        mb8_nand_diag_fail(DS35_IO, stage, "HAL call failed; first status retained across cleanup");
    }
    return DS35_IO;
}

void mb8_nand_diag_pin(const char *signal,
    uint32_t                       pin,
    uint32_t                       selector,
    bool                           valid,
    uint32_t                       expected,
    uint32_t                       actual)
{
    if (diagnostic.result < 0)
        return;
    diagnostic.signal       = signal;
    diagnostic.pin          = pin;
    diagnostic.selector     = selector;
    diagnostic.pin_valid    = signal != NULL;
    diagnostic.matrix_valid = valid;
    diagnostic.expected_fn  = expected;
    diagnostic.actual_fn    = actual;
}
