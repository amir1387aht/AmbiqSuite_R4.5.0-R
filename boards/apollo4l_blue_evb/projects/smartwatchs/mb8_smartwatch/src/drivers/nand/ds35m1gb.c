/* SPDX-License-Identifier: GPL-2.0-only
 * DS35X1GBXXX rev07, https://www.dosilicon.com/resources/SPI%20NAND/DS35X1GBXXX_rev07.pdf
 * x1 controls, optional1-1-4 quad data, ECC8/512 enabled; never writes parity bytes 0x840..0x87f.
 */
#include "ds35m1gb.h"
#include <string.h>

/* Compare documented B0 controls; never ignore QE/ECC/OTP mismatches. Keep raw
 * SET/GET bytes unchanged: reserved bits are not promised stable/read-writable.
 * All SET bytes retain the existing read-modify-write policy; no OTP operation.
 */
static bool b0_matches(uint8_t expected, uint8_t observed)
{
    return ((expected ^ observed) & DS35_B0_DEFINED_MASK) == 0;
}

/* Retain the first failing read/BBM phase across cleanup. A later probe clears
 * it only after the worker has printed the evidence and elected to continue. */
static void operation_begin(struct ds35_nand *n, const char *phase, uint32_t page)
{
    if (n->operation.result >= 0)
        n->operation = (struct ds35_operation_diagnostic){.phase = phase, .page = page};
}

static void operation_stage(struct ds35_nand *n, const char *phase, uint32_t page)
{
    if (n->operation.result < 0)
        return;
    /* Keep completed C0 evidence through cache-read phases for this page, but
     * never attach a previous page/operation's status to a failed new read. */
    bool    valid  = n->operation.status_valid && n->operation.page == page;
    uint8_t status = n->operation.status;
    n->operation   = (struct ds35_operation_diagnostic){
        .phase = phase, .page = page, .status_valid = valid, .status = valid ? status : 0};
}

static int operation_error(struct ds35_nand *n, int result)
{
    if (result < 0 && n->operation.result >= 0)
        n->operation.result = result;
    return result;
}

static int transfer(struct ds35_nand *n,
    const uint8_t                    *h,
    size_t                            hn,
    const uint8_t                    *tx,
    uint8_t                          *rx,
    size_t                            len,
    unsigned                          width)
{
    if (n->fault)
        return DS35_NOT_READY;
    int r = n->bus.transfer(n->bus.ctx, h, hn, tx, rx, len, width);
    if (r < 0)
        n->fault = true; /* Unknown operation completion: never replay. */
    return r;
}

static int command(struct ds35_nand *n, uint8_t op)
{
    return transfer(n, &op, 1, NULL, NULL, 0, 1);
}

static int feature(struct ds35_nand *n, uint8_t addr, uint8_t *value)
{
    uint8_t h[] = {0x0f, addr};
    return transfer(n, h, 2, NULL, value, 1, 1);
}

static int set_feature(struct ds35_nand *n, uint8_t addr, uint8_t value)
{
    uint8_t h[] = {0x1f, addr};
    return transfer(n, h, 2, &value, NULL, 1, 1);
}

static int wait_ready(struct ds35_nand *n, uint32_t max_us, uint8_t *status)
{
    /* Explicit elapsed delays also bound iteration count if the RTOS is stopped.
     * Transfer latency adds to wall time, so these are conservative driver bounds.
     */
    for (uint32_t elapsed = 0; elapsed <= max_us; elapsed += 10)
    {
        int r = feature(n, 0xc0, status);
        if (r < 0)
            return r;
        if (n->operation.result >= 0)
        {
            n->operation.status       = *status;
            n->operation.status_valid = true;
        }
        if (!(*status & 1))
            return DS35_OK;
        n->bus.delay_us(n->bus.ctx, 10);
    }
    n->fault = true;
    return DS35_TIMEOUT;
}

static int row_command(struct ds35_nand *n, uint8_t op, uint32_t row)
{
    uint8_t h[] = {op, (uint8_t)(row >> 16), (uint8_t)(row >> 8), (uint8_t)row};
    return transfer(n, h, 4, NULL, NULL, 0, 1);
}

static bool block_valid(const struct ds35_nand *n, uint32_t block)
{
    return n->ready && !n->fault && block >= n->first_block &&
           block - n->first_block < n->block_count;
}

static bool page_valid(const struct ds35_nand *n, uint32_t page)
{
    return block_valid(n, page / DS35_PAGES_PER_BLOCK);
}

int ds35_ecc_status(uint8_t status)
{
    switch ((status >> 4) & 7)
    {
        case 0:
            return DS35_OK;
        case 1:
        case 3:
        case 5:
            return DS35_CORRECTED;
        default:
            return DS35_ECC; /* 2 uncorrectable; 4/6/7 reserved, fail closed. */
    }
}

static int page_to_cache(struct ds35_nand *n, uint32_t page, uint8_t *status)
{
    int r = row_command(n, 0x13, page);
    return r < 0 ? r : wait_ready(n, 2000, status); /* tR_ECC max130us; margin. */
}

static int cache_read(struct ds35_nand *n, uint16_t col, uint8_t *buf, size_t size)
{
    if (col > DS35_PAGE + DS35_OOB || size > DS35_PAGE + DS35_OOB - col)
        return DS35_EINVAL;
    uint8_t h[] = {n->bus.data_width == 4 ? 0x6b : 0x03, (uint8_t)(col >> 8), (uint8_t)col, 0};
    return transfer(n, h, 4, NULL, buf, size, n->bus.data_width);
}

static int write_enable(struct ds35_nand *n)
{
    uint8_t status;
    int     r = command(n, 0x06);
    if (r < 0 || (r = feature(n, 0xc0, &status)) < 0)
        return r;
    return (status & 2) ? DS35_OK : DS35_IO;
}

int ds35_probe(struct ds35_nand *n, const struct ds35_bus *bus, uint32_t first, uint32_t count)
{
    if (!n || !bus || !bus->transfer || !bus->delay_us ||
        (bus->data_width != 1 && bus->data_width != 4) ||
        (bus->data_width == 4 && !bus->quad_ready) || (!count && first != 0) ||
        first >= DS35_BLOCKS || count > DS35_BLOCKS - first)
        return DS35_EINVAL;
    memset(n, 0, sizeof(*n));
    n->bus         = *bus;
    n->first_block = first;
    n->block_count = count;
    uint8_t status, id[2], h[] = {0x9f, 0};
    operation_begin(n, "probe-reset/status", first * DS35_PAGES_PER_BLOCK);
    int r = command(n, 0xff);
    if (r < 0 || (r = wait_ready(n, 2000, &status)) < 0)
        return operation_error(n, r);
    operation_stage(n, "probe-JEDEC-ID", first * DS35_PAGES_PER_BLOCK);
    if ((r = transfer(n, h, 2, NULL, id, 2, 1)) < 0)
        return operation_error(n, r);
    if (id[0] != 0xe5 || id[1] != 0xa1)
        return operation_error(n, DS35_ID);
    operation_stage(n, "probe-get-B0/mode-guard", first * DS35_PAGES_PER_BLOCK);
    if ((r = feature(n, 0xb0, &status)) < 0)
        return operation_error(n, r);
    n->operation.observed       = status;
    n->operation.observed_valid = true;
    if (status & 0xc0)
        return operation_error(n, DS35_NOT_READY);
    n->saved_feature = status;
    n->feature_saved = true;
    uint8_t expected = ((status | 0x10) & ~1u) | (bus->data_width == 4 ? 1u : 0u);
    operation_stage(n, "probe-set-B0", first * DS35_PAGES_PER_BLOCK);
    n->operation.expected       = expected;
    n->operation.expected_valid = true;
    n->operation.compare_mask   = DS35_B0_DEFINED_MASK;
    r                           = set_feature(n, 0xb0, expected);
    if (r >= 0)
    {
        n->operation.phase          = "probe-get-B0/setup-check";
        r                           = feature(n, 0xb0, &n->operation.observed);
        n->operation.observed_valid = r >= 0;
    }
    if (r < 0 || !b0_matches(expected, n->operation.observed))
    {
        int error = operation_error(n, r < 0 ? r : DS35_IO);
        /* Healthy readback mismatch may restore saved B0; a faulted transport
         * must not replay. Preserve the setup evidence across this cleanup. */
        if (!n->fault)
            ds35_shutdown(n);
        return error;
    }
    operation_stage(n, "probe-clock-up", first * DS35_PAGES_PER_BLOCK);
    if (bus->data_width == 4 && (r = bus->quad_ready(bus->ctx, true)) < 0)
    {
        n->fault = true;
        return operation_error(n, r);
    }
    n->ready = true;
    return DS35_OK;
}

int ds35_read_status(struct ds35_nand *n, uint8_t *status)
{
    if (!n || !n->ready || n->fault || !status)
        return DS35_NOT_READY;
    return feature(n, 0xc0, status);
}

int ds35_allow_writes(struct ds35_nand *n)
{
    if (!n || !n->ready || n->fault || !n->block_count)
        return DS35_NOT_READY;
    uint8_t protection;
    int     r = feature(n, 0xa0, &protection);
    if (r < 0)
        return r;
    if (protection & 0x80)
        return DS35_READ_ONLY; /* Never override BRWD. */
    /* Explicitly authorized provisioning only: volatile whole-array BP unlock.
     * Every mutating API separately enforces the selected partition boundaries.
     */
    if ((r = set_feature(n, 0xa0, 0)) < 0 || (r = feature(n, 0xa0, &protection)) < 0)
        return r;
    if (protection != 0)
        return DS35_READ_ONLY;
    n->writable = true;
    return DS35_OK;
}

int ds35_read(struct ds35_nand *n, uint32_t page, void *data, size_t bytes, void *tags, size_t tn)
{
    if (!n)
        return DS35_EINVAL;
    operation_begin(n, "read-guard", page);
    if (!page_valid(n, page) || bytes > DS35_PAGE || tn > DS35_TAG_CAPACITY || (tn && !tags))
        return operation_error(n, DS35_EINVAL);
    /* YAFFS tag-only reads pass NULL data with bytes=2048. */
    uint8_t status;
    operation_stage(n, "read-page-to-cache/status", page);
    int r = page_to_cache(n, page, &status);
    if (r < 0)
        return operation_error(n, r);
    int ecc = ds35_ecc_status(status);
    if (ecc < 0)
    {
        n->uncorrectable_reads++;
        return operation_error(n, ecc);
    }
    if (ecc == DS35_CORRECTED)
        n->corrected_reads++;
    size_t len = data ? bytes : 0;
    if (data && tn)
        len = DS35_PAGE + DS35_TAG_OFFSET + tn;
    operation_stage(n, "read-cache-data/tags", page);
    if (len)
    {
        if ((r = cache_read(n, 0, n->page, len)) < 0)
            return operation_error(n, r);
        memcpy(data, n->page, bytes);
        if (tn)
            memcpy(tags, n->page + DS35_PAGE + DS35_TAG_OFFSET, tn);
    }
    else if (tn)
    {
        if ((r = cache_read(n, DS35_PAGE + DS35_TAG_OFFSET, n->page, tn)) < 0)
            return operation_error(n, r);
        memcpy(tags, n->page, tn);
    }
    return ecc;
}

/* Snapshot complete B0 bytes for BBM ECC switching; verify every documented
 * bit, tolerating only reserved readback differences. No delay/retry changes. */
static int bbm_feature(
    struct ds35_nand *n, uint8_t value, uint32_t page, const char *set_phase, const char *get_phase)
{
    bool capture = n->operation.result >= 0;
    operation_stage(n, set_phase, page);
    if (capture)
    {
        n->operation.expected       = value;
        n->operation.expected_valid = true;
        n->operation.compare_mask   = DS35_B0_DEFINED_MASK;
    }
    uint8_t observed;
    int     r = set_feature(n, 0xb0, value);
    if (r >= 0)
    {
        if (capture)
            n->operation.phase = get_phase;
        r = feature(n, 0xb0, &observed);
        if (capture)
        {
            n->operation.observed_valid = r >= 0;
            if (r >= 0)
                n->operation.observed = observed;
        }
    }
    if (r >= 0 && !b0_matches(value, observed))
        r = DS35_IO;
    if (r < 0)
    {
        n->fault = true;
        return operation_error(n, r);
    }
    return DS35_OK;
}

int ds35_check_bad(struct ds35_nand *n, uint32_t block)
{
    if (!n)
        return DS35_EINVAL;
    operation_begin(n, "BBM-guard", block * DS35_PAGES_PER_BLOCK);
    if (!block_valid(n, block))
        return operation_error(n, DS35_EINVAL);
    if (n->bad[block / 8] & (1u << (block % 8)))
        return DS35_BAD;
    if (n->checked[block / 8] & (1u << (block % 8)))
        return DS35_OK;
    /* Table3.7 reserves OOB[0]. Read BOTH first pages RAW, then restore ECC.
     * Never replace the failing command/status with a subsequent NOT_READY.
     * A poisoned transport cannot safely attempt another feature transaction. */
    uint8_t config, status;
    operation_stage(n, "BBM-get-original-B0", block * DS35_PAGES_PER_BLOCK);
    int r = feature(n, 0xb0, &config);
    if (r < 0)
        return operation_error(n, r);
    r = bbm_feature(n,
        config & ~0x10u,
        block * DS35_PAGES_PER_BLOCK,
        "BBM-disable-ECC-set-B0",
        "BBM-disable-ECC-get-B0");
    if (r < 0)
        return r;
    int result = DS35_OK;
    for (uint32_t i = 0; i < 2; i++)
    {
        uint32_t page = block * DS35_PAGES_PER_BLOCK + i;
        operation_stage(n, "BBM-page-to-cache/status", page);
        r = page_to_cache(n, page, &status);
        if (r < 0)
            return operation_error(n, r); /* Faulted: no restore replay. */
        operation_stage(n, "BBM-cache-read-OOB0", page);
        if ((r = cache_read(n, DS35_PAGE, n->page, 1)) < 0)
            return operation_error(n, r);
        if (n->page[0] != 0xff)
        {
            result = DS35_BAD;
            break;
        }
    }
    r = bbm_feature(n,
        config,
        block * DS35_PAGES_PER_BLOCK,
        "BBM-restore-ECC-set-B0",
        "BBM-restore-ECC-get-B0");
    if (r < 0)
        return r; /* A marked block is NOT harmless if restoration failed. */
    if (result == DS35_BAD)
        n->bad[block / 8] |= 1u << (block % 8);
    if (result == DS35_OK)
        n->checked[block / 8] |= 1u << (block % 8);
    return result;
}

int ds35_program(
    struct ds35_nand *n, uint32_t page, const void *data, size_t bytes, const void *tags, size_t tn)
{
    if (!n || !page_valid(n, page) || !data || bytes != DS35_PAGE || !tags || tn == 0 ||
        tn > DS35_TAG_CAPACITY)
        return DS35_EINVAL;
    if (!n->writable)
        return DS35_READ_ONLY;
    int r = ds35_check_bad(n, page / 64);
    if (r < 0)
        return r;
    memset(n->page, 0xff, DS35_PROGRAM_BYTES);
    memcpy(n->page, data, bytes);
    memcpy(n->page + DS35_PAGE + DS35_TAG_OFFSET, tags, tn);
    uint8_t h[] = {n->bus.data_width == 4 ? 0x32 : 0x02, 0, 0}, status;
    if ((r = write_enable(n)) < 0 ||
        (r = transfer(n, h, 3, n->page, NULL, DS35_PROGRAM_BYTES, n->bus.data_width)) < 0 ||
        (r = row_command(n, 0x10, page)) < 0 || (r = wait_ready(n, 5000, &status)) < 0)
        return r;
    return (status & 8) ? DS35_IO : DS35_OK; /* tPROG max700us. */
}

int ds35_erase(struct ds35_nand *n, uint32_t block)
{
    if (!n || !block_valid(n, block))
        return DS35_EINVAL;
    if (!n->writable)
        return DS35_READ_ONLY;
    int     r = ds35_check_bad(n, block);
    uint8_t status;
    if (r < 0)
        return r;
    if ((r = write_enable(n)) < 0 || (r = row_command(n, 0xd8, block * 64)) < 0 ||
        (r = wait_ready(n, 20000, &status)) < 0)
        return r;
    return (status & 4) ? DS35_IO : DS35_OK; /* tBERS max10ms. */
}

int ds35_mark_bad(struct ds35_nand *n, uint32_t block)
{
    if (!n || !block_valid(n, block))
        return DS35_EINVAL;
    if (!n->writable)
        return DS35_READ_ONLY;
    n->bad[block / 8] |= 1u << (block % 8);
    /* A failed used page must not regenerate ECC over existing main data.
     * Disable ECC only while programming reserved marker, restore on all paths.
     */
    uint8_t config, status;
    int     r = feature(n, 0xb0, &config);
    if (r < 0)
        return r;
    if ((r = set_feature(n, 0xb0, config & ~0x10u)) < 0)
        return r;
    if ((r = feature(n, 0xb0, &status)) < 0 || !b0_matches((uint8_t)(config & ~0x10u), status))
    {
        if (!n->fault)
            set_feature(n, 0xb0, config);
        n->fault = true;
        return DS35_IO;
    }
    int success = 0;
    for (uint32_t i = 0; i < 2 && !n->fault; i++)
    {
        uint8_t h[] = {n->bus.data_width == 4 ? 0x32 : 0x02, 0x08, 0x00};
        n->page[0]  = 0;
        if ((r = write_enable(n)) < 0 ||
            (r = transfer(n, h, 3, n->page, NULL, 1, n->bus.data_width)) < 0 ||
            (r = row_command(n, 0x10, block * 64 + i)) < 0 ||
            (r = wait_ready(n, 5000, &status)) < 0)
            break;
        if (!(status & 8))
            success++;
    }
    int restore = set_feature(n, 0xb0, config);
    if (restore < 0 || n->fault)
    {
        n->fault = true;
        return DS35_IO;
    }
    if ((r = feature(n, 0xb0, &status)) < 0 || !b0_matches(config, status))
    {
        n->fault = true;
        return DS35_IO;
    }
    return success ? DS35_OK : DS35_IO;
}

int ds35_shutdown(struct ds35_nand *n)
{
    if (!n)
        return DS35_EINVAL;
    n->writable = false;
    /* Never overwrite the first shutdown failure with a second cleanup call. */
    if (n->shutdown.attempted && n->shutdown.result < 0)
        return n->shutdown.result;
    struct ds35_shutdown_diagnostic *d = &n->shutdown;
    *d = (struct ds35_shutdown_diagnostic){.phase = "shutdown-entry",
        .attempted                                = true,
        .expected_valid                           = n->feature_saved,
        .saved                                    = n->saved_feature,
        .expected                                 = n->saved_feature};
    if (n->fault)
    {
        d->phase  = "shutdown-prior-transport-fault";
        d->result = DS35_NOT_READY;
        return d->result;
    }
    if (n->feature_saved)
    {
        int r;
        /* Restore the known working serial probe clock BEFORE clearing QE.
         * Previously SET/GET B0 still ran at the selected bulk clock (48MHz),
         * and only a successful readback lowered it. This is a conservative
         * sequencing correction, not proof of the watch's failure mechanism.
         * Serial phase selection still happens in the transport per command.
         */
        d->phase = "shutdown-clock-down";
        if (n->bus.data_width == 4 && (r = n->bus.quad_ready(n->bus.ctx, false)) < 0)
        {
            d->result = r;
            n->fault  = true;
            return r;
        }
        d->phase = "shutdown-set-B0";
        r        = set_feature(n, 0xb0, n->saved_feature);
        if (r >= 0)
        {
            d->phase          = "shutdown-get-B0";
            r                 = feature(n, 0xb0, &d->observed);
            d->observed_valid = r >= 0;
        }
        if (r >= 0 && !b0_matches(d->expected, d->observed))
        {
            d->phase = "shutdown-B0-mismatch";
            r        = DS35_IO;
        }
        if (r < 0)
        {
            d->result = r;
            n->fault  = true;
            return r;
        }
        n->feature_saved = false;
    }
    n->ready  = false;
    d->phase  = "shutdown-complete";
    d->result = DS35_OK;
    return DS35_OK;
}
