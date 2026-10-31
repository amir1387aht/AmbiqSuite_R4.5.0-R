#!/usr/bin/env python3
"""Execute production profiler producer/consumer with bounded RTOS/UART mocks."""
from pathlib import Path
import subprocess,tempfile
P=Path(__file__).resolve().parents[1]
s=(P/'libs/gpu_port/watch_gpu_profile.c').read_text()
producer=s[s.index('void watch_gpu_profile_poll(void)'):s.index('#else\nvoid watch_gpu_profile_init')]
assert 'xQueueOverwrite(' in producer and 'snprintf' not in producer and 'uart_' not in producer
with tempfile.TemporaryDirectory() as temp:
 t=Path(temp)
 (t/'FreeRTOS.h').write_text("""#pragma once
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#define configASSERT(x) assert(x)
#define configCPU_CLOCK_HZ 192000000UL
#define TASK_PRIORITY_MEDIUM 2
#define tskIDLE_PRIORITY 0
#define pdTRUE 1
#define pdPASS 1
#define portMAX_DELAY UINT32_MAX
#define portTICK_PERIOD_MS 1
""")
 (t/'queue.h').write_text("""#pragma once
#include "FreeRTOS.h"
typedef void *QueueHandle_t;
QueueHandle_t xQueueCreate(unsigned, unsigned);
int xQueueReceive(QueueHandle_t,void*,uint32_t);
int xQueueOverwrite(QueueHandle_t,const void*);
""")
 (t/'task.h').write_text("""#pragma once
#include "FreeRTOS.h"
uint32_t xTaskGetTickCount(void);
int xTaskCreate(void (*)(void*),const char*,unsigned,void*,unsigned,void*);
""")
 (t/'am_mcu_apollo.h').write_text("""#pragma once
#include <stdint.h>
typedef struct {uint32_t CYCCNT;} DWT_t;
extern DWT_t mock_dwt;
#define DWT (&mock_dwt)
""")
 (t/'am_bsp.h').write_text('#pragma once\n')
 (t/'am_util.h').write_text('#pragma once\n')
 code=r"""
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>
#include <setjmp.h>
static int format_calls;
static int test_snprintf(char *s,size_t n,const char *f,...) {
 va_list a;va_start(a,f);++format_calls;int r=vsnprintf(s,n,f,a);va_end(a);return r;
}
#define snprintf test_snprintf
#include "watch_gpu_profile.c"
#undef snprintf
volatile watch_gpu_stats_t watch_gpu_stats;
volatile bool watch_gpu_ready;
DWT_t mock_dwt;
static uint32_t tick;
static watch_profile_sample_t queued;
static unsigned writes,receives,uart_calls;
static jmp_buf done;
uint32_t xTaskGetTickCount(void) {return tick;}
QueueHandle_t xQueueCreate(unsigned count,unsigned size) {assert(count==1 && size==sizeof(queued));return &queued;}
int xTaskCreate(void (*entry)(void*),const char *name,unsigned stack,void *arg,unsigned priority,void *handle) {
 assert(entry==profile_output_task && name && stack==1024 && !arg && priority<TASK_PRIORITY_MEDIUM && !handle);return pdPASS;
}
int xQueueOverwrite(QueueHandle_t q,const void *p) {assert(q==&queued);memcpy(&queued,p,sizeof(queued));++writes;return pdTRUE;}
int xQueueReceive(QueueHandle_t q,void *p,uint32_t wait) {
 assert(q==&queued && wait==portMAX_DELAY);
 if(receives++) longjmp(done,1);
 memcpy(p,&queued,sizeof(queued));return pdTRUE;
}
bool uart_profile_write(const char *text,size_t length) {
 assert(length==strlen(text) && length<768 && strstr(text,"GPUprof dma_fps=60 te_hz=60"));
 assert(strstr(text,"gap1=60 gap2=0 gap3p=0 hw=123"));++uart_calls;return true;
}
int main(void) {
 watch_gpu_profile_init();tick=999;watch_gpu_profile_poll();assert(!writes && !format_calls && !uart_calls);
 watch_gpu_stats.panel_frames=60;watch_gpu_stats.te_edges=60;watch_gpu_stats.panel_gap1=60;
 tick=1000;watch_gpu_profile_poll();assert(writes==1 && !format_calls && !uart_calls);
 watch_gpu_stats.panel_frames=120;watch_gpu_stats.te_edges=120;watch_gpu_stats.panel_gap1=120;watch_gpu_stats.completed_hw_id=123;
 tick=2000;watch_gpu_profile_poll();assert(writes==2 && queued.elapsed_ms==1000 && queued.previous.panel_frames==60 && !format_calls && !uart_calls);
 if(!setjmp(done)) profile_output_task(NULL);
 assert(format_calls==1 && uart_calls==1);
 puts("PASS: production profiler enqueues without formatting/UART; one-slot overwrite; lower-priority output task formats bounded line with cadence counters");
 return 0;
}
"""
 (t/'test.c').write_text(code)
 subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-DWATCH_GPU_PROFILE=1','-I'+str(t),'-I'+str(P/'libs/gpu_port'),'-I'+str(P/'src'),str(t/'test.c'),'-o',str(t/'test')],check=True)
 subprocess.run([str(t/'test')],check=True)
