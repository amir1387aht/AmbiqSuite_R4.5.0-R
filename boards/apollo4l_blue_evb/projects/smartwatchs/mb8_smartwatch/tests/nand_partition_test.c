/* SPDX-License-Identifier: GPL-2.0-only
 * Host-only partition-map and DS35 range-guard regression tests. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "mb8_nand_partitions.h"
#include "ds35m1gb.h"

#define Y MB8_PARTITION_YAFFS2_OOB4
#define R MB8_PARTITION_RESERVED
static unsigned bus_calls;
static int fail_transfer(void *ctx, const uint8_t *h, size_t hn,
    const uint8_t *tx, uint8_t *rx, size_t len, unsigned width)
{
    (void)ctx; (void)h; (void)hn; (void)tx; (void)rx; (void)len; (void)width;
    bus_calls++;
    return DS35_IO;
}
static void reset_nand(struct ds35_nand *n, bool writable)
{
    *n = (struct ds35_nand){0};
    n->ready = true;
    n->writable = writable;
    n->first_block = 10;
    n->block_count = 16;
    n->bus.transfer = fail_transfer;
    n->bus.data_width = 4;
    bus_calls = 0;
}
static void test_maps(void)
{
    struct mb8_partition_range out = {0};
    struct mb8_partition map[] = { {"storage", 10, 16, Y}, {"reserved", 26, 2, R}, {"end", 1008, 16, Y} };
    assert(mb8_partition_resolve(map, 3, 0, 1024, &out) == MB8_PARTITION_OK);
    assert(out.first_block == 10 && out.last_block == 25 && out.block_count == 16);
    assert(mb8_partition_resolve(map, 3, 2, 1024, &out) == MB8_PARTITION_OK);
    assert(out.first_block == 1008 && out.last_block == 1023);
    assert(mb8_partition_resolve(map, 3, 1, 1024, &out) == MB8_PARTITION_NOT_YAFFS);
    assert(out.first_block == 0 && out.block_count == 0 && out.last_block == 0);
    map[1].first_block = 25;
    assert(mb8_partition_resolve(map, 3, 0, 1024, &out) == MB8_PARTITION_OVERLAP);
    map[1].first_block = 0; map[1].block_count = 11;
    assert(mb8_partition_resolve(map, 3, 0, 1024, &out) == MB8_PARTITION_OVERLAP);
    map[1].first_block = 26; map[1].block_count = 2; map[1].name = "storage";
    assert(mb8_partition_resolve(map, 3, 0, 1024, &out) == MB8_PARTITION_DUPLICATE);
    map[1].name = "reserved"; map[2].block_count = 17;
    assert(mb8_partition_resolve(map, 3, 0, 1024, &out) == MB8_PARTITION_INVALID);
    map[2].first_block = UINT32_MAX; map[2].block_count = UINT32_MAX;
    assert(mb8_partition_resolve(map, 3, 0, 1024, &out) == MB8_PARTITION_INVALID);
    map[2].first_block = 1008; map[2].block_count = 16; map[0].block_count = 15;
    assert(mb8_partition_resolve(map, 3, 0, 1024, &out) == MB8_PARTITION_INVALID);
    map[0].block_count = 16; map[0].kind = 99;
    assert(mb8_partition_resolve(map, 3, 0, 1024, &out) == MB8_PARTITION_INVALID);
    assert(mb8_partition_resolve(map, 3, 3, 1024, &out) == MB8_PARTITION_INVALID);
    assert(mb8_partition_resolve(map, MB8_PARTITION_MAX + 1, 0, 1024, &out) == MB8_PARTITION_INVALID);
    assert(mb8_partition_resolve(NULL, 0, 0, 1024, &out) == MB8_PARTITION_INVALID);
    assert(mb8_partition_resolve(map, 3, 0, 1024, NULL) == MB8_PARTITION_INVALID);
#if WATCH_NAND_DEBUG_REPRO
    assert(mb8_partition_active(&out) == MB8_PARTITION_OK);
    assert(out.first_block == 0 && out.last_block == 1023);
#else
    assert(mb8_partition_active(&out) == MB8_PARTITION_INVALID);
#endif
}
static uint32_t rng_state = 0x8612badu;
static uint32_t next_random(void)
{
    rng_state = rng_state * 1664525u + 1013904223u;
    return rng_state;
}
static void test_overlap_properties(void)
{
    /* Compare against a simple occupancy bitmap, including touching edges. */
    for (unsigned iter = 0; iter < 10000; iter++)
    {
        struct mb8_partition_range out;
        struct mb8_partition map[2] = {{"fs", 0, 0, Y}, {"reserved", 0, 0, R}};
        unsigned char used[1024] = {0};
        map[0].first_block = next_random() % 1009u;
        map[0].block_count = 16u + next_random() % (1025u - map[0].first_block - 16u);
        map[1].first_block = next_random() % 1024u;
        map[1].block_count = 1u + next_random() % (1024u - map[1].first_block);
        for (uint32_t i = map[0].first_block; i < map[0].first_block + map[0].block_count; i++)
            used[i] = 1;
        int overlaps = 0;
        for (uint32_t i = map[1].first_block; i < map[1].first_block + map[1].block_count; i++)
            overlaps |= used[i];
        int r = mb8_partition_resolve(map, 2, 0, 1024, &out);
        assert(r == (overlaps ? MB8_PARTITION_OVERLAP : MB8_PARTITION_OK));
    }
}
static void test_driver_boundaries(void)
{
    struct ds35_nand n;
    uint8_t data[DS35_PAGE] = {0}, tags[1] = {0};
    const uint32_t first_page = 10u * DS35_PAGES_PER_BLOCK;
    const uint32_t end_page = 26u * DS35_PAGES_PER_BLOCK;
    reset_nand(&n, true);
    assert(ds35_read(&n, first_page - 1, data, 1, NULL, 0) == DS35_EINVAL);
    assert(ds35_read(&n, end_page, data, 1, NULL, 0) == DS35_EINVAL);
    assert(ds35_program(&n, first_page - 1, data, sizeof(data), tags, sizeof(tags)) == DS35_EINVAL);
    assert(ds35_program(&n, end_page, data, sizeof(data), tags, sizeof(tags)) == DS35_EINVAL);
    assert(ds35_erase(&n, 9) == DS35_EINVAL && ds35_erase(&n, 26) == DS35_EINVAL);
    assert(ds35_check_bad(&n, 9) == DS35_EINVAL && ds35_check_bad(&n, 26) == DS35_EINVAL);
    assert(ds35_mark_bad(&n, 9) == DS35_EINVAL && ds35_mark_bad(&n, 26) == DS35_EINVAL);
    assert(bus_calls == 0);
    reset_nand(&n, false);
    assert(ds35_program(&n, first_page, data, sizeof(data), tags, sizeof(tags)) == DS35_READ_ONLY);
    assert(ds35_erase(&n, 25) == DS35_READ_ONLY && bus_calls == 0);
    reset_nand(&n, false);
    assert(ds35_read(&n, first_page, data, 1, NULL, 0) == DS35_IO && bus_calls == 1);
    reset_nand(&n, false);
    assert(ds35_read(&n, end_page - 1, data, 1, NULL, 0) == DS35_IO && bus_calls == 1);
}
int main(void)
{
    test_maps();
    test_overlap_properties();
    test_driver_boundaries();
    puts("Partition map and NAND boundary tests passed");
    return 0;
}
