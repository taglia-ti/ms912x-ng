// SPDX-License-Identifier: GPL-2.0-only

#include <asm/fpu/api.h>

#include <linux/kernel.h>

/* Keep the kernel AVX path independent of userspace allocation helpers. */
#define _MM_MALLOC_H_INCLUDED
#include <immintrin.h>
#undef _MM_MALLOC_H_INCLUDED

#include "ms912x_scan_methods.h"

static __always_inline __m256i
ms912x_scan_avx2_difference(const u32 *old, const u32 *new)
{
	return _mm256_xor_si256(_mm256_loadu_si256((const __m256i *)old),
				_mm256_loadu_si256((const __m256i *)new));
}

/* Reduce vector differences once per damage block, preserving its granularity. */
static __always_inline void
ms912x_scan_row_avx2_blocks(const u32 *old, const u32 *new, unsigned int width,
			    struct ms912x_scan_row_result *result)
{
	unsigned int x = 0;

	ms912x_scan_row_result_init(result, width);
	for (; x + 64 <= width; x += 64) {
		__m256i d0 = ms912x_scan_avx2_difference(old + x, new + x);
		__m256i d1 = ms912x_scan_avx2_difference(old + x + 8, new + x + 8);
		__m256i d2 = ms912x_scan_avx2_difference(old + x + 16, new + x + 16);
		__m256i d3 = ms912x_scan_avx2_difference(old + x + 24, new + x + 24);
		__m256i diff;

		d0 = _mm256_or_si256(d0, ms912x_scan_avx2_difference(old + x + 32, new + x + 32));
		d1 = _mm256_or_si256(d1, ms912x_scan_avx2_difference(old + x + 40, new + x + 40));
		d2 = _mm256_or_si256(d2, ms912x_scan_avx2_difference(old + x + 48, new + x + 48));
		d3 = _mm256_or_si256(d3, ms912x_scan_avx2_difference(old + x + 56, new + x + 56));
		diff = _mm256_or_si256(_mm256_or_si256(d0, d1), _mm256_or_si256(d2, d3));
		ms912x_scan_record_block(result, width, x, x + 64, !_mm256_testz_si256(diff, diff));
	}
	if (x < width) {
		__m256i diff = _mm256_setzero_si256();
		unsigned int vx = x, changed = 0;

		for (; vx + 8 <= width; vx += 8)
			diff =
			    _mm256_or_si256(diff, ms912x_scan_avx2_difference(old + vx, new + vx));
		for (; vx < width; vx++)
			changed |= old[vx] != new[vx];
		changed |= !_mm256_testz_si256(diff, diff);
		ms912x_scan_record_block(result, width, x, width, changed);
	}
	result->equal_row = !result->changed_pixels;
}

/* Keep compiler-generated SIMD inside the FPU-protected batch call. */
static noinline void
ms912x_scan_rows_avx2_blocks_fpu(const u32 *old, const u32 *new, unsigned int width,
				 unsigned int row_stride, unsigned int row_count,
				 struct ms912x_scan_row_result *results)
{
	unsigned int row;

	for (row = 0; row < row_count; row++)
		ms912x_scan_row_avx2_blocks(old + row * row_stride, new + row * row_stride, width,
					    &results[row]);
	_mm256_zeroupper();
}

void
ms912x_scan_rows_avx2_blocks(const u32 *old, const u32 *new, unsigned int width,
			     unsigned int row_stride, unsigned int row_count,
			     struct ms912x_scan_row_result *results)
{
	kernel_fpu_begin();
	ms912x_scan_rows_avx2_blocks_fpu(old, new, width, row_stride, row_count, results);
	kernel_fpu_end();
}

static __always_inline void
ms912x_scan_row_avx2_relaxed(const u32 *old, const u32 *new, u32 *next, unsigned int width,
			     unsigned int tolerance, struct ms912x_scan_row_result *result)
{
	const __m256i rgb_mask = _mm256_set1_epi32(0x00ffffff);
	const __m256i threshold = _mm256_set1_epi8((char)tolerance);
	const __m256i zero = _mm256_setzero_si256();
	const __m256i one = _mm256_set1_epi32(1);
	__m256i counts = zero, first = _mm256_set1_epi32(width), last = zero;
	__m256i positions = _mm256_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7);
	__m128i count128, first128, last128;
	unsigned int x = 0;

	ms912x_scan_row_result_init(result, width);
	for (; x + 8 <= width; x += 8) {
		__m256i a = _mm256_loadu_si256((const __m256i *)&old[x]);
		__m256i b = _mm256_loadu_si256((const __m256i *)&new[x]);
		__m256i diff = _mm256_or_si256(_mm256_subs_epu8(a, b), _mm256_subs_epu8(b, a));
		__m256i over = _mm256_and_si256(_mm256_subs_epu8(diff, threshold), rgb_mask);
		__m256i equal = _mm256_cmpeq_epi32(over, zero);

		/* Keep the baseline mask and row reductions in vector registers. */
		_mm256_storeu_si256((__m256i *)&next[x], _mm256_blendv_epi8(b, a, equal));
		counts = _mm256_add_epi32(counts, _mm256_andnot_si256(equal, one));
		first = _mm256_min_epu32(first, _mm256_or_si256(positions, equal));
		last = _mm256_max_epu32(last, _mm256_andnot_si256(equal, positions));
		positions = _mm256_add_epi32(positions, _mm256_set1_epi32(8));
	}
	count128 =
	    _mm_add_epi32(_mm256_castsi256_si128(counts), _mm256_extracti128_si256(counts, 1));
	first128 = _mm_min_epu32(_mm256_castsi256_si128(first), _mm256_extracti128_si256(first, 1));
	last128 = _mm_max_epu32(_mm256_castsi256_si128(last), _mm256_extracti128_si256(last, 1));
	count128 = _mm_add_epi32(count128, _mm_shuffle_epi32(count128, _MM_SHUFFLE(1, 0, 3, 2)));
	count128 = _mm_add_epi32(count128, _mm_shuffle_epi32(count128, _MM_SHUFFLE(2, 3, 0, 1)));
	first128 = _mm_min_epu32(first128, _mm_shuffle_epi32(first128, _MM_SHUFFLE(1, 0, 3, 2)));
	first128 = _mm_min_epu32(first128, _mm_shuffle_epi32(first128, _MM_SHUFFLE(2, 3, 0, 1)));
	last128 = _mm_max_epu32(last128, _mm_shuffle_epi32(last128, _MM_SHUFFLE(1, 0, 3, 2)));
	last128 = _mm_max_epu32(last128, _mm_shuffle_epi32(last128, _MM_SHUFFLE(2, 3, 0, 1)));
	result->changed_pixels = _mm_cvtsi128_si32(count128);
	result->min_x = _mm_cvtsi128_si32(first128);
	result->max_x = _mm_cvtsi128_si32(last128);
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
ms912x_scan_rows_avx2_relaxed_fpu(const u32 *old, const u32 *new, u32 *next, unsigned int width,
				  unsigned int row_stride, unsigned int row_count,
				  unsigned int tolerance, struct ms912x_scan_row_result *results)
{
	unsigned int row;

	for (row = 0; row < row_count; row++)
		ms912x_scan_row_avx2_relaxed(old + row * row_stride, new + row * row_stride,
					     next + row * row_stride, width, tolerance,
					     &results[row]);
	_mm256_zeroupper();
}

void
ms912x_scan_rows_avx2_relaxed(const u32 *old, const u32 *new, u32 *next, unsigned int width,
			      unsigned int row_stride, unsigned int row_count,
			      unsigned int tolerance, struct ms912x_scan_row_result *results)
{
	kernel_fpu_begin();
	ms912x_scan_rows_avx2_relaxed_fpu(old, new, next, width, row_stride, row_count, tolerance,
					  results);
	kernel_fpu_end();
}
