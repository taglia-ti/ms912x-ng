// SPDX-License-Identifier: GPL-2.0-only
#include <asm/fpu/api.h>

#define _MM_MALLOC_H_INCLUDED
#include <emmintrin.h>
#undef _MM_MALLOC_H_INCLUDED

#include "ms912x_downscale_5to4.h"
#include "ms912x_downscale_5to4_sse.h"

#define MS912X_5TO4_SCOPE static noinline
#define MS912X_5TO4_WORKER ms912x_5to4_sse2_rows
#define MS912X_5TO4_PIXELS 4
#define MS912X_5TO4_TYPE __m128i
#define MS912X_5TO4_LOAD(p) _mm_loadu_si128((const __m128i *)(p))
#define MS912X_5TO4_STORE(p, v) _mm_storeu_si128((__m128i *)(p), (v))
#define MS912X_5TO4_H(row, x, ox) ms912x_5to4_sse_horizontal((row) + (x) / 4 * 5 + (ox))
#define MS912X_5TO4_A(a, b, c, d) ms912x_5to4_sse_filter(a, b, c, d, 0)
#define MS912X_5TO4_B(a, b, c, d) ms912x_5to4_sse_filter(a, b, c, d, 1)
#define MS912X_5TO4_FINISH() do { } while (0)
#include "ms912x_downscale_5to4_template.h"

#define MS912X_5TO4_REGION ms912x_downscale_xrgb8888_5to4_sse2_region
#define MS912X_5TO4_BEGIN kernel_fpu_begin
#define MS912X_5TO4_END kernel_fpu_end
#include "ms912x_downscale_5to4_entry.h"
