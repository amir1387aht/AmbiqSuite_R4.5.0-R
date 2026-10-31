#ifndef WATCH_RGB565_H
#define WATCH_RGB565_H
#include <stdint.h>
/* BGRA8888 in little-endian memory is the integer 0xAARRGGBB. */
static inline uint16_t watch_bgra8888_to_rgb565(uint32_t pixel)
{
    return (uint16_t)(((pixel >> 8) & 0xf800U) |
                      ((pixel >> 5) & 0x07e0U) | ((pixel >> 3) & 0x001fU));
}
#endif
