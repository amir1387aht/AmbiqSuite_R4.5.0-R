#!/usr/bin/env python3
"""Execute the actual local ISR body with host RTOS/register mocks."""
from pathlib import Path
import subprocess,tempfile
P=Path(__file__).resolve().parents[1]
s=(P/'libs/gpu_port/watch_nema_hal.c').read_text()
a=s.index('static void prvNemaInterruptHandler(');b=s.index('\n}',a)+2
body=s[a:b]
assert 'xHigherPriorityTaskWoken = pdFALSE' in body
assert body.index('last_cl_id =') < body.index('portYIELD_FROM_ISR(')
m=(P/'gcc/Makefile').read_text()
assert '$(NEMA_PLATFORM_DIR)/,nema_hal.c' not in m
mock=r"""
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#define pdFALSE 0
#define pdTRUE 1
#define NEMA_INTERRUPT 0
#define NEMA_CLID 1
#define eNoAction 0
typedef int BaseType_t;
static int stage, wake, yielded, last_cl_id;
#ifndef BAREMETAL
static void *xSemaphore=(void*)1, *xHandlingTask=(void*)1;
static void signal_task(void *handle, int *flag) {
 assert(handle && *flag == pdFALSE && stage == 1); stage=2; *flag=wake;
}
#define xSemaphoreGiveFromISR(s,f) signal_task((s),(f))
#define xTaskNotifyFromISR(t,v,a,f) signal_task((t),(f))
#define portYIELD_FROM_ISR(f) do { assert(stage==4 && last_cl_id==42); yielded=(f); stage=5; } while(0)
#endif
static void nema_reg_write(unsigned r,unsigned v) { assert(r==NEMA_INTERRUPT && v==0 && stage==0);stage=1; }
static unsigned nema_reg_read(unsigned r) {
 assert(r==NEMA_CLID);
#ifdef BAREMETAL
 assert(stage==1);
#else
 assert(stage==2);
#endif
 stage=3;return 42;
}
static void callback(int id) { assert(stage==3 && id==42 && last_cl_id==42);stage=4; }
static void (*nemagfx_cb)(int)=callback;
"""
main=r"""
int main(void) {
 for(wake=0;wake<=1;++wake) {
  stage=0;yielded=-1;last_cl_id=-1;prvNemaInterruptHandler(NULL);
#ifdef BAREMETAL
  assert(stage==4 && yielded==-1);
#else
  assert(stage==5 && yielded==wake);
#endif
 }
 return 0;
}
"""
with tempfile.TemporaryDirectory() as t:
 p=Path(t);(p/'test.c').write_text(mock+body+main)
 for option in ['-DWAIT_IRQ_BINARY_SEMAPHORE=1','-DWAIT_IRQ_BINARY_SEMAPHORE=0','-DBAREMETAL']:
  subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-variable',option,str(p/'test.c'),'-o',str(p/'test')],check=True)
  subprocess.run([str(p/'test')],check=True)
print('PASS: actual GPU ISR publishes completion before conditional yield; initialized wake flag; semaphore, notification and bare-metal branches')
