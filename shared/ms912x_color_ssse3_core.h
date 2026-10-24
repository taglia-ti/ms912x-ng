/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MS912X_COLOR_SSSE3_CORE_H
#define MS912X_COLOR_SSSE3_CORE_H
#define _MM_MALLOC_H_INCLUDED
#include <tmmintrin.h>
#undef _MM_MALLOC_H_INCLUDED
#include "ms912x_color_sse2_core.h"

#ifdef __KERNEL__
#define MS912X_COLOR_SSSE3_TARGET
#else
#define MS912X_COLOR_SSSE3_TARGET __attribute__((target("ssse3")))
#endif

/* Channel order changes coefficients at compile time, not source pixels. */
static MS912X_COLOR_SSSE3_TARGET inline __attribute__((always_inline)) void
ms912x_color_ssse3_eight(unsigned char *dst, const void *pixels, unsigned int red)
{
	const __m128i br_mask = _mm_set1_epi32(0x00ff00ff);
	const __m128i g_mask = _mm_set1_epi32(0xff);
	const unsigned char *src = pixels;
	__m128i a = _mm_loadu_si128((const __m128i *)src);
	__m128i b = _mm_loadu_si128((const __m128i *)(src + 16));
	__m128i br0 = _mm_and_si128(a, br_mask);
	__m128i br1 = _mm_and_si128(b, br_mask);
	__m128i g0 = _mm_and_si128(_mm_srli_epi32(a, 8), g_mask);
	__m128i g1 = _mm_and_si128(_mm_srli_epi32(b, 8), g_mask);
	__m128i y = _mm_packs_epi32(ms912x_color_sse2_luma(br0, g0, red),
				   ms912x_color_sse2_luma(br1, g1, red));
	/* Pair sums cannot carry between channels: each 16-bit sum is at most 510. */
	__m128i uv = ms912x_color_sse2_chroma(_mm_hadd_epi32(br0, br1),
					   _mm_hadd_epi32(g0, g1), red);
	__m128i packed = _mm_packus_epi16(uv, y);

	packed = _mm_shuffle_epi8(packed,
		_mm_setr_epi8(0, 8, 1, 9, 2, 10, 3, 11, 4, 12, 5, 13, 6, 14, 7, 15));
	_mm_storeu_si128((__m128i *)dst, packed);
}

#endif
