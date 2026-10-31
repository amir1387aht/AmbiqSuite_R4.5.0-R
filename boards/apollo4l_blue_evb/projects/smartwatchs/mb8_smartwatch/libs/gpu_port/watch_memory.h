#ifndef WATCH_MEMORY_H
#define WATCH_MEMORY_H
/* Call on the owning LVGL task after display/GPU/UI initialization. */
void watch_memory_report(void);
/* Start compact UART snapshots every 2000 ms, below the LVGL task priority. */
void watch_memory_monitor_start(void);
#endif
