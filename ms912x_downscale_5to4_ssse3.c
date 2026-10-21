// SPDX-License-Identifier: GPL-2.0-only
#include <asm/fpu/api.h>

#define _MM_MALLOC_H_INCLUDED
#include <tmmintrin.h>
#undef _MM_MALLOC_H_INCLUDED

#include "ms912x_downscale_5to4.h"
#include "ms912x_downscale_5to4_sse.h"

static __always_inline __m128i
ms912x_5to4_ssse3_pairs(__m128i ac, __m128i bd, __m128i no, __m128i cf)
{
	__m128i sum = _mm_sub_epi16(_mm_maddubs_epi16(ac, no),
				   _mm_set1_epi16(16384));

	sum = _mm_add_epi16(sum, _mm_maddubs_epi16(bd, cf));
	sum = _mm_srai_epi16(_mm_add_epi16(sum, _mm_set1_epi16(64)), 7);
	return _mm_add_epi16(sum, _mm_set1_epi16(128));
}

static __always_inline __m128i
ms912x_5to4_ssse3_filter(__m128i a, __m128i b, __m128i c, __m128i d,
			 unsigned int inner)
{
	unsigned int n = 123 - 30 * inner, w = 12 + 38 * inner;
	unsigned int o = 6 + 3 * inner, f = 1 + 5 * inner;
	__m128i no = _mm_set1_epi16((short)(n | ((256 - o) << 8)));
	__m128i cf = _mm_set1_epi16((short)(w | ((256 - f) << 8)));
	__m128i lo = ms912x_5to4_ssse3_pairs(_mm_unpacklo_epi8(a, c),
						_mm_unpacklo_epi8(b, d), no, cf);
	__m128i hi = ms912x_5to4_ssse3_pairs(_mm_unpackhi_epi8(a, c),
						_mm_unpackhi_epi8(b, d), no, cf);

	return _mm_packus_epi16(lo, hi);
}

static __always_inline __m128i ms912x_5to4_ssse3_horizontal(const u32 *row)
{
	__m128i a, b, c, d, no, cf, lo, hi;

	ms912x_5to4_sse_gather(row, &a, &b, &c, &d);
	no = _mm_set_epi32((int)0xf75df75d, (int)0xf75df75d,
			   (int)0xfa7bfa7b, (int)0xfa7bfa7b);
	cf = _mm_set_epi32((int)0xfa32fa32, (int)0xfa32fa32,
			   (int)0xff0cff0c, (int)0xff0cff0c);
	lo = ms912x_5to4_ssse3_pairs(_mm_unpacklo_epi8(a, c),
				     _mm_unpacklo_epi8(b, d), no, cf);
	hi = ms912x_5to4_ssse3_pairs(_mm_unpackhi_epi8(a, c),
				     _mm_unpackhi_epi8(b, d),
				     _mm_shuffle_epi32(no, _MM_SHUFFLE(1, 0, 3, 2)),
				     _mm_shuffle_epi32(cf, _MM_SHUFFLE(1, 0, 3, 2)));
	return _mm_packus_epi16(lo, hi);
}

#define MS912X_5TO4_SCOPE static noinline
#define MS912X_5TO4_WORKER ms912x_5to4_ssse3_rows
#define MS912X_5TO4_PIXELS 4
#define MS912X_5TO4_TYPE __m128i
#define MS912X_5TO4_LOAD(p) _mm_loadu_si128((const __m128i *)(p))
#define MS912X_5TO4_STORE(p, v) _mm_storeu_si128((__m128i *)(p), (v))
#define MS912X_5TO4_H(row, x, ox) ms912x_5to4_ssse3_horizontal((row) + (x) / 4 * 5 + (ox))
#define MS912X_5TO4_A(a, b, c, d) ms912x_5to4_ssse3_filter(a, b, c, d, 0)
#define MS912X_5TO4_B(a, b, c, d) ms912x_5to4_ssse3_filter(a, b, c, d, 1)
#define MS912X_5TO4_FINISH() do { } while (0)
#include "ms912x_downscale_5to4_template.h"

#define MS912X_5TO4_REGION ms912x_downscale_xrgb8888_5to4_ssse3_region
#define MS912X_5TO4_ENTRY ms912x_downscale_xrgb8888_5to4_ssse3
#define MS912X_5TO4_BEGIN kernel_fpu_begin
#define MS912X_5TO4_END kernel_fpu_end
#include "ms912x_downscale_5to4_entry.h"
