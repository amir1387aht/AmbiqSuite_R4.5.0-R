/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MB8_NAND_TRANSPORT_H
#define MB8_NAND_TRANSPORT_H
#ifndef WATCH_NAND_MSPI_QUAD
#define WATCH_NAND_MSPI_QUAD 1
#endif
#if WATCH_NAND_MSPI_QUAD
#include "mb8_nand_mspi.h"
#define mb8_nand_bus_open  mb8_nand_mspi_open
#define mb8_nand_bus_close mb8_nand_mspi_close
#else
#include "mb8_nand_iom.h"
#define mb8_nand_bus_open  mb8_nand_iom_open
#define mb8_nand_bus_close mb8_nand_iom_close
#endif
#endif
