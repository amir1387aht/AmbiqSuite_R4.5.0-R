/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MB8_NAND_MSPI_H
#define MB8_NAND_MSPI_H
#include "ds35m1gb.h"
int  mb8_nand_mspi_open(struct ds35_bus *);
void mb8_nand_mspi_close(void);
#endif
