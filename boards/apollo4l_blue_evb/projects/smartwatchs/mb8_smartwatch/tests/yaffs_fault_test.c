/* Host-only YAFFS failure tests; no MCU or physical NAND access.
 * SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "yaffs_guts.c" /* Exercise the real internal GC path. */
#include "yaffs_bitmap.h"
#include "yaffsfs.h"
#include "yaffs_checkptrw.h"

static int last_error;
static unsigned erased;
static int checkpoint_result, checkpoint_calls, deinit_calls;
int __wrap_yaffs_checkpoint_save(struct yaffs_dev *dev)
{ (void)dev; checkpoint_calls++; return checkpoint_result; }
void __wrap_yaffs_deinitialise(struct yaffs_dev *dev)
{ deinit_calls++; dev->is_mounted = 0; }
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

/* Drive the actual direct API with a synthetic mounted device. Linker wrappers
 * inject checkpoint outcomes and prevent device teardown; no NAND is touched. */
static void test_checkpoint_propagation(void)
{
    struct yaffs_dev dev = {0};
    dev.is_mounted = 1;
    dev.param.is_yaffs2 = 1;
    dev.internal_start_block = 1;
    dev.internal_end_block = 100;
    checkpoint_calls = deinit_calls = last_error = 0;
    checkpoint_result = YAFFS_FAIL;
    assert(yaffs_sync_reldev(&dev) == -1 && last_error == -EIO);
    assert(checkpoint_calls == 1 && dev.is_mounted);
    last_error = 0;
    assert(yaffs_unmount2_reldev(&dev, 0) == -1 && last_error == -EIO);
    assert(dev.is_mounted && deinit_calls == 0);
    assert(yaffs_unmount2_reldev(&dev, 1) == -1 && dev.is_mounted && deinit_calls == 0);
    assert(yaffs_sync_files_reldev(&dev) == 0); /* no checkpoint requested */
    last_error = 0;
    assert(yaffs_remount_reldev(&dev, 0, 1) == -1 && last_error == -EIO);
    assert(!dev.read_only && dev.is_mounted);

    checkpoint_result = YAFFS_OK;
    assert(yaffs_sync_reldev(&dev) == 0);
    assert(yaffs_remount_reldev(&dev, 0, 1) == 0 && dev.read_only);
    checkpoint_calls = 0;
    assert(yaffs_unmount2_reldev(&dev, 0) == 0);
    assert(checkpoint_calls == 0 && deinit_calls == 1);

    /* Skipped or inapplicable checkpoints are not errors and do not write. */
    dev.is_mounted = 1; dev.read_only = 0; dev.param.skip_checkpt_wr = 1;
    checkpoint_calls = 0; checkpoint_result = YAFFS_FAIL;
    assert(yaffs_sync_reldev(&dev) == 0 && checkpoint_calls == 0);
    assert(yaffs_unmount2_reldev(&dev, 0) == 0 && checkpoint_calls == 0);
    dev.is_mounted = 1; dev.param.skip_checkpt_wr = 0;
    dev.internal_end_block = 10; /* less than YAFFS_CHECKPOINT_MIN_BLOCKS */
    assert(yaffs_sync_reldev(&dev) == 0 && checkpoint_calls == 0);
}

static int checkpoint_programs;
static int fail_checkpoint_program(struct yaffs_dev *dev, int page, const u8 *data,
                                   const struct yaffs_ext_tags *tags)
{ (void)dev; (void)page; (void)data; (void)tags; checkpoint_programs++; return YAFFS_FAIL; }

static int ok_checkpoint_program(struct yaffs_dev *dev, int page, const u8 *data,
                                 const struct yaffs_ext_tags *tags)
{ (void)dev; (void)page; (void)data; (void)tags; checkpoint_programs++; return YAFFS_OK; }

static void test_checkpoint_program_failure(void)
{
    struct yaffs_dev dev = {0};
    struct yaffs_block_info block = {0};
    u8 buffer[64] = {0}, byte = 42;
    dev.internal_start_block = dev.internal_end_block = 1;
    dev.param.chunks_per_block = 64;
    dev.data_bytes_per_chunk = sizeof(buffer);
    dev.block_info = &block;
    block.block_state = YAFFS_BLOCK_STATE_EMPTY;
    dev.n_erased_blocks = 1;
    dev.n_free_chunks = 64;
    dev.checkpt_next_block = 1;
    dev.checkpt_cur_block = -1;
    dev.checkpt_buffer = buffer;
    dev.checkpt_open_write = 1;
    dev.checkpt_byte_offs = 63;
    dev.tagger.write_chunk_tags_fn = fail_checkpoint_program;
    checkpoint_programs = 0;
    /* Filling the last byte must not hide an unsuccessful page program. */
    assert(yaffs2_checkpt_wr(&dev, &byte, 1) < 0);
    assert(checkpoint_programs == 1);
    assert(yaffs_checkpt_close(&dev) == 0 && checkpoint_programs == 1);

    memset(&dev, 0, sizeof(dev)); memset(&block, 0, sizeof(block));
    dev.internal_start_block = dev.internal_end_block = 1;
    dev.param.chunks_per_block = 64; dev.data_bytes_per_chunk = sizeof(buffer);
    dev.block_info = &block; block.block_state = YAFFS_BLOCK_STATE_EMPTY;
    dev.n_erased_blocks = 1; dev.n_free_chunks = 64;
    dev.checkpt_next_block = 1; dev.checkpt_cur_block = -1;
    dev.checkpt_buffer = buffer; dev.checkpt_open_write = 1;
    dev.checkpt_byte_offs = 17; /* final partially filled page */
    dev.tagger.write_chunk_tags_fn = fail_checkpoint_program;
    checkpoint_programs = 0;
    assert(yaffs_checkpt_close(&dev) == 0 && checkpoint_programs == 1);

    memset(&dev, 0, sizeof(dev)); memset(&block, 0, sizeof(block));
    dev.internal_start_block = dev.internal_end_block = 1;
    dev.param.chunks_per_block = 64; dev.data_bytes_per_chunk = sizeof(buffer);
    dev.block_info = &block; block.block_state = YAFFS_BLOCK_STATE_EMPTY;
    dev.n_erased_blocks = 1; dev.n_free_chunks = 64;
    dev.checkpt_next_block = 1; dev.checkpt_cur_block = -1;
    dev.checkpt_buffer = buffer; dev.checkpt_open_write = 1;
    dev.checkpt_byte_offs = 63; dev.tagger.write_chunk_tags_fn = ok_checkpoint_program;
    checkpoint_programs = 0;
    assert(yaffs2_checkpt_wr(&dev, &byte, 1) == 1 && checkpoint_programs == 1);
    assert(yaffs_checkpt_close(&dev) == 1 && checkpoint_programs == 1);
}

static int read_mode, read_calls, query_calls;
static int mock_checkpoint_read(struct yaffs_dev *dev, int page, u8 *data,
                                struct yaffs_ext_tags *tags)
{
    (void)dev; (void)page; (void)data;
    read_calls++;
    /* Even plausible output must be discarded when the callback reports failure. */
    memset(tags, 0, sizeof(*tags));
    tags->seq_number = read_mode == 4 ? 0 : YAFFS_SEQUENCE_CHECKPOINT_DATA;
    tags->obj_id = 2;
    tags->chunk_id = 1;
    if (read_mode == 0 || (read_mode == 2 && data)) return YAFFS_FAIL;
    if (data) {
        u32 hdr[4] = {YAFFS_CHECKPOINT_VERSION, 0, 0, 0};
        memcpy(data, hdr, sizeof(hdr));
        data[sizeof(hdr)] = 0x5a;
    }
    return YAFFS_OK;
}
static int mock_checkpoint_query(struct yaffs_dev *dev, int block,
                                 enum yaffs_block_state *state, u32 *seq)
{
    (void)dev; (void)block; query_calls++; *state = YAFFS_BLOCK_STATE_EMPTY;
    *seq = YAFFS_SEQUENCE_CHECKPOINT_DATA;
    return read_mode == 1 ? YAFFS_FAIL : YAFFS_OK;
}
static void test_checkpoint_read_failure(void)
{
    for (read_mode = 0; read_mode <= 4; read_mode++) {
        struct yaffs_dev dev = {0};
        struct yaffs_block_info block = {0};
        u8 buffer[64] = {0}, dest = 0x7a;
        int list[1] = {-1};
        dev.internal_start_block = dev.internal_end_block = 1;
        dev.param.chunks_per_block = 64; dev.data_bytes_per_chunk = sizeof(buffer);
        dev.block_info = &block; dev.checkpt_max_blocks = 1;
        dev.checkpt_block_list = list; dev.checkpt_buffer = buffer;
        dev.checkpt_next_block = 1; dev.checkpt_cur_block = -1;
        dev.checkpt_byte_offs = sizeof(buffer);
        dev.tagger.read_chunk_tags_fn = mock_checkpoint_read;
        dev.tagger.query_block_fn = mock_checkpoint_query;
        read_calls = query_calls = 0;
        int rc = yaffs2_checkpt_rd(&dev, &dest, 1);
        if (read_mode <= 2) {
            assert(rc < 0 && dest == 0x7a);
            assert(yaffs2_checkpt_rd(&dev, &dest, 1) < 0); /* latched */
            assert(yaffs_checkpt_close(&dev) == 0);
        } else if (read_mode == 3) {
            assert(rc == 1 && dest == 0x5a);
            assert(yaffs_checkpt_close(&dev) == 1);
        } else {
            assert(rc == 0 && dest == 0x7a); /* no checkpoint, not I/O error */
            assert(yaffs_checkpt_close(&dev) == 1);
        }
        assert(read_calls == ((read_mode == 2 || read_mode == 3) ? 2 : 1));
        assert(query_calls == ((read_mode == 0 || read_mode == 4) ? 0 : 1));
    }
    /* A skipped restore must not close a stale prior write stream. */
    struct yaffs_dev dev = {0};
    u8 buffer[64] = {0};
    dev.param.is_yaffs2 = 1; dev.param.skip_checkpt_rd = 1;
    dev.checkpt_buffer = buffer; dev.checkpt_open_write = 1;
    dev.checkpt_byte_offs = 17; dev.data_bytes_per_chunk = sizeof(buffer);
    dev.tagger.write_chunk_tags_fn = fail_checkpoint_program;
    checkpoint_programs = 0;
    assert(yaffs2_checkpt_restore(&dev) == 0 && checkpoint_programs == 0);
}

int main(int argc, char **argv)
{
    if (argc == 1 || strcmp(argv[1], "tags") == 0) test_failed_tag_read();
    if (argc == 1 || strcmp(argv[1], "gc") == 0) test_failed_gc_read();
    if (argc == 1 || strcmp(argv[1], "write") == 0) test_write_failure_keeps_cache();
    if (argc == 1 || strcmp(argv[1], "checkpoint") == 0) test_checkpoint_propagation();
    if (argc == 1 || strcmp(argv[1], "checkpoint-io") == 0) test_checkpoint_program_failure();
    if (argc == 1 || strcmp(argv[1], "checkpoint-read") == 0) test_checkpoint_read_failure();
    puts("YAFFS fault tests passed");
    return 0;
}
