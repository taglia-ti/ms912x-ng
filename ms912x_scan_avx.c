// SPDX-License-Identifier: GPL-2.0-only

#include <asm/fpu/api.h>

#include <linux/kernel.h>

/* Keep the kernel AVX path independent of userspace allocation helpers. */
#define _MM_MALLOC_H_INCLUDED
#include <immintrin.h>
#undef _MM_MALLOC_H_INCLUDED

#include "ms912x_scan_methods.h"

#include "ms912x_scan_avx_core.h"

/* Keep compiler-generated SIMD inside the FPU-protected batch call. */
static noinline void
ms912x_scan_rows_avx_relaxed_fpu(const u32 *old, const u32 *new, u32 *next, unsigned int width,
				  unsigned int row_stride, unsigned int row_count,
				  u32 pixel_mask, struct ms912x_scan_row_result *results)
{
	unsigned int row;

	for (row = 0; row < row_count; row++)
		ms912x_scan_row_avx_masked(old + row * row_stride, new + row * row_stride,
					     next ? next + row * row_stride : NULL, width, pixel_mask,
					     &results[row]);
	_mm256_zeroupper();
}

void
ms912x_scan_rows_avx_relaxed_masked(const u32 *old, const u32 *new,
				    u32 *next, unsigned int width,
				    unsigned int row_stride,
				    unsigned int row_count, u32 pixel_mask,
				    struct ms912x_scan_row_result *results)
{
	kernel_fpu_begin();
	ms912x_scan_rows_avx_relaxed_fpu(old, new, next, width, row_stride,
					 row_count, pixel_mask, results);
	kernel_fpu_end();
}
