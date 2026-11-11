/* SPDX-License-Identifier: GPL-2.0-only */
#include "mb8_nand_partitions.h"
#include "mb8_nand_board.h"
#include "ds35m1gb.h"
#include <string.h>

int mb8_partition_resolve(const struct mb8_partition *map, size_t count,
    size_t active, uint32_t total_blocks, struct mb8_partition_range *out)
{
    if (!out)
        return MB8_PARTITION_INVALID;
    *out = (struct mb8_partition_range){0};
    if (!map || !count || count > MB8_PARTITION_MAX || active >= count || !total_blocks)
        return MB8_PARTITION_INVALID;
    for (size_t i = 0; i < count; i++)
    {
        const struct mb8_partition *p = &map[i];
        if (!p->name || !p->name[0] || p->first_block >= total_blocks ||
            !p->block_count || p->block_count > total_blocks - p->first_block ||
            (p->kind != MB8_PARTITION_RESERVED && p->kind != MB8_PARTITION_YAFFS2_OOB4) ||
            (p->kind == MB8_PARTITION_YAFFS2_OOB4 &&
                p->block_count < MB8_PARTITION_MIN_YAFFS_BLOCKS))
            return MB8_PARTITION_INVALID;
        for (size_t j = 0; j < i; j++)
        {
            const struct mb8_partition *q = &map[j];
            if (!strcmp(p->name, q->name))
                return MB8_PARTITION_DUPLICATE;
            /* Both end additions are safe after their individual bounds checks. */
            if (p->first_block < q->first_block + q->block_count &&
                q->first_block < p->first_block + p->block_count)
                return MB8_PARTITION_OVERLAP;
        }
    }
    if (map[active].kind != MB8_PARTITION_YAFFS2_OOB4)
        return MB8_PARTITION_NOT_YAFFS;
    out->first_block = map[active].first_block;
    out->block_count = map[active].block_count;
    out->last_block = out->first_block + out->block_count - 1u;
    return MB8_PARTITION_OK;
}

/* All entries describe PHYSICAL eraseblocks, not bytes or YAFFS internal block
 * numbers. Add verified occupied/reserved ranges here before selecting a volume.
 * Do not infer an empty area or stock compatibility from a successful scan.
 * Defaults intentionally remain unconfigured (except the existing debug profile).
 * Multiple entries are checked, but this adapter mounts only the selected entry. */
static const struct mb8_partition layout[] = {
    { "nand", MB8_NAND_FIRST_BLOCK, MB8_NAND_BLOCK_COUNT, MB8_PARTITION_YAFFS2_OOB4 },
    /* Example only, NOT enabled: { "reserved", first, count, MB8_PARTITION_RESERVED }, */
};
#define MB8_ACTIVE_PARTITION 0u

int mb8_partition_active(struct mb8_partition_range *out)
{
    return mb8_partition_resolve(layout, sizeof(layout) / sizeof(layout[0]),
        MB8_ACTIVE_PARTITION, DS35_BLOCKS, out);
}
