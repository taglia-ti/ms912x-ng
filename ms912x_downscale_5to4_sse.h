/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MS912X_DOWNSCALE_5TO4_SSE_H
#define MS912X_DOWNSCALE_5TO4_SSE_H

static __always_inline __m128i ms912x_5to4_sse_words(__m128i a, __m128i b,
		__m128i c, __m128i d, __m128i n, __m128i w, __m128i o, __m128i f)
{
	__m128i bias = _mm_set1_epi16(128), sum;

	a = _mm_sub_epi16(a, bias);
	b = _mm_sub_epi16(b, bias);
	c = _mm_sub_epi16(c, bias);
	d = _mm_sub_epi16(d, bias);
	sum = _mm_add_epi16(_mm_mullo_epi16(a, n), _mm_mullo_epi16(b, w));
	sum = _mm_sub_epi16(sum, _mm_mullo_epi16(c, o));
	sum = _mm_sub_epi16(sum, _mm_mullo_epi16(d, f));
	sum = _mm_srai_epi16(_mm_add_epi16(sum, _mm_set1_epi16(64)), 7);
	return _mm_add_epi16(sum, bias);
}

static __always_inline __m128i ms912x_5to4_sse_filter(__m128i a, __m128i b,
						 __m128i c, __m128i d, unsigned int inner)
{
	__m128i zero = _mm_setzero_si128();
	__m128i n = _mm_set1_epi16(123 - 30 * inner), w = _mm_set1_epi16(12 + 38 * inner);
	__m128i o = _mm_set1_epi16(6 + 3 * inner), f = _mm_set1_epi16(1 + 5 * inner);
	__m128i lo = ms912x_5to4_sse_words(_mm_unpacklo_epi8(a, zero),
		_mm_unpacklo_epi8(b, zero), _mm_unpacklo_epi8(c, zero),
		_mm_unpacklo_epi8(d, zero), n, w, o, f);
	__m128i hi = ms912x_5to4_sse_words(_mm_unpackhi_epi8(a, zero),
		_mm_unpackhi_epi8(b, zero), _mm_unpackhi_epi8(c, zero),
		_mm_unpackhi_epi8(d, zero), n, w, o, f);

	return _mm_packus_epi16(lo, hi);
}

static __always_inline void ms912x_5to4_sse_gather(const u32 *row,
		__m128i *near, __m128i *center, __m128i *outer, __m128i *far)
{
	__m128 a = _mm_castsi128_ps(_mm_loadu_si128((const __m128i *)(row - 1)));
	__m128 b = _mm_castsi128_ps(_mm_loadu_si128((const __m128i *)(row + 2)));

	*near = _mm_castps_si128(_mm_shuffle_ps(a, b, _MM_SHUFFLE(2, 1, 2, 1)));
	*center = _mm_castps_si128(_mm_shuffle_ps(a, b, _MM_SHUFFLE(1, 0, 3, 2)));
	*outer = _mm_castps_si128(_mm_shuffle_ps(a, b, _MM_SHUFFLE(3, 2, 1, 0)));
	*far = _mm_castps_si128(_mm_shuffle_ps(b, a, _MM_SHUFFLE(3, 2, 1, 0)));
}

static __always_inline __m128i ms912x_5to4_sse_horizontal(const u32 *row)
{
	__m128i a, b, c, d, zero = _mm_setzero_si128();
	__m128i n = _mm_set_epi32(0x005d005d, 0x005d005d, 0x007b007b, 0x007b007b);
	__m128i w = _mm_set_epi32(0x00320032, 0x00320032, 0x000c000c, 0x000c000c);
	__m128i o = _mm_set_epi32(0x00090009, 0x00090009, 0x00060006, 0x00060006);
	__m128i f = _mm_set_epi32(0x00060006, 0x00060006, 0x00010001, 0x00010001);
	__m128i lo, hi;

	ms912x_5to4_sse_gather(row, &a, &b, &c, &d);
	lo = ms912x_5to4_sse_words(_mm_unpacklo_epi8(a, zero),
		_mm_unpacklo_epi8(b, zero), _mm_unpacklo_epi8(c, zero),
		_mm_unpacklo_epi8(d, zero), n, w, o, f);
	hi = ms912x_5to4_sse_words(_mm_unpackhi_epi8(a, zero),
		_mm_unpackhi_epi8(b, zero), _mm_unpackhi_epi8(c, zero),
		_mm_unpackhi_epi8(d, zero),
		_mm_shuffle_epi32(n, _MM_SHUFFLE(1, 0, 3, 2)),
		_mm_shuffle_epi32(w, _MM_SHUFFLE(1, 0, 3, 2)),
		_mm_shuffle_epi32(o, _MM_SHUFFLE(1, 0, 3, 2)),
		_mm_shuffle_epi32(f, _MM_SHUFFLE(1, 0, 3, 2)));
	return _mm_packus_epi16(lo, hi);
}

#endif /* MS912X_DOWNSCALE_5TO4_SSE_H */
