// SPDX-License-Identifier: GPL-2.0-only

#include <asm/fpu/api.h>
#include <linux/bitops.h>

#define _MM_MALLOC_H_INCLUDED
#include <emmintrin.h>
#undef _MM_MALLOC_H_INCLUDED

#include "ms912x_scan_methods.h"

static __always_inline __m128i
ms912x_scan_sse2_difference(const u32 *old, const u32 *new)
{
	return _mm_xor_si128(_mm_loadu_si128((const __m128i *)old),
			     _mm_loadu_si128((const __m128i *)new));
}

/* Reduce vector differences once per damage block, preserving its granularity. */
static __always_inline void
ms912x_scan_row_sse2_blocks(const u32 *old, const u32 *new, unsigned int width,
			    struct ms912x_scan_row_result *result)
{
	unsigned int x = 0;

	ms912x_scan_row_result_init(result, width);
	for (; x + 16 <= width; x += 16) {
		__m128i d0 = ms912x_scan_sse2_difference(old + x, new + x);
		__m128i d1 = ms912x_scan_sse2_difference(old + x + 4, new + x + 4);
		__m128i d2 = ms912x_scan_sse2_difference(old + x + 8, new + x + 8);
		__m128i d3 = ms912x_scan_sse2_difference(old + x + 12, new + x + 12);
		__m128i diff;

		diff = _mm_or_si128(_mm_or_si128(d0, d1), _mm_or_si128(d2, d3));
		ms912x_scan_record_block(result, width, x, x + 16,
					 _mm_movemask_ps(_mm_castsi128_ps(
					     _mm_cmpeq_epi32(diff, _mm_setzero_si128()))) != 0xf);
	}
	if (x < width) {
		__m128i diff = _mm_setzero_si128();
		unsigned int vx = x, changed = 0;

		for (; vx + 4 <= width; vx += 4)
			diff = _mm_or_si128(diff, ms912x_scan_sse2_difference(old + vx, new + vx));
		for (; vx < width; vx++)
			changed |= old[vx] != new[vx];
		changed |= _mm_movemask_ps(
			       _mm_castsi128_ps(_mm_cmpeq_epi32(diff, _mm_setzero_si128()))) != 0xf;
		ms912x_scan_record_block(result, width, x, width, changed);
	}
	result->equal_row = !result->changed_pixels;
}

/* Keep compiler-generated SIMD inside the FPU-protected batch call. */
static noinline void
ms912x_scan_rows_sse2_blocks_fpu(const u32 *old, const u32 *new, unsigned int width,
				 unsigned int row_stride, unsigned int row_count,
				 struct ms912x_scan_row_result *results)
{
	unsigned int row;

	for (row = 0; row < row_count; row++)
		ms912x_scan_row_sse2_blocks(old + row * row_stride, new + row * row_stride, width,
					    &results[row]);
}

void
ms912x_scan_rows_sse2_blocks(const u32 *old, const u32 *new, unsigned int width,
			     unsigned int row_stride, unsigned int row_count,
			     struct ms912x_scan_row_result *results)
{
	kernel_fpu_begin();
	ms912x_scan_rows_sse2_blocks_fpu(old, new, width, row_stride, row_count, results);
	kernel_fpu_end();
}

static __always_inline void
ms912x_scan_row_sse2_relaxed(const u32 *old, const u32 *new, u32 *next, unsigned int width,
			     unsigned int tolerance, struct ms912x_scan_row_result *result)
{
	const __m128i threshold = _mm_set1_epi8((char)tolerance);
	const __m128i rgb_mask = _mm_set1_epi32(0x00ffffff);
	const __m128i zero = _mm_setzero_si128();
	const __m128i one = _mm_set1_epi32(1);
	__m128i counts = zero;
	unsigned int x = 0;

	ms912x_scan_row_result_init(result, width);
	for (; x + 4 <= width; x += 4) {
		__m128i a = _mm_loadu_si128((const __m128i *)&old[x]);
		__m128i b = _mm_loadu_si128((const __m128i *)&new[x]);
		__m128i ab = _mm_subs_epu8(a, b);
		__m128i ba = _mm_subs_epu8(b, a);
		__m128i diff = _mm_max_epu8(ab, ba);
		__m128i over = _mm_and_si128(_mm_subs_epu8(diff, threshold), rgb_mask);
		__m128i equal = _mm_cmpeq_epi32(over, zero);
		unsigned int bits = (~(unsigned int)_mm_movemask_ps(_mm_castsi128_ps(equal))) & 0xf;
		unsigned int first = __ffs(bits | 0x10);
		unsigned int last = fls(bits);
		unsigned int has_change = !!bits;
		unsigned int change_mask = -has_change;
		__m128i merged = _mm_or_si128(_mm_and_si128(equal, a), _mm_andnot_si128(equal, b));

		_mm_storeu_si128((__m128i *)&next[x], merged);
		counts = _mm_add_epi32(counts, _mm_andnot_si128(equal, one));
		result->min_x =
		    min(result->min_x, ((x + first) & change_mask) | (width & ~change_mask));
		result->max_x = max(result->max_x, (x + last - 1) & -has_change);
	}
	counts = _mm_add_epi32(counts, _mm_shuffle_epi32(counts, _MM_SHUFFLE(1, 0, 3, 2)));
	counts = _mm_add_epi32(counts, _mm_shuffle_epi32(counts, _MM_SHUFFLE(2, 3, 0, 1)));
	result->changed_pixels = _mm_cvtsi128_si32(counts);
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

static noinline void
ms912x_scan_rows_sse2_relaxed_fpu(const u32 *old, const u32 *new, u32 *next, unsigned int width,
				  unsigned int row_stride, unsigned int row_count,
				  unsigned int tolerance, struct ms912x_scan_row_result *results)
{
	unsigned int row;

	for (row = 0; row < row_count; row++)
		ms912x_scan_row_sse2_relaxed(old + row * row_stride, new + row * row_stride,
					     next + row * row_stride, width, tolerance,
					     &results[row]);
}

void
ms912x_scan_rows_sse2_relaxed(const u32 *old, const u32 *new, u32 *next, unsigned int width,
			      unsigned int row_stride, unsigned int row_count,
			      unsigned int tolerance, struct ms912x_scan_row_result *results)
{
	kernel_fpu_begin();
	ms912x_scan_rows_sse2_relaxed_fpu(old, new, next, width, row_stride, row_count, tolerance,
					  results);
	kernel_fpu_end();
}
