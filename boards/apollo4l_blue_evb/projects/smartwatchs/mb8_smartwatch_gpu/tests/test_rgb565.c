#include <assert.h>
#include <stdio.h>
#include "watch_rgb565.h"
int main(void)
{
    assert(watch_bgra8888_to_rgb565(0x00ff0000) == 0xf800);
    assert(watch_bgra8888_to_rgb565(0x0000ff00) == 0x07e0);
    assert(watch_bgra8888_to_rgb565(0x000000ff) == 0x001f);
    assert(watch_bgra8888_to_rgb565(0xffffffff) == 0xffff);
    assert(watch_bgra8888_to_rgb565(0xff000000) == 0);
    for (uint32_t c = 0; c < 65536; ++c) {
        uint32_t r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
        uint32_t pixel = (((r << 3) | (r >> 2)) << 16) |
                         (((g << 2) | (g >> 4)) << 8) | ((b << 3) | (b >> 2));
        assert(watch_bgra8888_to_rgb565(pixel) == c);
    }
    puts("PASS: all 65,536 RGB565 colors round-trip through BGRA8888");
    return 0;
}
