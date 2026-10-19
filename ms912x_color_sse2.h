/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MS912X_COLOR_SSE2_H
#define MS912X_COLOR_SSE2_H

#define _MM_MALLOC_H_INCLUDED
#include <emmintrin.h>
#undef _MM_MALLOC_H_INCLUDED

#include "ms912x_color.h"

/* One input register contains 4 XRGB pixels; output is 8 UYVY bytes. */
static __always_inline void
ms912x_sse2_convert_four(u8 *dst, const __le32 *pixels)
{
	const __m128i br_mask = _mm_set1_epi32(0x00ff00ff);
	const __m128i g_mask = _mm_set1_epi32(0xff);
	const __m128i y_br = _mm_setr_epi16(
		6391, 16763, 6391, 16763, 6391, 16763, 6391, 16763);
	const __m128i y_g = _mm_set1_epi32((u16)-32632);
	const __m128i u_br = _mm_setr_epi16(
		28672, -9676, 28672, -9676, 28672, -9676, 28672, -9676);
	const __m128i u_g = _mm_set1_epi32((u16)-18996);
	const __m128i v_br = _mm_setr_epi16(
		-4663, 28672, -4663, 28672, -4663, 28672, -4663, 28672);
	const __m128i v_g = _mm_set1_epi32((u16)-24009);
	const __m128i uv_bias = _mm_set1_epi32(128 << 17);
	__m128i packed = _mm_loadu_si128((const __m128i *)pixels);
	__m128i br = _mm_and_si128(packed, br_mask);
	__m128i g = _mm_and_si128(_mm_srli_epi32(packed, 8), g_mask);
	__m128i y, u, v;

	/* 32904 * G = -32632 * G + (G << 16), exactly in Q16. */
	y = _mm_add_epi32(_mm_madd_epi16(br, y_br),
			     _mm_madd_epi16(g, y_g));
	y = _mm_add_epi32(y, _mm_slli_epi32(
		_mm_add_epi32(g, _mm_set1_epi32(16)), 16));
	y = _mm_srli_epi32(y, 16);

	/* Each even dword holds B/R or G sums for one adjacent pixel pair. */
	br = _mm_add_epi16(br, _mm_srli_epi64(br, 32));
	g = _mm_add_epi16(g, _mm_srli_epi64(g, 32));
	u = _mm_add_epi32(_mm_madd_epi16(br, u_br),
			     _mm_madd_epi16(g, u_g));
	v = _mm_add_epi32(_mm_madd_epi16(br, v_br),
			     _mm_madd_epi16(g, v_g));
	u = _mm_srli_epi32(_mm_add_epi32(u, uv_bias), 17);
	v = _mm_srli_epi32(_mm_add_epi32(v, uv_bias), 17);

	/* Assemble U0 Y0 V0 Y1 in even dwords, entirely in SIMD registers. */
	packed = _mm_or_si128(u, _mm_slli_epi32(v, 16));
	packed = _mm_or_si128(packed, _mm_slli_epi32(y, 8));
	packed = _mm_or_si128(packed, _mm_srli_epi64(y, 8));
	packed = _mm_shuffle_epi32(packed, _MM_SHUFFLE(2, 0, 2, 0));
	_mm_storel_epi64((__m128i *)dst, packed);
}

#endif /* MS912X_COLOR_SSE2_H */
