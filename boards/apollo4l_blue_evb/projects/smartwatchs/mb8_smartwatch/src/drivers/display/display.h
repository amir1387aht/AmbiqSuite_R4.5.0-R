#pragma once
#include "am_mcu_apollo.h"
#include "am_bsp.h"
#include "am_util.h"
#include "mspi_raydium.h"
#include "lvgl.h"
#include "./display_defines.h"
#include "../gpio/gpio.h"

#define display_height 490
#define display_width  192

#define PANEL_COLOR_DEPTH 16
#define DRAW_BUF_LINES    64
#define DRAW_BUF_SIZE     (display_width * DRAW_BUF_LINES)

extern lv_color_t display_buffer1[DRAW_BUF_SIZE];
extern lv_color_t display_buffer2[DRAW_BUF_SIZE];

void display_init();
void display_end();
void display_update_area(const lv_area_t *area, uint8_t *px_map, void (*pArgXferDone)(void *));
/* Immediate panel command: LVGL task only. */
void display_set_brightness(uint8_t brightness);
/* Other tasks enqueue brightness changes through this function. */
void display_request_brightness(uint8_t brightness);
bool display_is_busy();
void display_turn_off(void);
void display_turn_on(void);