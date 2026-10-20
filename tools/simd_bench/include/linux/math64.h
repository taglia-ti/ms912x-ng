/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SIMD_BENCH_LINUX_MATH64_H
#define SIMD_BENCH_LINUX_MATH64_H

#include <linux/types.h>

typedef int32_t s32;
typedef int64_t s64;

static inline s64 div_s64(s64 dividend, s32 divisor)
{
	return dividend / divisor;
}

#endif