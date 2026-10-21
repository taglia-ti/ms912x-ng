// SPDX-License-Identifier: GPL-2.0-only

#include <asm/fpu/api.h>

#include <linux/kernel.h>

/* Keep the kernel AVX path independent of userspace allocation helpers. */
#define _MM_MALLOC_H_INCLUDED
#include <immintrin.h>
#undef _MM_MALLOC_H_INCLUDED

#include "ms912x_scan_methods.h"

static __always_inline __m256i
ms912x_scan_avx_difference(const u32 *old, const u32 *new)
{
	/* These are bitwise operations; the casts perform no numeric conversion. */
	return _mm256_castps_si256(_mm256_xor_ps(
		_mm256_castsi256_ps(_mm256_loadu_si256((const __m256i *)old)),
		_mm256_castsi256_ps(_mm256_loadu_si256((const __m256i *)new))));
}

static __always_inline __m256i ms912x_scan_avx_or(__m256i a, __m256i b)
{
	return _mm256_castps_si256(_mm256_or_ps(_mm256_castsi256_ps(a),
					       _mm256_castsi256_ps(b)));
}

/* Reduce vector differences once per damage block, preserving its granularity. */
static __always_inline void
ms912x_scan_row_avx_blocks(const u32 *old, const u32 *new, unsigned int width,
			    struct ms912x_scan_row_result *result)
{
	unsigned int x = 0;

	ms912x_scan_row_result_init(result, width);
	for (; x + 64 <= width; x += 64) {
		__m256i d0 = ms912x_scan_avx_difference(old + x, new + x);
		__m256i d1 = ms912x_scan_avx_difference(old + x + 8, new + x + 8);
		__m256i d2 = ms912x_scan_avx_difference(old + x + 16, new + x + 16);
		__m256i d3 = ms912x_scan_avx_difference(old + x + 24, new + x + 24);
		__m256i diff;

		d0 = ms912x_scan_avx_or(d0, ms912x_scan_avx_difference(old + x + 32, new + x + 32));
		d1 = ms912x_scan_avx_or(d1, ms912x_scan_avx_difference(old + x + 40, new + x + 40));
		d2 = ms912x_scan_avx_or(d2, ms912x_scan_avx_difference(old + x + 48, new + x + 48));
		d3 = ms912x_scan_avx_or(d3, ms912x_scan_avx_difference(old + x + 56, new + x + 56));
		diff = ms912x_scan_avx_or(ms912x_scan_avx_or(d0, d1), ms912x_scan_avx_or(d2, d3));
		ms912x_scan_record_block(result, width, x, x + 64, !_mm256_testz_si256(diff, diff));
	}
	if (x < width) {
		__m256i diff = _mm256_setzero_si256();
		unsigned int vx = x, changed = 0;

		for (; vx + 8 <= width; vx += 8)
			diff =
			    ms912x_scan_avx_or(diff, ms912x_scan_avx_difference(old + vx, new + vx));
		for (; vx < width; vx++)
			changed |= old[vx] != new[vx];
		changed |= !_mm256_testz_si256(diff, diff);
		ms912x_scan_record_block(result, width, x, width, changed);
	}
	result->equal_row = !result->changed_pixels;
}

/* Keep compiler-generated SIMD inside the FPU-protected batch call. */
static noinline void
ms912x_scan_rows_avx_blocks_fpu(const u32 *old, const u32 *new, unsigned int width,
				 unsigned int row_stride, unsigned int row_count,
				 struct ms912x_scan_row_result *results)
{
	unsigned int row;

	for (row = 0; row < row_count; row++)
		ms912x_scan_row_avx_blocks(old + row * row_stride, new + row * row_stride, width,
					    &results[row]);
	_mm256_zeroupper();
}

void
ms912x_scan_rows_avx_blocks(const u32 *old, const u32 *new, unsigned int width,
			     unsigned int row_stride, unsigned int row_count,
			     struct ms912x_scan_row_result *results)
{
	kernel_fpu_begin();
	ms912x_scan_rows_avx_blocks_fpu(old, new, width, row_stride, row_count, results);
	kernel_fpu_end();
}

#define MS912X_MASKED_TYPE __m256i
#define MS912X_MASKED_PIXELS 8
#define MS912X_MASKED_ZERO() _mm256_setzero_si256()
#define MS912X_MASKED_OR(a, b) ms912x_scan_avx_or(a, b)
#define MS912X_MASKED_DIFF(a, b) ms912x_scan_avx_difference(a, b)
#define MS912X_MASKED_CHANGED(diff, mask) (!_mm256_testz_si256(diff, _mm256_set1_epi32(mask)))
#define MS912X_MASKED_COPY(dst, src) \
	_mm256_storeu_si256((__m256i *)(dst), _mm256_loadu_si256((const __m256i *)(src)))
#define MS912X_MASKED_BLOCK ms912x_scan_avx_masked_block
#define MS912X_MASKED_PAIR ms912x_scan_avx_masked_pair
#define MS912X_MASKED_ROW ms912x_scan_row_avx_masked
#include "ms912x_scan_masked_template.h"

static noinline void
ms912x_scan_rows_avx_relaxed_fpu(const u32 *old, const u32 *new, u32 *next, unsigned int width,
				  unsigned int row_stride, unsigned int row_count,
				  unsigned int tolerance, struct ms912x_scan_row_result *results)
{
	u32 pixel_mask = ms912x_scan_tolerance_mask(tolerance);
	unsigned int row;

	for (row = 0; row < row_count; row++)
		ms912x_scan_row_avx_masked(old + row * row_stride, new + row * row_stride,
					     next + row * row_stride, width, pixel_mask,
					     &results[row]);
	_mm256_zeroupper();
}

void
ms912x_scan_rows_avx_relaxed(const u32 *old, const u32 *new, u32 *next, unsigned int width,
			      unsigned int row_stride, unsigned int row_count,
			      unsigned int tolerance, struct ms912x_scan_row_result *results)
{
	kernel_fpu_begin();
	ms912x_scan_rows_avx_relaxed_fpu(old, new, next, width, row_stride, row_count, tolerance,
					  results);
	kernel_fpu_end();
}
