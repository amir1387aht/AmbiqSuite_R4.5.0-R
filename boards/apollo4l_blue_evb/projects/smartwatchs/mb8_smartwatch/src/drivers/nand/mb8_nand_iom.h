/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MB8_NAND_IOM_H
#define MB8_NAND_IOM_H
#include "ds35m1gb.h"
int  mb8_nand_iom_open(struct ds35_bus *);
void mb8_nand_iom_close(void);
#endif
