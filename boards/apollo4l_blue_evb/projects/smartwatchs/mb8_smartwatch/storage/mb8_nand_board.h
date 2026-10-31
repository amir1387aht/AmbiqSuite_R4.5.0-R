/* SPDX-License-Identifier: GPL-2.0-only
 * All physical NAND choices live here. Nothing is initialized by watch startup.
 * Published ATC pin mapping is not on-watch electrical validation.
 * Keep confirmation flags zero until the actual board is checked.
 */
#ifndef MB8_NAND_BOARD_H
#define MB8_NAND_BOARD_H
#include <stdint.h>
#define MB8_NAND_UNKNOWN                 UINT32_MAX
#define MB8_NAND_PINOUT_CONFIRMED        0
#define MB8_NAND_1V8_CONFIRMED           0
#define MB8_NAND_PART_GEOMETRY_CONFIRMED 0
#define MB8_NAND_PARTITION_CONFIRMED     0
/* Confirm an independently provisioned YAFFS2 image with THIS tag layout.
 * A successful scan of blank/stock NAND does not establish YAFFS identity. */
#define MB8_NAND_YAFFS_PROVISIONED_CONFIRMED 0
#define MB8_NAND_WRITES_AUTHORIZED           0
/* Source: atc1441/ATCmiBand8fw, commit
 * 931216a15337fa371f3a7d2de6dfb216a90093a4, Pinout_MiBand8.ods,
 * Tabelle1 rows59..62,67,70. Upstream labels these Flash MOSI/MISO/WP/Hold,
 * CLK/CS; it does not specify board revision, voltage, part or partition.
 * SDK am_hal_pin_fn_list verifies these as dedicated MSPI2 lanes0..3/8.
 * Existing MOSI/MISO names mean bidirectional quad IO0/IO1 in MSPI mode.
 * MSPI1/display sharing remains forbidden. */
#define MB8_NAND_MSPI     2u
#define MB8_NAND_IO2_PIN  76u
#define MB8_NAND_IO2_FUNC 0u
#define MB8_NAND_IO3_PIN  77u
#define MB8_NAND_IO3_FUNC 0u
/* Quad lanes2/3 need verified pull-ups, NOT hard ties to VCC. */
#define MB8_NAND_QSPI_IO23_PULLUPS_CONFIRMED 0
#define MB8_NAND_QSPI_MAX_CLOCK_VALIDATED    0
/* Standard MSPI SCK=lane8; optional alternate SCK=lane4, board verified only. */
#define MB8_NAND_MSPI_CLKOND4 0
/* All known display/power/reset pads are forbidden to this dedicated backend. */
#define MB8_NAND_RESERVED_PINS {11, 37, 38, 39, 40, 41, 42, 43, 44, 45, 86}
#define MB8_NAND_IOM           MB8_NAND_UNKNOWN
/* CE0 is a software choice: pad85 selector7=NCE85 routes NCESRC=36
 * (MSPI2CEN0), active low. Upstream identifies the pad, not a CE channel.
 * SDK also permits CEN1=37; no display-controller sharing is required. */
#define MB8_NAND_CS_CHANNEL 0u
#define MB8_NAND_SCK_PIN    82u
#define MB8_NAND_SCK_FUNC   0u
#define MB8_NAND_MOSI_PIN   74u
#define MB8_NAND_MOSI_FUNC  0u
#define MB8_NAND_MISO_PIN   75u
#define MB8_NAND_MISO_FUNC  0u
#define MB8_NAND_CS_PIN     85u
#define MB8_NAND_CS_FUNC    7u
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
    uint32_t yaffs_provisioned_confirmed;
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
