// SPDX-License-Identifier: GPL-2.0-only

#include <asm/fpu/api.h>
#include <linux/bitops.h>

#define _MM_MALLOC_H_INCLUDED
#include <emmintrin.h>
#undef _MM_MALLOC_H_INCLUDED

#include "ms912x_scan_methods.h"

static __always_inline void
ms912x_scan_row_sse2_blocks(const u32 *old, const u32 *new,
			    unsigned int width,
			    struct ms912x_scan_row_result *result)
{
	unsigned int x = 0;

	ms912x_scan_row_result_init(result, width);
	while (x < width) {
		unsigned int end = min(width, x + 16), vx = x, bits = 0;
		unsigned int changed, mask;

		for (; vx + 4 <= end; vx += 4) {
			__m128i a = _mm_loadu_si128((const __m128i *)&old[vx]);
			__m128i b = _mm_loadu_si128((const __m128i *)&new[vx]);
			bits |= (~(unsigned int)_mm_movemask_ps(
				_mm_castsi128_ps(_mm_cmpeq_epi32(a, b)))) & 0xf;
		}
		for (; vx < end; vx++)
			bits |= old[vx] != new[vx];
		changed = !!bits;
		mask = -changed;
		result->avx2_blocks++;
		result->avx2_equal_blocks += !changed;
		result->changed_pixels += (end - x) * changed;
		result->min_x = min(result->min_x,
				    (x & mask) | (width & ~mask));
		result->max_x = max(result->max_x, (end - 1) & mask);
		x = end;
	}
	result->equal_row = !result->changed_pixels;
}

static __always_inline void
ms912x_scan_row_sse2_relaxed(const u32 *old, const u32 *new, u32 *next,
			     unsigned int width, unsigned int tolerance,
			     struct ms912x_scan_row_result *result)
{
	const __m128i threshold = _mm_set1_epi8((char)tolerance);
	const __m128i rgb_mask = _mm_set1_epi32(0x00ffffff);
	const __m128i zero = _mm_setzero_si128();
	unsigned int x = 0;

	ms912x_scan_row_result_init(result, width);
	for (; x + 4 <= width; x += 4) {
		__m128i a = _mm_loadu_si128((const __m128i *)&old[x]);
		__m128i b = _mm_loadu_si128((const __m128i *)&new[x]);
		__m128i ab = _mm_subs_epu8(a, b);
		__m128i ba = _mm_subs_epu8(b, a);
		__m128i diff = _mm_max_epu8(ab, ba);
		__m128i over = _mm_and_si128(_mm_subs_epu8(diff, threshold),
					     rgb_mask);
		unsigned int bits = (~(unsigned int)_mm_movemask_ps(
			_mm_castsi128_ps(_mm_cmpeq_epi32(over, zero)))) & 0xf;
		unsigned int first = __ffs(bits | 0x10);
		unsigned int last = fls(bits);
		unsigned int has_change = !!bits;
		unsigned int change_mask = -has_change;
		__m128i update = _mm_setr_epi32(-((bits >> 0) & 1),
			-((bits >> 1) & 1), -((bits >> 2) & 1),
			-((bits >> 3) & 1));
		__m128i merged = _mm_or_si128(_mm_andnot_si128(update, a),
					      _mm_and_si128(update, b));

		_mm_storeu_si128((__m128i *)&next[x], merged);
		result->changed_pixels += hweight8(bits);
		result->min_x = min(result->min_x,
				    ((x + first) & change_mask) |
				    (width & ~change_mask));
		result->max_x = max(result->max_x,
				    (x + last - 1) & -has_change);
	}
	for (; x < width; x++) {
		unsigned int changed = ms912x_scan_pixel_relaxed_changed(
			old[x], new[x], tolerance);
		unsigned int mask = -changed;

		result->changed_pixels += changed;
		result->min_x = min(result->min_x,
				    (x & mask) | (width & ~mask));
		result->max_x = max(result->max_x, x & mask);
		next[x] = (old[x] & ~mask) | (new[x] & mask);
	}
	result->equal_row = !result->changed_pixels;
}

void ms912x_scan_rows_sse2_blocks(const u32 *old, const u32 *new,
				  unsigned int width, unsigned int row_stride,
				  unsigned int row_count,
				  struct ms912x_scan_row_result *results)
{
	unsigned int row;

	kernel_fpu_begin();
	for (row = 0; row < row_count; row++)
		ms912x_scan_row_sse2_blocks(old + row * row_stride,
					    new + row * row_stride, width,
					    &results[row]);
	kernel_fpu_end();
}

void ms912x_scan_rows_sse2_relaxed(const u32 *old, const u32 *new,
				   u32 *next, unsigned int width,
				   unsigned int row_stride, unsigned int row_count,
				   unsigned int tolerance,
				   struct ms912x_scan_row_result *results)
{
	unsigned int row;

	kernel_fpu_begin();
	for (row = 0; row < row_count; row++)
		ms912x_scan_row_sse2_relaxed(old + row * row_stride,
					     new + row * row_stride,
					     next + row * row_stride, width,
					     tolerance, &results[row]);
	kernel_fpu_end();
}
