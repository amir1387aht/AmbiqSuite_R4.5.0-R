#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "watch_framebuffer.h"
#define WIDTH 192
#define HEIGHT 490
static uint32_t source[WIDTH * HEIGHT];
static uint16_t frame[WIDTH * HEIGHT];
static uint16_t reference[WIDTH * HEIGHT];

int main(void)
{
    for (unsigned i = 0; i < WIDTH * HEIGHT; ++i) {
        source[i] = 0xff000000u | ((i * 37u) & 0x00ffffffu);
        reference[i] = watch_bgra8888_to_rgb565(source[i]);
    }
    /* Exercise actual conversion helper at the port's 64-line stripe size,
     * including the final 42-line stripe. */
    for (unsigned y = 0; y < HEIGHT; y += 64) {
        unsigned h = HEIGHT - y < 64 ? HEIGHT - y : 64;
        watch_bgra8888_rect_to_rgb565(frame + y * WIDTH, WIDTH,
                                    source + y * WIDTH, WIDTH, h);
    }
    for (unsigned i = 0; i < WIDTH * HEIGHT; ++i) assert(frame[i] == reference[i]);

    /* Arbitrary partial rectangles must preserve every other framebuffer pixel,
     * including narrow edge/corner invalidations and non-full-width source rows. */
    for (unsigned y = 0; y < HEIGHT; y += 17) {
        for (unsigned x = 0; x < WIDTH; x += 13) {
            unsigned w = WIDTH - x < 11 ? WIDTH - x : 11;
            unsigned h = HEIGHT - y < 7 ? HEIGHT - y : 7;
            for (unsigned i = 0; i < w * h; ++i) source[i] = 0xff00a040u + i;
            watch_bgra8888_rect_to_rgb565(frame + y * WIDTH + x, WIDTH,
                                        source, w, h);
            for (unsigned row = 0; row < h; ++row)
                for (unsigned col = 0; col < w; ++col)
                    reference[(y + row) * WIDTH + x + col] =
                        watch_bgra8888_to_rgb565(source[row * w + col]);
            for (unsigned i = 0; i < WIDTH * HEIGHT; ++i)
                assert(frame[i] == reference[i]);
        }
    }
    watch_te_cadence_t cadence = {0};
    assert(watch_te_cadence_sample(&cadence, 100) == 0);
    assert(watch_te_cadence_sample(&cadence, 101) == 1);
    assert(watch_te_cadence_sample(&cadence, 103) == 2);
    assert(watch_te_cadence_sample(&cadence, 107) == 3);
    cadence.previous_te = UINT32_MAX - 1u;
    assert(watch_te_cadence_sample(&cadence, 0) == 2);
    assert(watch_te_cadence_sample(&cadence, 0) == 0);
    puts("PASS: TE cadence classifies consecutive, skipped, initial and wrapped intervals");
    watch_dirty_rows_t rows = {0};
    watch_dirty_rows_include(&rows, 100, 140);
    watch_dirty_rows_include(&rows, 20, 30);
    watch_dirty_rows_include(&rows, 80, 90);
    assert(rows.valid && rows.first == 20 && rows.last == 140);
    rows.valid = false;
    watch_dirty_rows_include(&rows, 480, 489);
    assert(rows.first == 480 && rows.last == 489);
    rows.valid = false;
    for (unsigned y = 0; y < HEIGHT; y += 64)
        watch_dirty_rows_include(&rows, y, HEIGHT - y < 64 ? HEIGHT - 1 : y + 63);
    assert(rows.first == 0 && rows.last == HEIGHT - 1);
    puts("PASS: dirty row union handles multiple invalid areas, reset and eight-stripe frames");
    puts("PASS: full frame assembled from eight stripes; 435 partial rectangles preserve all untouched pixels");
    return 0;
}
