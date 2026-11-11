/* Host-only YAFFS failure tests; no MCU or physical NAND access.
 * SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "yaffs_guts.c" /* Exercise the real internal GC path. */
#include "yaffs_bitmap.h"

static int last_error;
static unsigned erased;
void yaffsfs_Lock(void) {}
void yaffsfs_Unlock(void) {}
u32 yaffsfs_CurrentTime(void) { return 0; }
void yaffsfs_SetError(int err) { last_error = err; }
int yaffsfs_GetLastError(void) { return last_error; }
void *yaffsfs_malloc(size_t n) { return malloc(n); }
void yaffsfs_free(void *p) { free(p); }
void yaffsfs_get_malloc_values(unsigned *current, unsigned *high) { *current = *high = 0; }
int yaffsfs_CheckMemRegion(const void *p, size_t n, int wr) { (void)n; (void)wr; return p ? 0 : -1; }
void yaffsfs_OSInitialisation(void) {}
int yaffs_start_up(void) { return 0; }
void yaffs_bug_fn(const char *f, int line) { fprintf(stderr, "BUG %s:%d\n", f, line); abort(); }
unsigned int yaffs_trace_mask;
static int fail_read(struct yaffs_dev *dev, int page, u8 *data, struct yaffs_ext_tags *tags)
{ (void)dev; (void)page; (void)data; (void)tags; return YAFFS_FAIL; }
static int count_erase(struct yaffs_dev *dev, int block)
{ (void)dev; (void)block; erased++; return YAFFS_FAIL; }

static void test_failed_tag_read(void)
{
    struct yaffs_dev dev = {0};
    struct yaffs_ext_tags tags;
    memset(&tags, 0xa5, sizeof(tags));
    dev.tagger.read_chunk_tags_fn = fail_read;
    assert(yaffs_rd_chunk_tags_nand(&dev, 64, NULL, &tags) == YAFFS_FAIL);
    /* A failed tagger need not initialize its output. */
    assert(tags.obj_id == 0 && tags.chunk_used == 0);
}

static void test_failed_gc_read(void)
{
    struct yaffs_dev dev = {0};
    struct yaffs_block_info info = {0};
    u8 bits[1] = {1};
    u8 tmp[2048] = {0};
    dev.param.chunks_per_block = 8;
    dev.param.total_bytes_per_chunk = sizeof(tmp);
    dev.param.is_yaffs2 = 1;
    dev.internal_start_block = dev.internal_end_block = 1;
    dev.block_info = &info;
    dev.chunk_bits = bits;
    dev.chunk_bit_stride = 1;
    dev.gc_block = 1;
    dev.temp_buffer[0].buffer = tmp;
    dev.tagger.read_chunk_tags_fn = fail_read;
    dev.drv.drv_erase_fn = count_erase;
    info.block_state = YAFFS_BLOCK_STATE_FULL;
    info.pages_in_use = 1;
    info.has_shrink_hdr = 1;
    assert(yaffs_gc_block(&dev, 1, 1) == YAFFS_FAIL);
    assert(dev.gc_chunk == 0);
    assert(dev.gc_block == 1);
    assert(info.block_state == YAFFS_BLOCK_STATE_FULL);
    assert(info.pages_in_use == 1 && info.has_shrink_hdr);
    assert(bits[0] == 1 && dev.n_deletions == 0 && erased == 0);
}
/* Force the real write path to fail before a page is allocated. */
static void test_write_failure_keeps_cache(void)
{
    struct yaffs_dev dev = {0};
    struct yaffs_obj obj = {0};
    struct yaffs_cache cache = {0};
    u8 old_data[2048] = {0};
    u8 new_data[2048];
    memset(new_data, 0x5a, sizeof(new_data));
    dev.data_bytes_per_chunk = 2048;
    dev.param.total_bytes_per_chunk = 2048;
    dev.param.n_caches = 1;
    dev.param.cache_bypass_aligned = 1;
    dev.chunk_shift = 11;
    dev.chunk_div = 1;
    dev.chunk_mask = 2047;
    dev.gc_disable = 1;
    dev.cache_mgr.n_caches = 1;
    dev.cache_mgr.cache = &cache;
    obj.my_dev = &dev;
    obj.variant_type = YAFFS_OBJECT_TYPE_FILE;
    cache.object = &obj;
    cache.chunk_id = 1;
    cache.data = old_data;
    cache.dirty = 1;
    cache.n_bytes = 16;

    /* Missing tnode makes yaffs_wr_data_obj return 0, not a page ID. */
    assert(yaffs_do_file_wr(&obj, new_data, 0, 2048, 0) == 0);
    assert(cache.object == &obj && cache.dirty);
    assert(obj.variant.file_variant.file_size == 0);
    assert(obj.variant.file_variant.stored_size == 0);

    assert(yaffs_do_file_wr(&obj, new_data, 0, 4, 1) == 0);
    assert(cache.object == &obj && cache.dirty);
    assert(memcmp(cache.data, new_data, 4) == 0);
    assert(obj.variant.file_variant.file_size == 0);

    /* Buffered writes still succeed and stay dirty until flushed. */
    assert(yaffs_do_file_wr(&obj, new_data, 4, 4, 0) == 4);
    assert(cache.object == &obj && cache.dirty);
    assert(obj.variant.file_variant.file_size == 8);
}

int main(int argc, char **argv)
{
    if (argc == 1 || strcmp(argv[1], "tags") == 0) test_failed_tag_read();
    if (argc == 1 || strcmp(argv[1], "gc") == 0) test_failed_gc_read();
    if (argc == 1 || strcmp(argv[1], "write") == 0) test_write_failure_keeps_cache();
    puts("YAFFS fault tests passed");
    return 0;
}
