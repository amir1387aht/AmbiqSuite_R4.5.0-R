#ifndef WATCH_GPU_H
#define WATCH_GPU_H
#include <stdbool.h>
#include <stdint.h>
#ifndef WATCH_GPU_PROFILE
#define WATCH_GPU_PROFILE 0
#endif

typedef struct {
    uint32_t submitted_cl;
    uint32_t gpu_glyphs;
    uint32_t cpu_glyphs;
    uint32_t glyph_cache_new;
    uint32_t glyph_cache_hits;
    uint32_t completed_hw_id;
    uint32_t panel_frames;
    uint32_t te_edges;
    uint32_t handler_cycles;
    uint32_t convert_cycles;
    uint32_t gpu_wait_cycles;
    uint32_t panel_wait_cycles;
} watch_gpu_stats_t;
extern volatile watch_gpu_stats_t watch_gpu_stats;
extern volatile bool watch_gpu_ready;
void watch_gpu_note_submit(int32_t submission_id, uint32_t glyphs);
void watch_gpu_note_completed(int32_t submission_id, uint32_t hardware_id);
void watch_gpu_profile_poll(void);
#if WATCH_GPU_PROFILE
#include "am_mcu_apollo.h"
static inline uint32_t watch_profile_start(void) { return DWT->CYCCNT; }
#define WATCH_PROFILE_ADD(field, start) (watch_gpu_stats.field += DWT->CYCCNT - (start))
#else
static inline uint32_t watch_profile_start(void) { return 0; }
#define WATCH_PROFILE_ADD(field, start) ((void)(start))
#endif
void watch_gpu_init(void);
#endif
