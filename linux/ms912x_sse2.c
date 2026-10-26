// SPDX-License-Identifier: GPL-2.0-only

#include <asm/fpu/api.h>
#include <asm/simd.h>

#include <linux/kernel.h>
#include <linux/unaligned.h>

#include "ms912x_convert.h"
#include "ms912x_color.h"
#include "shared/ms912x_color_sse2_core.h"

#define MS912X_SIMD_ROWS_PER_FPU 16

/* The noinline workers below specialize channel order inside the FPU section. */
static __always_inline void
ms912x_sse2_convert_rows_core(void *dst, const __le32 *src,
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

		for (; x + 4 <= width; x += 4)
			ms912x_color_sse2_four(line_dst + x * 2, pixels + x, red);
		if (x < width) {
			u32 pair = ms912x_rgbx_pair_to_uyvy(pixels + x, red);

			put_unaligned_le32(pair, line_dst + x * 2);
		}
	}
}

static noinline void ms912x_sse2_convert_rows_bgrx(void *dst, const __le32 *src,
		const struct drm_rect *rect, unsigned int first_row,
		unsigned int end_row, unsigned int dst_stride)
{
	ms912x_sse2_convert_rows_core(dst, src, rect, first_row, end_row, dst_stride, 2);
}

static noinline void ms912x_sse2_convert_rows_rgbx(void *dst, const __le32 *src,
		const struct drm_rect *rect, unsigned int first_row,
		unsigned int end_row, unsigned int dst_stride)
{
	ms912x_sse2_convert_rows_core(dst, src, rect, first_row, end_row, dst_stride, 0);
}

bool ms912x_convert_rgb_to_uyvy_sse2_strided(void *dst, const __le32 *src,
		const struct drm_rect *rect, unsigned int dst_stride, bool rgbx)
{
	void (*convert_rows)(void *, const __le32 *, const struct drm_rect *,
			     unsigned int, unsigned int, unsigned int) =
		rgbx ? ms912x_sse2_convert_rows_rgbx : ms912x_sse2_convert_rows_bgrx;
	unsigned int row = rect->y1;
	unsigned int chunk_end;
	unsigned int end = rect->y2;

	if (!may_use_simd())
		return false;

	while (row < end) {
		chunk_end = min(row + MS912X_SIMD_ROWS_PER_FPU, end);
		kernel_fpu_begin();
		convert_rows(dst, src, rect, row, chunk_end, dst_stride);
		kernel_fpu_end();
		row = chunk_end;
	}

	return true;
}
