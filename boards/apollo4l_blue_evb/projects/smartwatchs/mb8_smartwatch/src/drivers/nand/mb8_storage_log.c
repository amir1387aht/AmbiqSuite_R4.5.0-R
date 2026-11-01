/* SPDX-License-Identifier: GPL-2.0-only
 * Avoid Ambiq printf's shared g_prfbuf: UART callback locking is too late to
 * protect formatting. No vendor/GPU changes, heap allocation or IRQ masking.
 */
#include "mb8_storage_log.h"
#include "uart_output.h"
#include <stdarg.h>
#include <stdio.h>
static unsigned dropped;

void mb8_storage_log(const char *format, ...)
{
    char    line[512];
    va_list args;
    va_start(args, format);
    int n = vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    /* Never emit a truncated PASS/status as if complete. Sink failure is
     * bounded/nonfatal; do not recurse into logging or stall the render task. */
    if (n < 0 || (size_t)n >= sizeof(line) || (n && !uart_log_write(line, (size_t)n)))
        dropped++;
}

unsigned mb8_storage_log_dropped(void)
{
    return dropped;
}
