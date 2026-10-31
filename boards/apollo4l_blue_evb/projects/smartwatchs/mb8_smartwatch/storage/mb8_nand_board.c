/* SPDX-License-Identifier: GPL-2.0-only */
#include "mb8_nand_board.h"
const struct mb8_nand_board_config mb8_nand_board = {
    .pinout_confirmed        = MB8_NAND_PINOUT_CONFIRMED,
    .voltage_confirmed       = MB8_NAND_1V8_CONFIRMED,
    .part_geometry_confirmed = MB8_NAND_PART_GEOMETRY_CONFIRMED,
    .partition_confirmed     = MB8_NAND_PARTITION_CONFIRMED,
    .writes_authorized       = MB8_NAND_WRITES_AUTHORIZED,
    .iom                     = MB8_NAND_IOM,
    .cs_channel              = MB8_NAND_CS_CHANNEL,
    .sck_pin                 = MB8_NAND_SCK_PIN,
    .sck_func                = MB8_NAND_SCK_FUNC,
    .mosi_pin                = MB8_NAND_MOSI_PIN,
    .mosi_func               = MB8_NAND_MOSI_FUNC,
    .miso_pin                = MB8_NAND_MISO_PIN,
    .miso_func               = MB8_NAND_MISO_FUNC,
    .cs_pin                  = MB8_NAND_CS_PIN,
    .cs_func                 = MB8_NAND_CS_FUNC,
    .wp_hold_high_confirmed  = MB8_NAND_WP_HOLD_HIGH_CONFIRMED,
    .clock_hz                = MB8_NAND_CLOCK_HZ,
    .first_block             = MB8_NAND_FIRST_BLOCK,
    .block_count             = MB8_NAND_BLOCK_COUNT,
};
