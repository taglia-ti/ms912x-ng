/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MS912X_DOWNSCALE_2X_SSE_H
#define MS912X_DOWNSCALE_2X_SSE_H

/*
 * PAVGB rounds each pair up. Correct the extra rounding of the second PAVGB:
 * subtract one when a pair sum is odd and the two pair averages differ in parity.
 * This is exactly (a + b + c + d + 2) >> 2, including the X/alpha byte.
 */
static __always_inline __m128i
ms912x_downscale_sse_avg4(__m128i a, __m128i b, __m128i c, __m128i d)
{
	__m128i ab = _mm_avg_epu8(a, b);
	__m128i cd = _mm_avg_epu8(c, d);
	__m128i odd = _mm_or_si128(_mm_xor_si128(a, b), _mm_xor_si128(c, d));
	__m128i correction = _mm_and_si128(odd, _mm_xor_si128(ab, cd));

	correction = _mm_and_si128(correction, _mm_set1_epi8(1));
	return _mm_sub_epi8(_mm_avg_epu8(ab, cd), correction);
}

static __always_inline __m128i
ms912x_downscale_sse_four(const u32 *row0, const u32 *row1)
{
	__m128 r00 = _mm_castsi128_ps(_mm_loadu_si128((const __m128i *)row0));
	__m128 r01 = _mm_castsi128_ps(_mm_loadu_si128((const __m128i *)(row0 + 4)));
	__m128 r10 = _mm_castsi128_ps(_mm_loadu_si128((const __m128i *)row1));
	__m128 r11 = _mm_castsi128_ps(_mm_loadu_si128((const __m128i *)(row1 + 4)));
	__m128i even0 = _mm_castps_si128(_mm_shuffle_ps(r00, r01, _MM_SHUFFLE(2, 0, 2, 0)));
	__m128i odd0 = _mm_castps_si128(_mm_shuffle_ps(r00, r01, _MM_SHUFFLE(3, 1, 3, 1)));
	__m128i even1 = _mm_castps_si128(_mm_shuffle_ps(r10, r11, _MM_SHUFFLE(2, 0, 2, 0)));
	__m128i odd1 = _mm_castps_si128(_mm_shuffle_ps(r10, r11, _MM_SHUFFLE(3, 1, 3, 1)));

	return ms912x_downscale_sse_avg4(even0, even1, odd0, odd1);
}

static __always_inline __m128i
ms912x_downscale_sse_two(const u32 *row0, const u32 *row1)
{
	__m128i a = _mm_loadu_si128((const __m128i *)row0);
	__m128i b = _mm_loadu_si128((const __m128i *)row1);
	__m128i ab = _mm_avg_epu8(a, b);
	__m128i parity = _mm_xor_si128(a, b);
	__m128i adjacent = _mm_shuffle_epi32(ab, _MM_SHUFFLE(2, 3, 0, 1));
	__m128i pair_parity = _mm_shuffle_epi32(parity, _MM_SHUFFLE(2, 3, 0, 1));
	__m128i correction = _mm_and_si128(_mm_or_si128(parity, pair_parity),
					 _mm_xor_si128(ab, adjacent));
	__m128i average;

	correction = _mm_and_si128(correction, _mm_set1_epi8(1));
	average = _mm_sub_epi8(_mm_avg_epu8(ab, adjacent), correction);
	return _mm_shuffle_epi32(average, _MM_SHUFFLE(2, 0, 2, 0));
}
#endif
