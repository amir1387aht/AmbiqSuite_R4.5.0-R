#pragma once
#include "am_mcu_apollo.h"
#include "am_bsp.h"
#include "am_util.h"

#include <stddef.h>
int uart_write_bytes(const char *text, size_t length);
void uart_init();
void uart_flush();
#if defined(WATCH_GPU_PROFILE) && WATCH_GPU_PROFILE
#include <stddef.h>
#include <stdbool.h>
bool uart_profile_write(const char *text, size_t length);
#endif
