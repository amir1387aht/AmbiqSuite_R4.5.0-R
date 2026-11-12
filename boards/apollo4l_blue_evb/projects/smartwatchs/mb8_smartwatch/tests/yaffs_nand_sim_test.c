/* Host-only integration test: RAM NAND model, actual YAFFS2 direct and OOB tags.
 * This does not exercise the device driver, controller timing, or real flash.
 * SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include "yaffs_guts.h"
#include "yaffsfs.h"
#include "yaffs_packedtags2.h"

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
static int cut_program, inject_query_failure, inject_tag_failure, inject_header_failure, injected;
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
    if (cut_program) {
        /* Three possible cuts, not a hardware power-cut model: main half,
         * full main + partial tags, or full page before callback return. */
        assert(data && dl == PAGE && oob && ol >= 8);
        int main_len=cut_program==1 ? PAGE/2 : PAGE;
        int tags_len=cut_program==1 ? 0 : cut_program==2 ? 8 : ol;
        for (int i=0; i<main_len; i++) main_area[(size_t)p*PAGE+i] &= data[i];
        for (int i=0; i<tags_len; i++) spare_area[(size_t)p*OOB+i] &= oob[i];
        _exit(77);
    }
    if (data) for (int i=0;i<dl;i++) main_area[(size_t)p*PAGE+i] &= data[i];
    if (oob) for (int i=0;i<ol;i++) spare_area[(size_t)p*OOB+i] &= oob[i];
    writes++; return YAFFS_OK;
}
static int rd(struct yaffs_dev *d, int p, u8 *data, int dl, u8 *oob, int ol, enum yaffs_ecc_result *ecc)
{
    (void)d; valid_page(p);
    if (inject_tag_failure && !injected && p%PPB && programmed[p] && !data && oob) {
        injected=1; return YAFFS_FAIL;
    }
    if (inject_header_failure && !injected && data && oob && programmed[p]) {
        struct yaffs_packed_tags2 pt; struct yaffs_ext_tags tags;
        memcpy(&pt,spare_area+(size_t)p*OOB,sizeof(pt));
        yaffs_unpack_tags2(d,&tags,&pt,1);
        if (tags.chunk_used && tags.chunk_id==0 && tags.ecc_result<=YAFFS_ECC_RESULT_FIXED) {
            injected=1; return YAFFS_FAIL;
        }
    }
    if (p/PPB == 4) { bad_reads++; return YAFFS_FAIL; } assert(dl == PAGE && ol >= 0 && ol <= OOB);
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
    if (inject_query_failure && b==FIRST+1) { injected=1; return YAFFS_FAIL; }
    if (b==4) return YAFFS_OK;
    struct yaffs_ext_tags t={0};
    if (d->tagger.read_chunk_tags_fn(d,b*PPB,NULL,&t)!=YAFFS_OK ||
        t.ecc_result>YAFFS_ECC_RESULT_FIXED) return YAFFS_FAIL;
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
    /* Shared across fork so the child can lose power without cleaning up. */
    main_area=mmap(NULL,(size_t)PAGES*PAGE,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANONYMOUS,-1,0);
    spare_area=mmap(NULL,(size_t)PAGES*OOB,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANONYMOUS,-1,0);
    programmed=mmap(NULL,PAGES,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANONYMOUS,-1,0);
    assert(main_area!=MAP_FAILED && spare_area!=MAP_FAILED && programmed!=MAP_FAILED);
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
    fflush(NULL);
    for (int mode=1; mode<=3; mode++) {
        pid_t child=fork(); assert(child>=0);
        if (child==0) {
            assert(yaffs_mount("/nand")==0);
            int interrupted=yaffs_open("/nand/data.bin",O_RDWR,0); assert(interrupted>=0);
            if (mode==2) {
                /* Exercise a torn tag in a non-first page: first-page tag ECC
                 * failure is instead tested as a fail-closed mount below. */
                assert(yaffs_pwrite(interrupted,expected+PAGE,PAGE,PAGE)==PAGE);
                assert(yaffs_fsync(interrupted)==0);
                assert(d.alloc_block>0 && d.alloc_page>0 && d.alloc_page<PPB-1);
            }
            memset(page,0x36+mode,sizeof(page)); cut_program=mode;
            (void)yaffs_pwrite(interrupted,page,PAGE,0);
            _exit(78); /* Must interrupt a real program callback. */
        }
        int status=0; assert(waitpid(child,&status,0)==child);
        assert(WIFEXITED(status) && WEXITSTATUS(status)==77);
        assert(yaffs_mount3("/nand",0,1)==0);
        if (mode==3) {
            /* An unacknowledged complete program may be old or new on recovery. */
            u8 got[PAGE], replacement[PAGE]; memset(replacement,0x36+mode,PAGE);
            fd=yaffs_open("/nand/data.bin",O_RDONLY,0); assert(fd>=0);
            assert(yaffs_read(fd,got,PAGE)==PAGE); assert(yaffs_close(fd)==0);
            assert(!memcmp(got,expected,PAGE) || !memcmp(got,replacement,PAGE));
            memcpy(expected,got,PAGE);
        }
        verify(expected);
        /* Recovery must not program an unerased torn page. */
        fd=yaffs_open("/nand/data.bin",O_RDWR,0); assert(fd>=0);
        memset(page,0x90+mode,sizeof(page)); memcpy(expected,page,PAGE);
        assert(yaffs_pwrite(fd,page,PAGE,0)==PAGE);
        assert(yaffs_fsync(fd)==0); assert(yaffs_close(fd)==0);
        assert(yaffs_unmount("/nand")==0);
        assert(yaffs_mount3("/nand",0,1)==0); verify(expected);
        assert(yaffs_unmount("/nand")==0);
        printf("PASS simulated program interruption mode %d; write/remount\n",mode);
        fflush(NULL);
    }
    /* No board-level latch: YAFFS must itself reject incomplete scans. */
    d.param.disable_lazy_load=1;
    for (int readonly=0; readonly<=1; readonly++) for (int mode=0; mode<3; mode++) {
        unsigned prev_writes=writes, prev_erases=erases; injected=0;
        inject_query_failure=(mode==0); inject_tag_failure=(mode==1);
        inject_header_failure=(mode==2);
        int rc=yaffs_mount3("/nand",readonly,1);
        assert(injected && rc<0 && !d.is_mounted);
        assert(writes==prev_writes && erases==prev_erases);
        inject_query_failure=inject_tag_failure=inject_header_failure=0;
        assert(yaffs_mount3("/nand",1,1)==0); verify(expected);
        assert(yaffs_unmount("/nand")==0);
        printf("PASS %s scan fails closed on %s failure\n",readonly ? "RO" : "RW",
               mode==0 ? "block query" : mode==1 ? "tag read" : "header read");
    }
    /* Damage first-page tag metadata, keeping the NAND transport successful.
     * A block sequence with uncorrectable tag ECC must not be trusted. */
    int damaged=-1; struct yaffs_packed_tags2 original, broken;
    for (int b=FIRST; b<=LAST && damaged<0; b++) {
        int p=b*PPB; if (b==4 || !programmed[p]) continue;
        memcpy(&original,spare_area+(size_t)p*OOB,sizeof(original));
        struct yaffs_ext_tags before;
        broken=original; yaffs_unpack_tags2(&d,&before,&broken,1);
        if (!before.chunk_used || before.seq_number<YAFFS_LOWEST_SEQUENCE_NUMBER ||
            before.seq_number>=YAFFS_HIGHEST_SEQUENCE_NUMBER) continue;
        for (unsigned x=0; x<sizeof(original.t)*8 && damaged<0; x++)
            for (unsigned y=x+1; y<sizeof(original.t)*8 && damaged<0; y++) {
                struct yaffs_packed_tags2 probe; struct yaffs_ext_tags tags;
                broken=original;
                ((u8 *)&broken.t)[x/8] ^= (u8)(1u<<(x%8));
                ((u8 *)&broken.t)[y/8] ^= (u8)(1u<<(y%8));
                probe=broken; yaffs_unpack_tags2(&d,&tags,&probe,1);
                if (tags.ecc_result==YAFFS_ECC_RESULT_UNFIXED) damaged=p;
            }
    }
    assert(damaged>=0);
    memcpy(spare_area+(size_t)damaged*OOB,&broken,sizeof(broken));
    before_writes=writes; before_erases=erases;
    assert(yaffs_mount3("/nand",1,1)<0 && !d.is_mounted);
    assert(writes==before_writes && erases==before_erases);
    /* Restore the model snapshot, not a repair operation on physical NAND. */
    memcpy(spare_area+(size_t)damaged*OOB,&original,sizeof(original));
    assert(yaffs_mount3("/nand",1,1)==0); verify(expected);
    assert(yaffs_unmount("/nand")==0);
    printf("PASS uncorrectable first-page tag ECC fails mount closed\n");
    free(expected);
    assert(munmap(programmed,PAGES)==0);
    assert(munmap(spare_area,(size_t)PAGES*OOB)==0);
    assert(munmap(main_area,(size_t)PAGES*PAGE)==0);
    return 0;
}
