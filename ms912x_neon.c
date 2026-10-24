// SPDX-License-Identifier: GPL-2.0-only

#include <linux/kernel.h>
#include <linux/unaligned.h>

#include <asm/neon-intrinsics.h>

#include "ms912x_convert.h"
#include "ms912x_color.h"
#include "ms912x_neon.h"

#include "shared/ms912x_color_neon_core.h"

/* The integer-only glue owns the FPU section. */
static __always_inline void
ms912x_neon_convert_rows_core(void *dst, const __le32 *src,
			 const struct drm_rect *rect,
			 unsigned int first_row, unsigned int end_row,
			 unsigned int dst_stride, unsigned int red)
{
	unsigned int width = drm_rect_width(rect);
	unsigned int row;

	for (row = first_row; row < end_row; row++) {
		const __le32 *pixels = src + row * MS912X_MAX_WIDTH + rect->x1;
		u8 *line_dst = dst + (row - rect->y1) * dst_stride;
		unsigned int x = 0;

		for (; x + 8 <= width; x += 8)
			ms912x_color_neon_eight(line_dst + x * 2, pixels + x, red);
		if (x + 4 <= width) {
			ms912x_color_neon_four(line_dst + x * 2, pixels + x, red);
			x += 4;
		}
		if (x < width) {
			u32 pair = ms912x_rgbx_pair_to_uyvy(pixels + x, red);

			put_unaligned_le32(pair, line_dst + x * 2);
		}
	}
}

noinline void ms912x_neon_convert_rows_bgrx(void *dst, const __le32 *src,
		const struct drm_rect *rect, unsigned int first_row,
		unsigned int end_row, unsigned int dst_stride)
{
	ms912x_neon_convert_rows_core(dst, src, rect, first_row, end_row, dst_stride, 2);
}

noinline void ms912x_neon_convert_rows_rgbx(void *dst, const __le32 *src,
		const struct drm_rect *rect, unsigned int first_row,
		unsigned int end_row, unsigned int dst_stride)
{
	ms912x_neon_convert_rows_core(dst, src, rect, first_row, end_row, dst_stride, 0);
}
