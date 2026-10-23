// SPDX-License-Identifier: GPL-2.0-only

#include <asm/fpu/api.h>
#include <asm/simd.h>

#include <linux/kernel.h>
#include <linux/unaligned.h>

#include "ms912x.h"
#include "ms912x_color_sse2.h"

#define MS912X_SIMD_ROWS_PER_FPU 16

/* Keep compiler-generated SIMD inside the FPU-protected batch call. */
static noinline void
ms912x_sse2_convert_rows(void *dst, const __le32 *src,
			 const struct drm_rect *rect,
			 unsigned int first_row, unsigned int end_row)
{
	unsigned int width = drm_rect_width(rect);
	unsigned int row;

	for (row = first_row; row < end_row; row++) {
		const __le32 *pixels = src + row * MS912X_MAX_WIDTH + rect->x1;
		u8 *line_dst = dst + (row - rect->y1) * width * 2;
		unsigned int x = 0;

		for (; x + 4 <= width; x += 4)
			ms912x_sse2_convert_four(line_dst + x * 2, pixels + x);
		if (x < width) {
			u32 pair = ms912x_xrgb_pair_to_uyvy(pixels + x);

			put_unaligned_le32(pair, line_dst + x * 2);
		}
	}
}

bool ms912x_convert_xrgb8888_to_uyvy_sse2(void *dst, const __le32 *src,
					    const struct drm_rect *rect)
{
	unsigned int row = rect->y1;
	unsigned int chunk_end;
	unsigned int end = rect->y2;

	if (!may_use_simd())
		return false;

	while (row < end) {
		chunk_end = min(row + MS912X_SIMD_ROWS_PER_FPU, end);
		kernel_fpu_begin();
		ms912x_sse2_convert_rows(dst, src, rect, row, chunk_end);
		kernel_fpu_end();
		row = chunk_end;
	}

	return true;
}
