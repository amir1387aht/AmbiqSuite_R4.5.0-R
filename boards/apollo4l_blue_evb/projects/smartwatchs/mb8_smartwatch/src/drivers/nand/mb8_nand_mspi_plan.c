/* SPDX-License-Identifier: GPL-2.0-only
 * DS35X1GBXXX rev07 tables1.3/4.7 and figures3.8/3.11: 6B/32 are1-1-4.
 */
#include "mb8_nand_mspi_plan.h"
#include <string.h>

int mb8_nand_mspi_clock_select(uint32_t hz, uint32_t *divider)
{
    if (!divider || hz > DS35_MAX_CLOCK_HZ)
        return DS35_EINVAL;
    switch (hz)
    {
        case 3000000:
        case 4000000:
        case 6000000:
        case 8000000:
        case 12000000:
        case 16000000:
        case 24000000:
        case 32000000:
        case 48000000:
            *divider = 96000000u / hz;
            return DS35_OK;
        default:
            return DS35_EINVAL; /* No rounding up to96MHz. */
    }
}

int mb8_nand_mspi_plan(const uint8_t *h,
    size_t                            hn,
    const uint8_t                    *tx,
    uint8_t                          *rx,
    size_t                            len,
    unsigned                          width,
    bool                              quad_ready,
    struct mb8_nand_mspi_plan        *p)
{
    if (!p || !h || !hn || hn > 4 || (tx && rx) || (len && !tx && !rx))
        return DS35_EINVAL;
    memset(p, 0, sizeof(*p));
    p->opcode     = h[0];
    p->data_width = width;
    p->receive    = rx != NULL;
    switch (h[0])
    {
        case 0xff:
        case 0x06:
        case 0x04:
            if (hn != 1 || len || width != 1)
                return DS35_EINVAL;
            break;
        case 0x0f:
        case 0x1f:
            if (hn != 2 || len != 1 || width != 1 || (h[0] == 0x0f ? !rx : !tx))
                return DS35_EINVAL;
            p->address_bytes = 1;
            p->address       = h[1];
            break;
        case 0x9f:
            if (hn != 2 || h[1] != 0 || len != 2 || !rx || width != 1)
                return DS35_EINVAL;
            /* One serial zero byte emits the specified8 ID dummy clocks. */
            p->address_bytes = 1;
            break;
        case 0x13:
        case 0x10:
        case 0xd8:
            if (hn != 4 || len || width != 1 || h[1] != 0)
                return DS35_EINVAL;
            p->address_bytes = 3;
            p->address       = ((uint32_t)h[2] << 8) | h[3];
            break;
#if WATCH_NAND_STARTUP_TESTS || WATCH_NAND_FULL_FORMAT
        case 0x03:
        {
            if (hn != 4 || width != 1 || !rx || !len || len > 256 || (h[1] & 0xf0) || h[3])
                return DS35_EINVAL;
            uint32_t column = ((uint32_t)h[1] << 8) | h[2];
            if (column >= DS35_PAGE + DS35_OOB || len > DS35_PAGE + DS35_OOB - column)
                return DS35_EINVAL;
            /* Rev07 Table1.3: two column bytes, then8 dummy clocks.
             * Use HAL ENTturn explicitly, not a third fake address byte. */
            p->address_bytes = 2;
            p->address       = column;
            p->dummy_clocks  = 8;
            break;
        }
#endif
        case 0x6b:
        case 0x32:
        {
            bool read = h[0] == 0x6b;
            if (width != 4 || !quad_ready)
                return DS35_NOT_READY;
            if (hn != (read ? 4u : 3u) || (h[1] & 0xf0) || !len || (read ? !rx : !tx))
                return DS35_EINVAL;
            uint32_t col   = ((uint32_t)h[1] << 8) | h[2];
            uint32_t limit = read ? DS35_PAGE + DS35_OOB : DS35_PROGRAM_BYTES;
            if (col >= limit || len > limit - col || (read && h[3] != 0))
                return DS35_EINVAL;
            p->address_bytes = 2;
            p->address       = col;
            p->dummy_clocks  = read ? 8 : 0;
            p->dma           = true;
            break;
        }
        default:
            return DS35_EINVAL;
    }
    return DS35_OK;
}
