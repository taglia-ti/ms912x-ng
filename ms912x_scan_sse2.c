// SPDX-License-Identifier: GPL-2.0-only

#include <asm/fpu/api.h>

#include "ms912x_scan_sse.h"

/* Keep compiler-generated SIMD inside the FPU-protected batch call. */
static noinline void ms912x_scan_rows_sse2_blocks_fpu(const u32 *old, const u32 *new,
						      unsigned int width, unsigned int row_stride,
						      unsigned int row_count,
						      struct ms912x_scan_row_result *results)
{
	unsigned int row;

	for (row = 0; row < row_count; row++)
		ms912x_scan_row_sse_blocks(old + row * row_stride, new + row * row_stride, width,
					   &results[row]);
}

void ms912x_scan_rows_sse2_blocks(const u32 *old, const u32 *new, unsigned int width,
				  unsigned int row_stride, unsigned int row_count,
				  struct ms912x_scan_row_result *results)
{
	kernel_fpu_begin();
	ms912x_scan_rows_sse2_blocks_fpu(old, new, width, row_stride, row_count, results);
	kernel_fpu_end();
}

static noinline void ms912x_scan_rows_sse2_relaxed_fpu(const u32 *old, const u32 *new, u32 *next,
						       unsigned int width, unsigned int row_stride,
						       unsigned int row_count,
						       u32 pixel_mask,
						       struct ms912x_scan_row_result *results)
{
	unsigned int row;

	for (row = 0; row < row_count; row++)
		ms912x_scan_row_sse_masked(old + row * row_stride, new + row * row_stride,
					    next + row * row_stride, width, pixel_mask,
					    &results[row]);
}

void ms912x_scan_rows_sse2_relaxed(const u32 *old, const u32 *new, u32 *next, unsigned int width,
				   unsigned int row_stride, unsigned int row_count,
				   unsigned int tolerance, struct ms912x_scan_row_result *results)
{
	kernel_fpu_begin();
	ms912x_scan_rows_sse2_relaxed_fpu(old, new, next, width, row_stride, row_count,
					  ms912x_scan_tolerance_mask(tolerance),
					  results);
	kernel_fpu_end();
}

void ms912x_scan_rows_sse2_relaxed_masked(const u32 *old, const u32 *new,
				  u32 *next, unsigned int width,
				  unsigned int row_stride, unsigned int row_count,
				  u32 pixel_mask,
				  struct ms912x_scan_row_result *results)
{
	kernel_fpu_begin();
	ms912x_scan_rows_sse2_relaxed_fpu(old, new, next, width, row_stride,
					  row_count, pixel_mask, results);
	kernel_fpu_end();
}
