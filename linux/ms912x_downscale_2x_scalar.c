// SPDX-License-Identifier: GPL-2.0-only

#include <linux/kernel.h>

#include "ms912x_downscale_2x.h"

void ms912x_downscale_xrgb8888_2x_scalar(const u32 *src, u32 *dst,
					unsigned int src_width,
					unsigned int src_height,
					unsigned int dst_width,
					unsigned int dst_height,
					unsigned int src_stride,
					unsigned int dst_stride)
{
	unsigned int y, x;

	if (!src_width || !src_height || !dst_width || !dst_height)
		return;
	for (y = 0; y < dst_height; y++) {
		unsigned int sy0 = min(2 * y, src_height - 1);
		unsigned int sy1 = min(2 * y + 1, src_height - 1);
		const u32 *row0 = src + sy0 * src_stride;
		const u32 *row1 = src + sy1 * src_stride;
		u32 *out = dst + y * dst_stride;

		for (x = 0; x < dst_width; x++) {
			unsigned int sx0 = min(2 * x, src_width - 1);
			unsigned int sx1 = min(2 * x + 1, src_width - 1);

			out[x] = ms912x_downscale_avg4(row0[sx0], row0[sx1],
						       row1[sx0], row1[sx1]);
		}
	}
}
