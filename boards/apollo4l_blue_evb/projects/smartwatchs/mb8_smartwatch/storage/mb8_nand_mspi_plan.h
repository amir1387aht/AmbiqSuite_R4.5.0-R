/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MB8_NAND_MSPI_PLAN_H
#define MB8_NAND_MSPI_PLAN_H
#include "ds35m1gb.h"

/* Complete transaction under one hardware CS assertion. Command/address always
 * single-lane. Dummy clocks are clocks, not bytes sent on quad data lines.
 */
struct mb8_nand_mspi_plan
{
    uint8_t  opcode, address_bytes, data_width, dummy_clocks;
    uint32_t address;
    bool     receive, dma;
};

int mb8_nand_mspi_plan(const uint8_t *,
    size_t,
    const uint8_t *,
    uint8_t *,
    size_t,
    unsigned,
    bool,
    struct mb8_nand_mspi_plan *);
/* Current E5 A1/1.8V chip ONLY. Never generalize a 133MHz claim to this part. */
int mb8_nand_mspi_clock_select(uint32_t hz, bool validated, uint32_t *divider);
#endif
