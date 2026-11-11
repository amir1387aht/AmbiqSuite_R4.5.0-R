/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MB8_NAND_PARTITIONS_H
#define MB8_NAND_PARTITIONS_H

#include <stddef.h>
#include <stdint.h>

/* Compile-time physical eraseblock map, never an on-flash partition table. */
#define MB8_PARTITION_MAX 16u
#define MB8_PARTITION_MIN_YAFFS_BLOCKS 16u

enum mb8_partition_kind
{
    MB8_PARTITION_RESERVED = 1,
    MB8_PARTITION_YAFFS2_OOB4 = 2
};

struct mb8_partition
{
    const char *name;
    uint32_t first_block, block_count;
    enum mb8_partition_kind kind;
};

struct mb8_partition_range
{
    uint32_t first_block, block_count, last_block; /* last_block is inclusive */
};

enum mb8_partition_result
{
    MB8_PARTITION_OK = 0,
    MB8_PARTITION_INVALID = -1,
    MB8_PARTITION_OVERLAP = -2,
    MB8_PARTITION_DUPLICATE = -3,
    MB8_PARTITION_NOT_YAFFS = -4
};

/* Validate the ENTIRE map before resolving the one active volume. Reject
 * duplicate names, overlap, invalid types, too-small YAFFS ranges and overflow.
 * No hardware is touched. On failure, output is cleared. */
int mb8_partition_resolve(const struct mb8_partition *map, size_t count,
    size_t active, uint32_t total_blocks, struct mb8_partition_range *out);

/* Select the compile-time map in mb8_nand_partitions.c. This only defines
 * bounds; the separate partition/layout/write authorization gates still apply. */
int mb8_partition_active(struct mb8_partition_range *out);
#endif
