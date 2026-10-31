/* Host command-contract test with mocked Nema execution, NOT a GPU emulator. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "watch_gpu_convert.h"
#include "watch_gpu.h"
#include "watch_framebuffer.h"
#include "nema_cmdlist.h"
#include "nema_graphics.h"
#include "nema_blender.h"
#include "nema_error.h"
#include "nema_regs.h"
volatile watch_gpu_stats_t watch_gpu_stats;
volatile bool watch_gpu_ready;
void watch_gpu_cpu_write_complete(void) {}
static nema_cmdlist_t *bound;
static uint8_t command_storage[1024];
static uintptr_t src_address, dst_address;
static uint32_t clip_x, clip_y, clip_w, clip_h;
static nema_tex_format_t captured_source_format;
static int dst_stride, src_stride, blit_x, blit_y;
static bool allocation_fails, command_fails, submitted;
static uint32_t error, submissions, waits;
static unsigned phase;
static _Alignas(32) uint32_t source[192*64];
static _Alignas(32) uint16_t frame[192*490];
static uint16_t reference[192*490];
static _Alignas(32) uint16_t snapshot[192*490];
int am_util_stdio_printf(const char *fmt, ...) { (void)fmt; return 0; }
void watch_gpu_note_submit(int32_t id, uint32_t glyphs) { assert(id > 0 && glyphs == 0); }
void watch_gpu_note_completed(int32_t id, uint32_t hw) { assert((uint32_t)id == hw); }
uint32_t nema_reg_read(uint32_t reg) { assert(reg == NEMA_CLID); return submissions; }
uint32_t nema_get_error(void) { uint32_t e=error; error=0; return e; }
nema_cmdlist_t nema_cl_create_sized(int bytes) {
    assert(bytes == 1024);
    nema_cmdlist_t cl = {0};
    if (allocation_fails) { error=NEMA_ERR_OUT_OF_GFX_MEMORY; return cl; }
    cl.bo.base_virt=command_storage; return cl;
}
void nema_cl_destroy(nema_cmdlist_t *cl) { assert(!submitted); cl->bo.base_virt=NULL; }
void nema_cl_rewind(nema_cmdlist_t *cl) { assert(!submitted && !bound && cl->bo.base_virt); phase=0; }
void nema_cl_bind(nema_cmdlist_t *cl) { assert(!bound && phase == 0); bound=cl; }
nema_cmdlist_t *nema_cl_get_bound(void) { return bound; }
void nema_cl_unbind(void) { assert(bound); bound=NULL; }
void nema_set_clip(int32_t x,int32_t y,uint32_t w,uint32_t h) {
    assert(bound && phase++ == 0 && x >= 0 && y == 0); clip_x=x;clip_y=y;clip_w=w;clip_h=h;
}
void nema_bind_dst_tex(uintptr_t addr,uint32_t w,uint32_t h,nema_tex_format_t fmt,int32_t stride) {
    assert(bound && phase++ == 1 && !(addr&7u) && fmt == NEMA_RGB565);
    assert(w == clip_w+clip_x && h == clip_h && stride == 192*2);
    dst_address=addr; dst_stride=stride;
}
void nema_bind_src_tex(uintptr_t addr,uint32_t w,uint32_t h,nema_tex_format_t fmt,int32_t stride,nema_tex_mode_t mode) {
    assert(bound && phase++ == 2 && !(addr&7u));
    assert((fmt == NEMA_BGRA8888 || fmt == NEMA_RGB565) && mode == NEMA_FILTER_PS && w == clip_w && h == clip_h);
    assert(stride == (int32_t)w*(fmt == NEMA_BGRA8888 ? 4 : 2));
    captured_source_format=fmt;src_address=addr;src_stride=stride;
}
void nema_set_blend(uint32_t mode,nema_tex_t dst,nema_tex_t src,nema_tex_t src2) {
    assert(bound && phase++ == 3 && mode == NEMA_BL_SRC && dst == NEMA_TEX0 && src == NEMA_TEX1 && src2 == NEMA_NOTEX);
}
void nema_blit(int x,int y) {
    assert(bound && phase++ == 4 && x == (int)clip_x && y == 0);blit_x=x;blit_y=y;
    if (command_fails) error=NEMA_ERR_NO_BOUND_CL;
}
void nema_cl_submit(nema_cmdlist_t *cl) {
    assert(!bound && !submitted && phase == 5);submitted=true;cl->submission_id=++submissions;
}
int nema_cl_wait(nema_cmdlist_t *cl) {
    assert(submitted && cl->submission_id == (int32_t)submissions);++waits;
    /* CPU reference execution of captured coordinates/strides; validates the
     * command contract and untouched pixels, not hardware raster/byte order. */
    for (unsigned y=0;y<clip_h;++y) for (unsigned x=0;x<clip_w;++x) {
        uint16_t color;
        if (captured_source_format == NEMA_BGRA8888) {
            uint32_t pixel;
            memcpy(&pixel,(const uint8_t*)src_address+y*src_stride+x*4,4);
            color=watch_bgra8888_to_rgb565(pixel);
        } else {
            memcpy(&color,(const uint8_t*)src_address+y*src_stride+x*2,2);
        }
        uint16_t *d=(uint16_t*)(dst_address+(y+blit_y)*dst_stride);
        d[x+blit_x]=color;
    }
    cl->submission_id=-1; /* Match the real SDK side effect. */
    submitted=false;return 0;
}
static void check_rect(unsigned x,unsigned y,unsigned w,unsigned h) {
    for (unsigned i=0;i<w*h;++i) source[i]=0xff000000u|((i*73u+x*131u+y)&0x00ffffffu);
    watch_gpu_convert_rect(frame+y*192+x,192,source,w,h);
    assert(!submitted && !bound);
    for (unsigned row=0;row<h;++row) for(unsigned col=0;col<w;++col)
        reference[(y+row)*192+x+col]=watch_bgra8888_to_rgb565(source[row*w+col]);
    assert(memcmp(frame,reference,sizeof(frame)) == 0);
}
int main(void) {
    for(unsigned i=0;i<192*490;++i) frame[i]=reference[i]=(uint16_t)i;
    watch_gpu_convert_init();
    for (unsigned y=0;y<490;y+=64) check_rect(0,y,192,490-y<64?490-y:64);
    for (unsigned y=0;y<490;y+=17) for(unsigned x=0;x<192;x+=13)
        check_rect(x,y,192-x<11?192-x:11,490-y<7?490-y:7);
    watch_gpu_copy_frame(snapshot,frame,192,490);
    assert(!memcmp(snapshot,frame,sizeof(frame)));
#if WATCH_GPU_CONVERT
    assert(submissions == waits && waits == 444 && watch_gpu_stats.convert_gpu == 443);
    command_fails=true;check_rect(2,4,10,6);command_fails=false;
    assert(submissions == 444 && watch_gpu_stats.convert_cpu == 1);
    allocation_fails=true;watch_gpu_convert_init();check_rect(188,489,4,1);
    assert(submissions == 444 && watch_gpu_stats.convert_cpu == 2);
#else
    assert(!submissions && watch_gpu_stats.convert_cpu == 443);
#endif
    puts("PASS: conversion command contract/reference rectangles, 64-bit prefix alignment, source-copy blend, strides, completion ordering and CPU fallbacks (not hardware GPU validation)");
    return 0;
}
