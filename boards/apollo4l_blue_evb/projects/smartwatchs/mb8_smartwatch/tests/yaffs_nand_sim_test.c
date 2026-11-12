/* Host-only integration test: RAM NAND model, actual YAFFS2 direct and OOB tags.
 * This does not exercise the device driver, controller timing, or real flash.
 * SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "yaffs_guts.h"
#include "yaffsfs.h"

#define PAGE 2048
#define OOB 60
#define PPB 64
#define FIRST 2
#define LAST 81
#define BLOCKS 84
#define PAGES (BLOCKS * PPB)
#define FILE_PAGES 32
static u8 *main_area, *spare_area, *programmed;
static unsigned writes, erases, reads, bad_reads;
static int last_error;
void yaffsfs_Lock(void) {}
void yaffsfs_Unlock(void) {}
u32 yaffsfs_CurrentTime(void) { return 0; }
void yaffsfs_SetError(int e) { last_error=e; }
int yaffsfs_GetLastError(void) { return last_error; }
void *yaffsfs_malloc(size_t n) { return malloc(n); }
void yaffsfs_free(void *p) { free(p); }
void yaffsfs_get_malloc_values(unsigned *a, unsigned *b) { *a=*b=0; }
int yaffsfs_CheckMemRegion(const void *p, size_t n, int w) { (void)n; (void)w; return p ? 0 : -1; }
void yaffsfs_OSInitialisation(void) {}
int yaffs_start_up(void) { return 0; }
void yaffs_bug_fn(const char *f, int l) { fprintf(stderr,"BUG %s:%d\n",f,l); abort(); }
unsigned int yaffs_trace_mask;
static void valid_page(int p) { assert(p >= FIRST*PPB && p < (LAST+1)*PPB);  }
static int wr(struct yaffs_dev *d, int p, const u8 *data, int dl, const u8 *oob, int ol)
{
    (void)d; valid_page(p); assert(p/PPB != 4); assert(dl == PAGE && ol >= 0 && ol <= OOB);
    /* Conservative NAND model: YAFFS must program a page at most once per erase. */
    assert(!programmed[p]); programmed[p]=1;
    if (data) for (int i=0;i<dl;i++) main_area[(size_t)p*PAGE+i] &= data[i];
    if (oob) for (int i=0;i<ol;i++) spare_area[(size_t)p*OOB+i] &= oob[i];
    writes++; return YAFFS_OK;
}
static int rd(struct yaffs_dev *d, int p, u8 *data, int dl, u8 *oob, int ol, enum yaffs_ecc_result *ecc)
{
    (void)d; valid_page(p); if (p/PPB == 4) { bad_reads++; return YAFFS_FAIL; } assert(dl == PAGE && ol >= 0 && ol <= OOB);
    if (data) memcpy(data,main_area+(size_t)p*PAGE,dl);
    if (oob) memcpy(oob,spare_area+(size_t)p*OOB,ol);
    if (ecc) *ecc=YAFFS_ECC_RESULT_NO_ERROR;
    reads++; return YAFFS_OK;
}
static int erase(struct yaffs_dev *d, int b)
{
    (void)d; valid_page(b*PPB); assert(b != 4);
    memset(main_area+(size_t)b*PPB*PAGE,0xff,PPB*PAGE);
    memset(spare_area+(size_t)b*PPB*OOB,0xff,PPB*OOB);
    memset(programmed+b*PPB,0,PPB); erases++; return YAFFS_OK;
}
static int mark_bad(struct yaffs_dev *d, int b) { (void)d; (void)b; return YAFFS_FAIL; }
static int check_bad(struct yaffs_dev *d, int b) { (void)d; assert(b>=FIRST && b<=LAST); return b==4 ? YAFFS_FAIL : YAFFS_OK; }
static int init(struct yaffs_dev *d) { (void)d; return YAFFS_OK; }
static int query(struct yaffs_dev *d, int b, enum yaffs_block_state *s, u32 *seq)
{
    assert(b>=FIRST && b<=LAST); *s=YAFFS_BLOCK_STATE_DEAD; *seq=0;
    if (b==4) return YAFFS_OK;
    struct yaffs_ext_tags t={0};
    if (d->tagger.read_chunk_tags_fn(d,b*PPB,NULL,&t)!=YAFFS_OK) return YAFFS_FAIL;
    *s=t.chunk_used ? YAFFS_BLOCK_STATE_NEEDS_SCAN : YAFFS_BLOCK_STATE_EMPTY;
    *seq=t.chunk_used ? t.seq_number : 0; return YAFFS_OK;
}
static void verify(const u8 *expected)
{
    u8 *got=malloc(FILE_PAGES*PAGE); assert(got);
    int fd=yaffs_open("/nand/data.bin",O_RDONLY,0); assert(fd>=0);
    assert(yaffs_read(fd,got,FILE_PAGES*PAGE)==FILE_PAGES*PAGE);
    assert(memcmp(got,expected,FILE_PAGES*PAGE)==0);
    assert(yaffs_close(fd)==0); free(got);
}
int main(void)
{
    main_area=malloc((size_t)PAGES*PAGE); spare_area=malloc((size_t)PAGES*OOB); programmed=calloc(PAGES,1);
    assert(main_area && spare_area && programmed);
    memset(main_area,0xff,(size_t)PAGES*PAGE); memset(spare_area,0xff,(size_t)PAGES*OOB);
    struct yaffs_dev d={0};
    d.param.name="/nand"; d.param.is_yaffs2=1; d.param.total_bytes_per_chunk=PAGE;
    d.param.spare_bytes_per_chunk=OOB; d.param.chunks_per_block=PPB;
    d.param.start_block=FIRST; d.param.end_block=LAST; d.param.n_reserved_blocks=5;
    d.param.n_caches=4; d.param.cache_bypass_aligned=1; d.param.use_nand_ecc=1;
    d.param.stored_endian=1; d.param.max_objects=256; d.param.refresh_period=1000;
    d.drv.drv_write_chunk_fn=wr; d.drv.drv_read_chunk_fn=rd; d.drv.drv_erase_fn=erase;
    d.drv.drv_check_bad_fn=check_bad; d.drv.drv_mark_bad_fn=mark_bad; d.drv.drv_initialise_fn=init;
    d.tagger.query_block_fn=query; yaffs_add_device(&d);
    u8 *expected=malloc(FILE_PAGES*PAGE); assert(expected);
    for (unsigned i=0;i<FILE_PAGES*PAGE;i++) expected[i]=(u8)(i*37u+(i>>8));
    assert(yaffs_mount("/nand")==0); assert(bad_reads==0);
    int fd=yaffs_open("/nand/data.bin",O_CREAT|O_RDWR,0600); assert(fd>=0);
    assert(yaffs_write(fd,expected,FILE_PAGES*PAGE)==FILE_PAGES*PAGE);
    assert(yaffs_fsync(fd)==0); assert(yaffs_close(fd)==0); verify(expected);
    assert(yaffs_unmount("/nand")==0);
    assert(yaffs_mount("/nand")==0); verify(expected);
    assert(d.is_checkpointed);
    printf("checkpoint remount: restored=%d\n",d.is_checkpointed);
    fd=yaffs_open("/nand/data.bin",O_RDWR,0); assert(fd>=0);
    u8 page[PAGE];
    for (unsigned n=0;n<5500;n++) {
        unsigned index=(n*13u)%FILE_PAGES;
        for (unsigned i=0;i<PAGE;i++) page[i]=(u8)(n*17u+i*7u+(i>>8));
        memcpy(expected+index*PAGE,page,PAGE);
        assert(yaffs_pwrite(fd,page,PAGE,(Y_LOFF_T)index*PAGE)==PAGE);
        if ((n%64)==63) assert(yaffs_fsync(fd)==0);
    }
    assert(yaffs_fsync(fd)==0); assert(yaffs_close(fd)==0);
    verify(expected);
    printf("overwrite workload: writes=%u erases=%u gc-blocks=%u gc-copies=%u\n",writes,erases,d.n_gc_blocks,d.n_gc_copies);
    assert(d.n_gc_blocks>0 && erases>0);
    /* Clean operation boundary, no open handles; simulate restart without checkpoint. */
    yaffs_deinitialise(&d);
    assert(yaffs_mount3("/nand",0,1)==0); verify(expected);
    assert(yaffs_unmount("/nand")==0);
    unsigned before_writes=writes, before_erases=erases;
    assert(yaffs_mount2("/nand",1)==0); verify(expected);
    assert(yaffs_unmount("/nand")==0);
    assert(writes==before_writes && erases==before_erases && bad_reads==0);
    printf("PASS full scan, read-only remount, partition bounds, bad-block skip; reads=%u\n",reads);
    free(expected); free(programmed); free(spare_area); free(main_area);
    return 0;
}
