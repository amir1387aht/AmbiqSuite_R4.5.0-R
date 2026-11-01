/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MB8_NAND_DIAGNOSTICS_H
#define MB8_NAND_DIAGNOSTICS_H
#include <stdbool.h>
#include <stdint.h>

/* Single storage worker owns this snapshot. No logging in HAL/IRQ callbacks. */
struct mb8_nand_diagnostic
{
    const char *backend, *stage, *reason, *signal;
    int         result;
    uint32_t    hal_status, pin, selector, expected_fn, actual_fn, selected_hz, active_hz;
    bool        attempted, hardware_attempted, hal_valid, pin_valid, matrix_valid, probe_started;
};
const struct mb8_nand_diagnostic *mb8_nand_bus_diagnostic(void);
void                              mb8_nand_diag_begin(const char *backend);
void                              mb8_nand_diag_hardware(void);
void                              mb8_nand_diag_clock(uint32_t hz);
void                              mb8_nand_diag_active_clock(uint32_t hz);
void                              mb8_nand_diag_probe_started(void);
int  mb8_nand_diag_fail(int result, const char *stage, const char *reason);
int  mb8_nand_diag_hal(uint32_t status, const char *stage);
void mb8_nand_diag_pin(const char *signal,
    uint32_t                       pin,
    uint32_t                       selector,
    bool                           matrix_valid,
    uint32_t                       expected,
    uint32_t                       actual);
#endif
