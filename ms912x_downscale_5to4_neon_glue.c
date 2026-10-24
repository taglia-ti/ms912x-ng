// SPDX-License-Identifier: GPL-2.0-only

#include <linux/fpu.h>
#include <linux/kernel.h>

#include "ms912x.h"
#include "ms912x_downscale_5to4.h"
#include "ms912x_neon.h"
#include "ms912x_scan_methods.h"

void ms912x_downscale_xrgb8888_5to4_neon_region(const u32 *src, u32 *dst,
					      unsigned int src_width,
					      unsigned int src_height,
					      unsigned int dst_width,
					      unsigned int dst_height,
					      unsigned int src_stride,
					      unsigned int dst_stride,
					      unsigned int offset_x, unsigned int offset_y)
{
	unsigned int row = 0;

	if (!src_width || !src_height || !dst_width || !dst_height ||
	    offset_x >= src_width || offset_y >= src_height)
		return;
	if (!ms912x_scan_neon_available()) {
		ms912x_downscale_xrgb8888_5to4_scalar_region(
			src, dst, src_width, src_height, dst_width, dst_height,
			src_stride, dst_stride, offset_x, offset_y);
		return;
	}
	while (row < dst_height) {
		unsigned int end = min(row + MS912X_DOWNSCALE_ROWS_PER_FPU, dst_height);

		kernel_fpu_begin();
		ms912x_downscale_xrgb8888_5to4_neon_rows_fpu(src, dst,
			src_width, src_height, dst_width, src_stride, dst_stride,
			row, end, offset_x, offset_y);
		kernel_fpu_end();
		row = end;
	}
}
