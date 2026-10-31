/* SPDX-License-Identifier: GPL-2.0-only
 * All physical NAND choices live here. Nothing is initialized by watch startup.
 * UNKNOWN is not a pin assignment. Keep confirmations zero until measured.
 */
#ifndef MB8_NAND_BOARD_H
#define MB8_NAND_BOARD_H
#include <stdint.h>
#define MB8_NAND_UNKNOWN                 UINT32_MAX
#define MB8_NAND_PINOUT_CONFIRMED        0
#define MB8_NAND_1V8_CONFIRMED           0
#define MB8_NAND_PART_GEOMETRY_CONFIRMED 0
#define MB8_NAND_PARTITION_CONFIRMED     0
#define MB8_NAND_WRITES_AUTHORIZED       0
/* A dedicated IOM is required: no display, touch, or other bus sharing. */
#define MB8_NAND_IOM        MB8_NAND_UNKNOWN
#define MB8_NAND_CS_CHANNEL MB8_NAND_UNKNOWN
#define MB8_NAND_SCK_PIN    MB8_NAND_UNKNOWN
#define MB8_NAND_SCK_FUNC   MB8_NAND_UNKNOWN
#define MB8_NAND_MOSI_PIN   MB8_NAND_UNKNOWN
#define MB8_NAND_MOSI_FUNC  MB8_NAND_UNKNOWN
#define MB8_NAND_MISO_PIN   MB8_NAND_UNKNOWN
#define MB8_NAND_MISO_FUNC  MB8_NAND_UNKNOWN
#define MB8_NAND_CS_PIN     MB8_NAND_UNKNOWN
#define MB8_NAND_CS_FUNC    MB8_NAND_UNKNOWN
/* WP#/HOLD# must be verified tied high externally for the x1 backend. */
#define MB8_NAND_WP_HOLD_HIGH_CONFIRMED 0
/* 12MHz bring-up; validated dedicated IOM may use 48MHz, never >48MHz here.
 * DS35M1GB limit is 83MHz. 96MHz MSPI would be out of specification.
 */
#define MB8_NAND_CLOCK_HZ 12000000u
/* Eraseblock indices; deliberately no guessed partition or whole-chip default. */
#define MB8_NAND_FIRST_BLOCK MB8_NAND_UNKNOWN
#define MB8_NAND_BLOCK_COUNT 0u
#define MB8_NAND_CACHE_PAGES 4u
/* Separate bounded allocation budget; does not alter the UI allocator. */
#define MB8_NAND_HEAP_LIMIT  (96u * 1024u)
#define MB8_NAND_MAX_OBJECTS 256

struct mb8_nand_board_config
{
    uint32_t pinout_confirmed;
    uint32_t voltage_confirmed;
    uint32_t part_geometry_confirmed;
    uint32_t partition_confirmed;
    uint32_t writes_authorized;
    uint32_t iom;
    uint32_t cs_channel;
    uint32_t sck_pin;
    uint32_t sck_func;
    uint32_t mosi_pin;
    uint32_t mosi_func;
    uint32_t miso_pin;
    uint32_t miso_func;
    uint32_t cs_pin;
    uint32_t cs_func;
    uint32_t wp_hold_high_confirmed;
    uint32_t clock_hz;
    uint32_t first_block;
    uint32_t block_count;
};
extern const struct mb8_nand_board_config mb8_nand_board;

#endif
