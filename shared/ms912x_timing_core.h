/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MS912X_TIMING_CORE_H
#define MS912X_TIMING_CORE_H
#include "ms912x_modes_core.h"
#include "../ms912x_protocol_values.h"
#include "../ms912x_limits.h"
#ifdef __KERNEL__
#include <linux/errno.h>
#include <linux/math64.h>
#include <linux/string.h>
#else
#include <errno.h>
#include <string.h>
#endif

static inline int ms912x_mode_timing_valid(const struct ms912x_mode *mode)
{
    return mode->clock_khz && mode->htotal > mode->width && mode->vtotal > mode->height &&
        mode->hoffset <= mode->htotal && mode->voffset <= mode->vtotal &&
        mode->hsync_width && mode->vsync_width && mode->hsync_width <= mode->hoffset &&
        mode->vsync_width <= mode->voffset && mode->htotal - mode->hoffset >= mode->width &&
        mode->vtotal - mode->voffset >= mode->height;
}
static inline unsigned int ms912x_timing_le16(const unsigned char *p)
{
    return p[0] | (unsigned int)p[1] << 8;
}
/* Decode the adapter's 22-byte flash record using the Linux validation rules. */
static inline int ms912x_custom_timing_decode(struct ms912x_mode *mode,
                                            const unsigned char record[MS912X_CUSTOM_TIMING_RECORD_SIZE])
{
    struct ms912x_mode decoded = {0};
    unsigned int frequency = ms912x_timing_le16(record + 12);
    unsigned long long denominator, refresh;
    decoded.vic = record[0]; decoded.polarity = record[1];
    decoded.htotal = ms912x_timing_le16(record + 2);
    decoded.vtotal = ms912x_timing_le16(record + 4);
    decoded.width = ms912x_timing_le16(record + 6);
    decoded.height = ms912x_timing_le16(record + 8);
    decoded.clock_khz = ms912x_timing_le16(record + 10) * 10U;
    decoded.hoffset = ms912x_timing_le16(record + 14);
    decoded.voffset = ms912x_timing_le16(record + 16);
    decoded.hsync_width = ms912x_timing_le16(record + 18);
    decoded.vsync_width = ms912x_timing_le16(record + 20);
    if (!decoded.width || decoded.width > MS912X_MAX_WIDTH || !decoded.height ||
        decoded.height > MS912X_MAX_HEIGHT || !frequency || !ms912x_mode_timing_valid(&decoded))
        return -EINVAL;
    denominator = (unsigned long long)decoded.htotal * decoded.vtotal;
    refresh = (unsigned long long)decoded.clock_khz * 1000U;
    if (!(decoded.polarity & MS912X_TIMING_PROGRESSIVE)) refresh *= 2;
    decoded.hz = (frequency + 50U) / 100U;
#ifdef __KERNEL__
    refresh = div64_u64(refresh + denominator / 2, denominator);
#else
    refresh = (refresh + denominator / 2) / denominator;
#endif
    if (!decoded.hz || refresh != (unsigned int)decoded.hz)
        return -EINVAL;
    *mode = decoded;
    return 0;
}
struct ms912x_timing_io {
    int (*read)(void *context, unsigned int address);
    int (*flash)(void *context, unsigned int address, void *data, unsigned int length);
};
static inline int ms912x_custom_timing_base(void *context, const struct ms912x_timing_io *ops)
{
    const unsigned int addresses[2] = {MS913X_REG_CHIP_ID, MS912X_REG_CHIP_ID};
    const unsigned int signatures[2] = {MS913X_CHIP_ID_SIGNATURE_MSB, MS912X_CHIP_ID_SIGNATURE_MSB};
    const unsigned int bases[2] = {MS913X_CUSTOM_TIMING_BASE, MS912X_CUSTOM_TIMING_BASE};
    unsigned int chip, i;
    for (chip = 0; chip < 2; ++chip) {
        unsigned char signature[3];
        for (i = 0; i < 3; ++i) {
            int ret = ops->read(context, addresses[chip] + i);
            if (ret < 0) return ret;
            signature[i] = ret;
        }
        if (signature[1] == signatures[chip] && signature[2] == MS91XX_CHIP_ID_SIGNATURE_LSB)
            return bases[chip];
    }
    return -ENODEV;
}
static inline int ms912x_custom_timing_load(void *context, const struct ms912x_timing_io *ops,
                                          struct ms912x_mode modes[2])
{
    unsigned char marker[7], record[MS912X_CUSTOM_TIMING_RECORD_SIZE];
    unsigned int i, records, count = 0;
    int base = ms912x_custom_timing_base(context, ops), ret, error = 0;
    if (base < 0) return base;
    ret = ops->flash(context, base, marker, sizeof(marker));
    if (ret) return ret;
    if (!memcmp(marker, "modify1", 7)) records = 1;
    else if (!memcmp(marker, "modify2", 7)) records = 2;
    else return 0;
    for (i = 0; i < records; ++i) {
        ret = ops->flash(context, base + MS912X_CUSTOM_TIMING_OFFSET +
                         i * MS912X_CUSTOM_TIMING_STRIDE, record, sizeof(record));
        if (!ret) ret = ms912x_custom_timing_decode(&modes[count], record);
        if (ret) error = ret;
        else ++count;
    }
    return count ? (int)count : error;
}
#endif
