// SPDX-License-Identifier: GPL-2.0-only

#include <linux/kernel.h>

#include <asm/neon-intrinsics.h>

#include "ms912x_scan_methods.h"

#include "ms912x_scan_neon_core.h"

/* The integer-only glue owns the FPU section. */
noinline void
ms912x_scan_rows_neon_relaxed_fpu(const u32 *old, const u32 *new, u32 *next, unsigned int width,
				  unsigned int row_stride, unsigned int row_count,
				  u32 pixel_mask, struct ms912x_scan_row_result *results)
{
	unsigned int row;

	for (row = 0; row < row_count; row++)
		ms912x_scan_row_neon_masked(old + row * row_stride, new + row * row_stride,
					     next ? next + row * row_stride : NULL, width, pixel_mask,
					     &results[row]);
}
