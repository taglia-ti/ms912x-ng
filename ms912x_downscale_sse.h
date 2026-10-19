/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MS912X_DOWNSCALE_SSE_H
#define MS912X_DOWNSCALE_SSE_H

static __always_inline __m128i
ms912x_cubic_sse_words(__m128i a, __m128i b, __m128i c, __m128i d)
{
	__m128i bias = _mm_set1_epi16(128);
	__m128i sum;

	/* Center at zero: even the negative-lobe sums fit signed 16-bit lanes. */
	a = _mm_sub_epi16(a, bias);
	b = _mm_sub_epi16(b, bias);
	c = _mm_sub_epi16(c, bias);
	d = _mm_sub_epi16(d, bias);
	sum = _mm_add_epi16(_mm_mullo_epi16(a, _mm_set1_epi16(111)),
			    _mm_mullo_epi16(b, _mm_set1_epi16(29)));
	sum = _mm_sub_epi16(sum, _mm_mullo_epi16(c, _mm_set1_epi16(9)));
	sum = _mm_sub_epi16(sum, _mm_mullo_epi16(d, _mm_set1_epi16(3)));
	sum = _mm_srai_epi16(_mm_add_epi16(sum, _mm_set1_epi16(64)), 7);
	return _mm_add_epi16(sum, bias);
}

static __always_inline __m128i
ms912x_cubic_sse_filter(__m128i a, __m128i b, __m128i c, __m128i d)
{
	__m128i zero = _mm_setzero_si128();
	__m128i lo = ms912x_cubic_sse_words(_mm_unpacklo_epi8(a, zero),
		_mm_unpacklo_epi8(b, zero), _mm_unpacklo_epi8(c, zero),
		_mm_unpacklo_epi8(d, zero));
	__m128i hi = ms912x_cubic_sse_words(_mm_unpackhi_epi8(a, zero),
		_mm_unpackhi_epi8(b, zero), _mm_unpackhi_epi8(c, zero),
		_mm_unpackhi_epi8(d, zero));

	return _mm_packus_epi16(lo, hi);
}

static __always_inline void
ms912x_cubic_sse_gather(const u32 *row, __m128i *near, __m128i *center,
			__m128i *outer9, __m128i *outer3)
{
	__m128 a = _mm_castsi128_ps(_mm_loadu_si128((const __m128i *)(row - 1)));
	__m128 b = _mm_castsi128_ps(_mm_loadu_si128((const __m128i *)(row + 3)));
	__m128i cross;

	/* Four outputs centered at 0.25, 1.75, 3.25, 4.75 in the source. */
	*near = _mm_castps_si128(_mm_shuffle_ps(a, b, _MM_SHUFFLE(2, 0, 3, 1)));
	*center = _mm_castps_si128(_mm_shuffle_ps(a, b, _MM_SHUFFLE(1, 1, 2, 2)));
	cross = _mm_castps_si128(_mm_shuffle_ps(a, b, _MM_SHUFFLE(3, 0, 3, 0)));
	*outer9 = _mm_shuffle_epi32(cross, _MM_SHUFFLE(3, 1, 2, 0));
	*outer3 = _mm_castps_si128(_mm_shuffle_ps(a, b, _MM_SHUFFLE(0, 2, 1, 3)));
}

static __always_inline __m128i ms912x_cubic_sse_horizontal(const u32 *row)
{
	__m128i a, b, c, d;

	ms912x_cubic_sse_gather(row, &a, &b, &c, &d);
	return ms912x_cubic_sse_filter(a, b, c, d);
}
#endif
