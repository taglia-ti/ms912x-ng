// SPDX-License-Identifier: GPL-2.0-only
#include <asm/fpu/api.h>

#define _MM_MALLOC_H_INCLUDED
#include <emmintrin.h>
#undef _MM_MALLOC_H_INCLUDED

#include "ms912x_downscale_3to2.h"
#include "ms912x_downscale_3to2_sse.h"

#define MS912X_CUBIC_SCOPE static noinline
#define MS912X_CUBIC_WORKER ms912x_downscale_sse2_fpu
#define MS912X_CUBIC_PIXELS 4
#define MS912X_CUBIC_TYPE __m128i
#define MS912X_CUBIC_LOAD(p) _mm_loadu_si128((const __m128i *)(p))
#define MS912X_CUBIC_STORE(p, v) _mm_storeu_si128((__m128i *)(p), (v))
#define MS912X_CUBIC_H(row, x, ox) ms912x_cubic_sse_horizontal((row) + (x) / 2 * 3 + (ox))
#define MS912X_CUBIC_FILTER ms912x_cubic_sse_filter
#define MS912X_CUBIC_FINISH() do { } while (0)
#include "ms912x_downscale_3to2_template.h"

#define MS912X_CUBIC_REGION ms912x_downscale_xrgb8888_3to2_sse2_region
#define MS912X_CUBIC_ENTRY ms912x_downscale_xrgb8888_3to2_sse2
#define MS912X_CUBIC_BEGIN kernel_fpu_begin
#define MS912X_CUBIC_END kernel_fpu_end
#include "ms912x_downscale_3to2_entry.h"
