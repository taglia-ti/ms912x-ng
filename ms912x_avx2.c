// SPDX-License-Identifier: GPL-2.0-only

#include <asm/fpu/api.h>

#include <linux/kernel.h>
#include <linux/unaligned.h>

/* The kernel path does not use the userspace allocation helpers. */
#define _MM_MALLOC_H_INCLUDED
#include <immintrin.h>
#undef _MM_MALLOC_H_INCLUDED

#include "ms912x.h"
#include "ms912x_color_sse2.h"
#include "shared/ms912x_color_avx2_core.h"

#define MS912X_AVX2_ROWS_PER_FPU 16

/* Keep compiler-generated SIMD inside the FPU-protected batch call. */
static noinline void
ms912x_avx2_convert_rows(void *dst, const __le32 *src,
			 const struct drm_rect *rect,
			 unsigned int first_row, unsigned int end_row, unsigned int dst_stride)
{
	unsigned int width = drm_rect_width(rect);
	unsigned int row;

	for (row = first_row; row < end_row; row++) {
		const __le32 *pixels = src + row * MS912X_MAX_WIDTH + rect->x1;
		u8 *line_dst = dst + (row - rect->y1) * dst_stride;
		unsigned int x = 0;

		for (; x + 16 <= width; x += 16)
			ms912x_color_avx2_sixteen(line_dst + x * 2, pixels + x, 2);
		if (x + 8 <= width) {
			ms912x_color_avx2_eight(line_dst + x * 2, pixels + x, 2);
			x += 8;
		}
		if (x + 4 <= width) {
			ms912x_sse2_convert_four(line_dst + x * 2, pixels + x);
			x += 4;
		}
		if (x < width) {
			u32 pair = ms912x_xrgb_pair_to_uyvy(pixels + x);

			put_unaligned_le32(pair, line_dst + x * 2);
		}
	}
}

void ms912x_convert_xrgb8888_to_uyvy_avx2_strided(void *dst, const __le32 *src,
						  const struct drm_rect *rect,
						  unsigned int dst_stride)
{
	unsigned int row = rect->y1;
	unsigned int chunk_end;
	unsigned int end = rect->y2;

	while (row < end) {
		chunk_end = min(row + MS912X_AVX2_ROWS_PER_FPU, end);
		kernel_fpu_begin();
		ms912x_avx2_convert_rows(dst, src, rect, row, chunk_end, dst_stride);
		_mm256_zeroupper();
		kernel_fpu_end();
		row = chunk_end;
	}
}

void ms912x_convert_xrgb8888_to_uyvy_avx2(void *dst, const __le32 *src, const struct drm_rect *rect)
{
	ms912x_convert_xrgb8888_to_uyvy_avx2_strided(dst, src, rect, drm_rect_width(rect) * 2);
}
