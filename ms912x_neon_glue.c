// SPDX-License-Identifier: GPL-2.0-only

#include <linux/fpu.h>
#include <linux/kernel.h>

#include "ms912x.h"
#include "ms912x_neon.h"
#include "ms912x_scan_methods.h"

#define MS912X_NEON_ROWS_PER_FPU 16

/* ARM32 forbids kernel_neon_begin() in a NEON compilation unit. */
bool ms912x_convert_xrgb8888_to_uyvy_neon(void *dst, const __le32 *src,
					    const struct drm_rect *rect)
{
	unsigned int row = rect->y1;
	unsigned int end = rect->y2;

	if (!ms912x_scan_neon_available())
		return false;

	while (row < end) {
		unsigned int chunk_end = min(row + MS912X_NEON_ROWS_PER_FPU, end);

		kernel_fpu_begin();
		ms912x_neon_convert_rows(dst, src, rect, row, chunk_end);
		kernel_fpu_end();
		row = chunk_end;
	}
	return true;
}

/* Callers check availability and limit these batches to 16 sampled rows. */
void ms912x_scan_rows_neon_blocks(const u32 *old, const u32 *new,
				  unsigned int width, unsigned int row_stride,
				  unsigned int row_count,
				  struct ms912x_scan_row_result *results)
{
	kernel_fpu_begin();
	ms912x_scan_rows_neon_blocks_fpu(old, new, width, row_stride, row_count, results);
	kernel_fpu_end();
}

void ms912x_scan_rows_neon_relaxed(const u32 *old, const u32 *new, u32 *next,
				   unsigned int width, unsigned int row_stride,
				   unsigned int row_count, unsigned int tolerance,
				   struct ms912x_scan_row_result *results)
{
	kernel_fpu_begin();
	ms912x_scan_rows_neon_relaxed_fpu(old, new, next, width, row_stride,
					 row_count,
					 ms912x_scan_tolerance_mask(tolerance), results);
	kernel_fpu_end();
}

void ms912x_scan_rows_neon_relaxed_masked(const u32 *old, const u32 *new,
					 u32 *next, unsigned int width,
					 unsigned int row_stride,
					 unsigned int row_count, u32 pixel_mask,
					 struct ms912x_scan_row_result *results)
{
	kernel_fpu_begin();
	ms912x_scan_rows_neon_relaxed_fpu(old, new, next, width, row_stride,
					 row_count, pixel_mask, results);
	kernel_fpu_end();
}
