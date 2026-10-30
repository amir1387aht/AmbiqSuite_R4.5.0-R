#ifndef WATCH_TICK_H
#define WATCH_TICK_H
#include "FreeRTOS.h"
#include "task.h"
static inline uint32_t watch_tick_ms(void)
{
    return (uint32_t)xTaskGetTickCount() * (uint32_t)portTICK_PERIOD_MS;
}
#endif
