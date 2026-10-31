#pragma once
#include "am_mcu_apollo.h"
#include "am_bsp.h"
#include "am_util.h"

void uart_init();
void uart_flush();
#if defined(WATCH_GPU_PROFILE) && WATCH_GPU_PROFILE
#include <stddef.h>
#include <stdbool.h>
bool uart_profile_write(const char *text, size_t length);
#endif
