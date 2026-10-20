// SPDX-License-Identifier: GPL-2.0-only

#include <asm/fpu/api.h>

/* Keep kernel SSE code independent of userspace allocation helpers. */
#define _MM_MALLOC_H_INCLUDED
#include <emmintrin.h>
#undef _MM_MALLOC_H_INCLUDED

#include <linux/kernel.h>

#include "ms912x_downscale_2x.h"

#include "ms912x_downscale_2x_sse.h"

static noinline void
ms912x_downscale_sse2_fpu(const u32 *src, u32 *dst,
		unsigned int src_width, unsigned int src_height, unsigned int dst_width,
		unsigned int src_stride, unsigned int dst_stride,
		unsigned int first_row, unsigned int end_row)
{
	unsigned int pairs = min(dst_width, src_width / 2);
	unsigned int vector_width = pairs & ~3U;
	unsigned int y, x;

	for (y = first_row; y < end_row; y++) {
		unsigned int sy0 = min(2 * y, src_height - 1);
		unsigned int sy1 = min(2 * y + 1, src_height - 1);
		const u32 *row0 = src + sy0 * src_stride;
		const u32 *row1 = src + sy1 * src_stride;
		u32 *out = dst + y * dst_stride;

		for (x = 0; x < vector_width; x += 4) {
			__m128i average = ms912x_downscale_sse_four(row0 + 2 * x, row1 + 2 * x);

			_mm_storeu_si128((__m128i *)(out + x), average);
		}
		if (x + 2 <= pairs) {
			_mm_storel_epi64((__m128i *)(out + x),
				ms912x_downscale_sse_two(row0 + 2 * x, row1 + 2 * x));
			x += 2;
		}
		for (; x < dst_width; x++) {
			unsigned int sx0 = min(2 * x, src_width - 1);
			unsigned int sx1 = min(2 * x + 1, src_width - 1);

			out[x] = ms912x_downscale_avg4(row0[sx0], row0[sx1], row1[sx0], row1[sx1]);
		}
	}
}

void ms912x_downscale_xrgb8888_2x_sse2(const u32 *src, u32 *dst,
		unsigned int src_width, unsigned int src_height,
		unsigned int dst_width, unsigned int dst_height,
		unsigned int src_stride, unsigned int dst_stride)
{
	unsigned int row = 0;

	if (!src_width || !src_height || !dst_width || !dst_height)
		return;
	while (row < dst_height) {
		unsigned int end = min(row + MS912X_DOWNSCALE_ROWS_PER_FPU, dst_height);

		kernel_fpu_begin();
		ms912x_downscale_sse2_fpu(src, dst, src_width, src_height, dst_width,
					 src_stride, dst_stride, row, end);
		kernel_fpu_end();
		row = end;
	}
}
