#ifndef WATCH_GPU_H
#define WATCH_GPU_H
#include <stdbool.h>
#include <stdint.h>
extern volatile bool watch_gpu_ready;
extern volatile uint32_t watch_gpu_submissions;
extern volatile uint32_t watch_gpu_completed_waits;
extern volatile uint32_t watch_panel_flushes;
void watch_gpu_init(void);
void watch_gpu_smoke_screen(void);
#endif
