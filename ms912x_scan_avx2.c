// SPDX-License-Identifier: GPL-2.0-only

#include <asm/fpu/api.h>

#include <linux/bitops.h>
#include <linux/kernel.h>

/* Keep the kernel AVX path independent of userspace allocation helpers. */
#define _MM_MALLOC_H_INCLUDED
#include <immintrin.h>
#undef _MM_MALLOC_H_INCLUDED

#include "ms912x_scan_methods.h"

#define MS912X_SCAN_AVX2_BLOCK_BYTES 256
#define MS912X_SCAN_AVX2_BLOCK_PIXELS \
	(MS912X_SCAN_AVX2_BLOCK_BYTES / sizeof(u32))

/* Compare 256-byte blocks; any changed block is fully dirty. */
static __always_inline void
ms912x_scan_row_avx2_blocks(const u32 *old, const u32 *new,
			    unsigned int width,
			    struct ms912x_scan_row_result *result)
{
	unsigned int x = 0;

	ms912x_scan_row_result_init(result, width);
	while (x < width) {
		unsigned int block_end = min(width,
					     x + MS912X_SCAN_AVX2_BLOCK_PIXELS);
		unsigned int block_changed = 0;
		unsigned int vector_x;
		unsigned int block_width = block_end - x;
		unsigned int changed, mask;

		for (vector_x = x; vector_x + 8 <= block_end; vector_x += 8) {
			__m256i old_pixels = _mm256_loadu_si256(
				(const __m256i *)&old[vector_x]);
			__m256i new_pixels = _mm256_loadu_si256(
				(const __m256i *)&new[vector_x]);
			__m256i equal = _mm256_cmpeq_epi32(old_pixels,
							    new_pixels);

			block_changed |= (~(unsigned int)_mm256_movemask_ps(
				_mm256_castsi256_ps(equal))) & 0xff;
		}
		/* At most seven trailing pixels complete a partial block. */
		for (; vector_x < block_end; vector_x++)
			block_changed |= old[vector_x] != new[vector_x];

		changed = !!block_changed;
		mask = -changed;
		result->avx2_blocks++;
		result->avx2_equal_blocks += !changed;
		result->changed_pixels += block_width * changed;
		result->min_x = min(result->min_x,
				    (x & mask) | (width & ~mask));
		result->max_x = max(result->max_x,
				    (block_end - 1) & mask);
		x = block_end;
	}
	result->equal_row = !result->changed_pixels;
}

void ms912x_scan_rows_avx2_blocks(const u32 *old, const u32 *new,
				  unsigned int width,
				  unsigned int row_stride,
				  unsigned int row_count,
				  struct ms912x_scan_row_result *results)
{
	unsigned int row;

	kernel_fpu_begin();
	for (row = 0; row < row_count; row++)
		ms912x_scan_row_avx2_blocks(old + row * row_stride,
					     new + row * row_stride,
					     width, &results[row]);
	_mm256_zeroupper();
	kernel_fpu_end();
}

static __always_inline void
ms912x_scan_row_avx2_relaxed(const u32 *old, const u32 *new, u32 *next,
			    unsigned int width, unsigned int tolerance,
			    struct ms912x_scan_row_result *result)
{
	const __m256i rgb_mask = _mm256_set1_epi32(0x00ffffff);
	const __m256i threshold = _mm256_set1_epi8((char)tolerance);
	const __m256i zero = _mm256_setzero_si256();
	unsigned int x = 0;

	ms912x_scan_row_result_init(result, width);
	for (; x + 8 <= width; x += 8) {
		__m256i old_pixels = _mm256_loadu_si256(
			(const __m256i *)&old[x]);
		__m256i new_pixels = _mm256_loadu_si256(
			(const __m256i *)&new[x]);
		__m256i old_minus_new = _mm256_subs_epu8(old_pixels, new_pixels);
		__m256i new_minus_old = _mm256_subs_epu8(new_pixels, old_pixels);
		__m256i abs_diff = _mm256_max_epu8(old_minus_new,
						    new_minus_old);
		__m256i over_threshold = _mm256_and_si256(
			_mm256_subs_epu8(abs_diff, threshold), rgb_mask);
		__m256i equal = _mm256_cmpeq_epi32(over_threshold, zero);
		unsigned int changed =
			(~(unsigned int)_mm256_movemask_ps(
				_mm256_castsi256_ps(equal))) & 0xff;
		unsigned int first = __ffs(changed | 0x100);
		unsigned int last = fls(changed);
		unsigned int has_change = !!changed;
		unsigned int change_mask = -has_change;
		unsigned int max_x = (x + last - 1) & -has_change;
		__m256i update_mask = _mm256_setr_epi32(
			-((changed >> 0) & 1), -((changed >> 1) & 1),
			-((changed >> 2) & 1), -((changed >> 3) & 1),
			-((changed >> 4) & 1), -((changed >> 5) & 1),
			-((changed >> 6) & 1), -((changed >> 7) & 1));
		__m256i updated = _mm256_blendv_epi8(old_pixels, new_pixels,
						      update_mask);

		_mm256_storeu_si256((__m256i *)&next[x], updated);
		result->changed_pixels += hweight8(changed);
		result->min_x = min(result->min_x,
				    ((x + first) & change_mask) |
				    (width & ~change_mask));
		result->max_x = max(result->max_x, max_x);
	}

	for (; x < width; x++) {
		unsigned int changed = ms912x_scan_pixel_relaxed_changed(
			old[x], new[x], tolerance);
		unsigned int mask = -changed;

		result->changed_pixels += changed;
		result->min_x = min(result->min_x,
				    (x & mask) | (width & ~mask));
		result->max_x = max(result->max_x, x & mask);
		next[x] = changed ? new[x] : old[x];
	}
	result->equal_row = !result->changed_pixels;
}

void ms912x_scan_rows_avx2_relaxed(const u32 *old, const u32 *new,
				   u32 *next, unsigned int width,
				   unsigned int row_stride,
				   unsigned int row_count,
				   unsigned int tolerance,
				   struct ms912x_scan_row_result *results)
{
	unsigned int row;

	kernel_fpu_begin();
	for (row = 0; row < row_count; row++)
		ms912x_scan_row_avx2_relaxed(
			old + row * row_stride, new + row * row_stride,
			next + row * row_stride, width, tolerance, &results[row]);
	_mm256_zeroupper();
	kernel_fpu_end();
}
