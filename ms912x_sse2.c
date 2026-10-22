// SPDX-License-Identifier: GPL-2.0-only

#include <asm/fpu/api.h>
#include <asm/simd.h>

#include <linux/kernel.h>
#include <linux/unaligned.h>

#include "ms912x.h"
#include "ms912x_color_sse2.h"
#include "ms912x_rgb565.h"

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

/* Two independent 128-bit vectors per iteration, with bounded tails. */
static __always_inline void ms912x_rgb565_mask_row_sse2(u8 *out,
							const u16 *pixels,
							unsigned int width,
							__m128i mask)
{
	size_t x = 0;

	for (; x + 16 <= width; x += 16) {
		__m128i a = _mm_loadu_si128((const __m128i *)(pixels + x));
		__m128i b = _mm_loadu_si128((const __m128i *)(pixels + x + 8));

		_mm_storeu_si128((__m128i *)(out + x * 2),
				 _mm_and_si128(a, mask));
		_mm_storeu_si128((__m128i *)(out + (x + 8) * 2),
				 _mm_and_si128(b, mask));
	}
	if (x + 8 <= width) {
		__m128i packed = _mm_loadu_si128((const __m128i *)(pixels + x));

		_mm_storeu_si128((__m128i *)(out + x * 2),
				 _mm_and_si128(packed, mask));
		x += 8;
	}
	ms912x_rgb565_mask_tail(out + x * 2, pixels + x, width - x);
}

/* Eight packed native pixels per register: load, mask and store. */
static noinline void ms912x_sse2_mask_rgb565_rows(void *dst, const u16 *src,
						  const struct drm_rect *rect,
						  unsigned int first_row,
						  unsigned int end_row)
{
	const __m128i mask = _mm_set1_epi16((short)MS912X_RGB565_WIRE_MASK);
	unsigned int width = drm_rect_width(rect);
	const u16 *pixels = src + first_row * MS912X_MAX_WIDTH + rect->x1;
	u8 *out = (u8 *)dst + (size_t)(first_row - rect->y1) * width * 2;
	unsigned int row;

	for (row = first_row; row < end_row; row++) {
		ms912x_rgb565_mask_row_sse2(out, pixels, width, mask);
		pixels += MS912X_MAX_WIDTH;
		out += (size_t)width * 2;
	}
}

bool ms912x_rgb565_mask_sse2(void *dst, const u16 *src,
			     const struct drm_rect *rect)
{
	unsigned int row = rect->y1;
	unsigned int end = rect->y2;

	if (drm_rect_width(rect) < 8 || !may_use_simd())
		return false;
	while (row < end) {
		unsigned int chunk_end =
			min(row + MS912X_SIMD_ROWS_PER_FPU, end);

		kernel_fpu_begin();
		ms912x_sse2_mask_rgb565_rows(dst, src, rect, row, chunk_end);
		kernel_fpu_end();
		row = chunk_end;
	}
	return true;
}
