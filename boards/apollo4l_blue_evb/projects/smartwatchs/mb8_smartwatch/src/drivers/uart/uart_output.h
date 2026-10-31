#pragma once
#include "am_mcu_apollo.h"
#include "am_bsp.h"
#include "am_util.h"

#include <stddef.h>
int  uart_write_bytes(const char *text, size_t length);
void uart_init();
void uart_flush();
#include <stdbool.h>
/* Task-context background diagnostics; bounded wait, no fail-stop on errors. */
bool uart_log_write(const char *text, size_t length);
#if defined(WATCH_GPU_PROFILE) && WATCH_GPU_PROFILE
static inline bool uart_profile_write(const char *text, size_t length)
{
    return uart_log_write(text, length);
}
#endif
