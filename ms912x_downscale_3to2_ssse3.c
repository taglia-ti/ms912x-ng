// SPDX-License-Identifier: GPL-2.0-only
#include <asm/fpu/api.h>

#define _MM_MALLOC_H_INCLUDED
#include <tmmintrin.h>
#undef _MM_MALLOC_H_INCLUDED

#include "ms912x_downscale_3to2.h"
#include "ms912x_downscale_3to2_sse.h"

static __always_inline __m128i
ms912x_cubic_ssse3_pairs(__m128i ac, __m128i bd)
{
	__m128i near_outer = _mm_maddubs_epi16(ac, _mm_set1_epi16((short)0xf76f));
	__m128i center_outer = _mm_maddubs_epi16(bd, _mm_set1_epi16((short)0xfd1d));
	__m128i sum = _mm_sub_epi16(near_outer, _mm_set1_epi16(16384));

	sum = _mm_add_epi16(sum, center_outer);
	sum = _mm_srai_epi16(_mm_add_epi16(sum, _mm_set1_epi16(64)), 7);
	return _mm_add_epi16(sum, _mm_set1_epi16(128));
}

static __always_inline __m128i
ms912x_cubic_ssse3_filter(__m128i a, __m128i b, __m128i c, __m128i d)
{
	__m128i lo = ms912x_cubic_ssse3_pairs(_mm_unpacklo_epi8(a, c),
						_mm_unpacklo_epi8(b, d));
	__m128i hi = ms912x_cubic_ssse3_pairs(_mm_unpackhi_epi8(a, c),
						_mm_unpackhi_epi8(b, d));

	return _mm_packus_epi16(lo, hi);
}

static __always_inline __m128i
ms912x_cubic_ssse3_horizontal(const u32 *row)
{
	__m128i a, b, c, d;

	ms912x_cubic_sse_gather(row, &a, &b, &c, &d);
	return ms912x_cubic_ssse3_filter(a, b, c, d);
}

#define MS912X_CUBIC_SCOPE static noinline
#define MS912X_CUBIC_WORKER ms912x_downscale_ssse3_fpu
#define MS912X_CUBIC_PIXELS 4
#define MS912X_CUBIC_TYPE __m128i
#define MS912X_CUBIC_LOAD(p) _mm_loadu_si128((const __m128i *)(p))
#define MS912X_CUBIC_STORE(p, v) _mm_storeu_si128((__m128i *)(p), (v))
#define MS912X_CUBIC_H(row, x, ox) ms912x_cubic_ssse3_horizontal((row) + (x) / 2 * 3 + (ox))
#define MS912X_CUBIC_FILTER ms912x_cubic_ssse3_filter
#define MS912X_CUBIC_FINISH() do { } while (0)
#include "ms912x_downscale_3to2_template.h"

#define MS912X_CUBIC_REGION ms912x_downscale_xrgb8888_3to2_ssse3_region
#define MS912X_CUBIC_BEGIN kernel_fpu_begin
#define MS912X_CUBIC_END kernel_fpu_end
#include "ms912x_downscale_3to2_entry.h"
