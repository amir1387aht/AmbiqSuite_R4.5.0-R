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
/* Dedicated controller only: MSPI1/display sharing is forbidden.
 * Existing MOSI/MISO names mean bidirectional quad IO0/IO1 in MSPI mode. */
#define MB8_NAND_MSPI     MB8_NAND_UNKNOWN
#define MB8_NAND_IO2_PIN  MB8_NAND_UNKNOWN
#define MB8_NAND_IO2_FUNC MB8_NAND_UNKNOWN
#define MB8_NAND_IO3_PIN  MB8_NAND_UNKNOWN
#define MB8_NAND_IO3_FUNC MB8_NAND_UNKNOWN
/* Quad lanes2/3 need verified pull-ups, NOT hard ties to VCC. */
#define MB8_NAND_QSPI_IO23_PULLUPS_CONFIRMED 0
#define MB8_NAND_QSPI_MAX_CLOCK_VALIDATED    0
/* Standard MSPI SCK=lane8; optional alternate SCK=lane4, board verified only. */
#define MB8_NAND_MSPI_CLKOND4 0
/* All known display/power/reset pads are forbidden to this dedicated backend. */
#define MB8_NAND_RESERVED_PINS {11, 37, 38, 39, 40, 41, 42, 43, 44, 45, 86}
#define MB8_NAND_IOM           MB8_NAND_UNKNOWN
#define MB8_NAND_CS_CHANNEL    MB8_NAND_UNKNOWN
#define MB8_NAND_SCK_PIN       MB8_NAND_UNKNOWN
#define MB8_NAND_SCK_FUNC      MB8_NAND_UNKNOWN
#define MB8_NAND_MOSI_PIN      MB8_NAND_UNKNOWN
#define MB8_NAND_MOSI_FUNC     MB8_NAND_UNKNOWN
#define MB8_NAND_MISO_PIN      MB8_NAND_UNKNOWN
#define MB8_NAND_MISO_FUNC     MB8_NAND_UNKNOWN
#define MB8_NAND_CS_PIN        MB8_NAND_UNKNOWN
#define MB8_NAND_CS_FUNC       MB8_NAND_UNKNOWN
/* WP#/HOLD# must be verified tied high externally for the x1 backend. */
#define MB8_NAND_WP_HOLD_HIGH_CONFIRMED 0
/* 12MHz bring-up; verified dedicated MSPI/IOM may use48MHz, never >48MHz.
 * QSPI_MAX_CLOCK_VALIDATED selects48MHz after strict ID/QE verification.
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
    uint32_t mspi;
    uint32_t io2_pin, io2_func, io3_pin, io3_func;
    uint32_t qspi_io23_pullups_confirmed, qspi_max_clock_validated, mspi_clkond4;
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
