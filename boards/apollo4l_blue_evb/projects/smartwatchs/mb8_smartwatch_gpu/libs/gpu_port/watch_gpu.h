#ifndef WATCH_GPU_H
#define WATCH_GPU_H
#include <stdbool.h>
#include <stdint.h>
extern volatile bool watch_gpu_ready;
void watch_gpu_init(void);
#endif
