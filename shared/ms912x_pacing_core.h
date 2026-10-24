/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MS912X_PACING_CORE_H
#define MS912X_PACING_CORE_H
#include "../ms912x_platform.h"
#ifdef __KERNEL__
#include <linux/math64.h>
#endif
static inline u64 ms912x_pacing_div(u64 numerator, u64 denominator)
{
#ifdef __KERNEL__
    return div64_u64(numerator, denominator);
#else
    return numerator / denominator;
#endif
}
/* Linux's running mean, shared with successful userspace USB completions. */
static inline void ms912x_usb_rate_update(u64 *mean, u64 *samples, u64 measured)
{
    if (!*samples) { *mean = measured; *samples = 1; return; }
    if (*samples == ~(u64)0) return;
    if (measured >= *mean) *mean += ms912x_pacing_div(measured - *mean, *samples + 1);
    else *mean -= ms912x_pacing_div(*mean - measured, *samples + 1);
    ++*samples;
}
static inline void ms912x_pacing_watermarks(unsigned int refresh, u64 *low, u64 *high)
{
    u64 denominator = (u64)(refresh ? refresh : 60) * 1000000U;
    *low = ms912x_pacing_div(1000000000ULL, 2 * denominator) * 1000000U;
    *high = ms912x_pacing_div(2000000000ULL + denominator - 1, denominator) * 1000000U;
}
static inline u64 ms912x_pacing_pressure(u64 bytes, u64 rate)
{
    return ms912x_pacing_div(bytes * 1000000000ULL, rate ? rate : 1);
}
#endif
