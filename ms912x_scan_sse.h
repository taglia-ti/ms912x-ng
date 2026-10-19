/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MS912X_SCAN_SSE_H
#define MS912X_SCAN_SSE_H

#define _MM_MALLOC_H_INCLUDED
#ifdef MS912X_SCAN_SSE41
#include <smmintrin.h>
#else
#include <emmintrin.h>
#endif
#undef _MM_MALLOC_H_INCLUDED

#include "ms912x_scan_methods.h"

static __always_inline unsigned int ms912x_scan_sse_nonzero(__m128i diff)
{
#ifdef MS912X_SCAN_SSE41
	return !_mm_testz_si128(diff, diff);
#else
	return _mm_movemask_ps(_mm_castsi128_ps(_mm_cmpeq_epi32(diff, _mm_setzero_si128()))) != 0xf;
#endif
}

static __always_inline __m128i ms912x_scan_sse_difference(const u32 *old, const u32 *new)
{
	return _mm_xor_si128(_mm_loadu_si128((const __m128i *)old),
			     _mm_loadu_si128((const __m128i *)new));
}

/* Reduce vector differences once per damage block, preserving its granularity. */
static __always_inline void ms912x_scan_row_sse_blocks(const u32 *old, const u32 *new,
						       unsigned int width,
						       struct ms912x_scan_row_result *result)
{
	unsigned int x = 0;

	ms912x_scan_row_result_init(result, width);
	for (; x + 16 <= width; x += 16) {
		__m128i d0 = ms912x_scan_sse_difference(old + x, new + x);
		__m128i d1 = ms912x_scan_sse_difference(old + x + 4, new + x + 4);
		__m128i d2 = ms912x_scan_sse_difference(old + x + 8, new + x + 8);
		__m128i d3 = ms912x_scan_sse_difference(old + x + 12, new + x + 12);
		__m128i diff;

		diff = _mm_or_si128(_mm_or_si128(d0, d1), _mm_or_si128(d2, d3));
		ms912x_scan_record_block(result, width, x, x + 16, ms912x_scan_sse_nonzero(diff));
	}
	if (x < width) {
		__m128i diff = _mm_setzero_si128();
		unsigned int vx = x, changed = 0;

		for (; vx + 4 <= width; vx += 4)
			diff = _mm_or_si128(diff, ms912x_scan_sse_difference(old + vx, new + vx));
		for (; vx < width; vx++)
			changed |= old[vx] != new[vx];
		changed |= ms912x_scan_sse_nonzero(diff);
		ms912x_scan_record_block(result, width, x, width, changed);
	}
	result->equal_row = !result->changed_pixels;
}

static __always_inline __m128i ms912x_scan_sse_min(__m128i a, __m128i b)
{
#ifdef MS912X_SCAN_SSE41
	return _mm_min_epu32(a, b);
#else
	/* Positions fit in signed 16 bits; the upper word of each lane is zero. */
	return _mm_min_epi16(a, b);
#endif
}

static __always_inline __m128i ms912x_scan_sse_max(__m128i a, __m128i b)
{
#ifdef MS912X_SCAN_SSE41
	return _mm_max_epu32(a, b);
#else
	return _mm_max_epi16(a, b);
#endif
}

static __always_inline void ms912x_scan_row_sse_relaxed(const u32 *old, const u32 *new, u32 *next,
							unsigned int width, unsigned int tolerance,
							struct ms912x_scan_row_result *result)
{
	const __m128i threshold = _mm_set1_epi8((char)tolerance);
	const __m128i rgb_mask = _mm_set1_epi32(0x00ffffff);
	const __m128i zero = _mm_setzero_si128();
	const __m128i one = _mm_set1_epi32(1);
	__m128i counts = zero, first = _mm_set1_epi32(width), last = zero;
	__m128i positions = _mm_setr_epi32(0, 1, 2, 3);
	unsigned int x = 0;

#ifndef MS912X_SCAN_SSE41
	/* Driver rows are at most 1920 pixels. Keep the helper safe for wider rows. */
	if (unlikely(width > 32767)) {
		ms912x_scan_row_scalar_relaxed(old, new, next, width, tolerance, result);
		return;
	}
#endif
	ms912x_scan_row_result_init(result, width);
	for (; x + 4 <= width; x += 4) {
		__m128i a = _mm_loadu_si128((const __m128i *)&old[x]);
		__m128i b = _mm_loadu_si128((const __m128i *)&new[x]);
		__m128i ab = _mm_subs_epu8(a, b);
		__m128i ba = _mm_subs_epu8(b, a);
		__m128i diff = _mm_max_epu8(ab, ba);
		__m128i over = _mm_and_si128(_mm_subs_epu8(diff, threshold), rgb_mask);
		__m128i equal = _mm_cmpeq_epi32(over, zero);
		__m128i merged, candidate;

#ifdef MS912X_SCAN_SSE41
		merged = _mm_blendv_epi8(b, a, equal);
		/* Equal lanes become UINT_MAX and cannot lower the first position. */
		candidate = _mm_or_si128(positions, equal);
#else
		merged = _mm_or_si128(_mm_and_si128(equal, a), _mm_andnot_si128(equal, b));
		/* Equal lanes become 32767, with a zero upper word. */
		candidate = _mm_or_si128(positions, _mm_srli_epi32(equal, 17));
#endif
		_mm_storeu_si128((__m128i *)&next[x], merged);
		counts = _mm_add_epi32(counts, _mm_andnot_si128(equal, one));
		first = ms912x_scan_sse_min(first, candidate);
		last = ms912x_scan_sse_max(last, _mm_andnot_si128(equal, positions));
		positions = _mm_add_epi32(positions, _mm_set1_epi32(4));
	}
	/* Reduce counts and bounds once per row, without per-vector bit scans. */
	counts = _mm_add_epi32(counts, _mm_shuffle_epi32(counts, _MM_SHUFFLE(1, 0, 3, 2)));
	counts = _mm_add_epi32(counts, _mm_shuffle_epi32(counts, _MM_SHUFFLE(2, 3, 0, 1)));
	first = ms912x_scan_sse_min(first, _mm_shuffle_epi32(first, _MM_SHUFFLE(1, 0, 3, 2)));
	first = ms912x_scan_sse_min(first, _mm_shuffle_epi32(first, _MM_SHUFFLE(2, 3, 0, 1)));
	last = ms912x_scan_sse_max(last, _mm_shuffle_epi32(last, _MM_SHUFFLE(1, 0, 3, 2)));
	last = ms912x_scan_sse_max(last, _mm_shuffle_epi32(last, _MM_SHUFFLE(2, 3, 0, 1)));
	result->changed_pixels = _mm_cvtsi128_si32(counts);
	result->min_x = _mm_cvtsi128_si32(first);
	result->max_x = _mm_cvtsi128_si32(last);
	for (; x < width; x++) {
		unsigned int changed = ms912x_scan_pixel_relaxed_changed(old[x], new[x], tolerance);
		unsigned int mask = -changed;

		result->changed_pixels += changed;
		result->min_x = min(result->min_x, (x & mask) | (width & ~mask));
		result->max_x = max(result->max_x, x & mask);
		next[x] = (old[x] & ~mask) | (new[x] & mask);
	}
	result->equal_row = !result->changed_pixels;
}

#endif /* MS912X_SCAN_SSE_H */
