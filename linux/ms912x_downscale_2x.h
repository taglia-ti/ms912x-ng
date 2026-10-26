/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MS912X_DOWNSCALE_2X_H
#define MS912X_DOWNSCALE_2X_H

#include <linux/types.h>

#include "ms912x_neon.h"

#ifndef MS912X_DOWNSCALE_ROWS_PER_FPU
#define MS912X_DOWNSCALE_ROWS_PER_FPU 16
#endif

/* Round the four samples once, independently for all four byte channels. */
static __always_inline u32 ms912x_downscale_avg4(u32 p00, u32 p01,
						 u32 p10, u32 p11)
{
	const u32 high_mask = 0x3f3f3f3f;
	const u32 low_mask = 0x03030303;
	u32 high = ((p00 >> 2) & high_mask) + ((p01 >> 2) & high_mask) +
		   ((p10 >> 2) & high_mask) + ((p11 >> 2) & high_mask);
	u32 low = (p00 & low_mask) + (p01 & low_mask) +
		  (p10 & low_mask) + (p11 & low_mask) + 0x02020202;

	return high + ((low >> 2) & low_mask);
}

typedef void (*ms912x_downscale_2x_fn)(const u32 *src, u32 *dst,
				    unsigned int src_width,
				    unsigned int src_height,
				    unsigned int dst_width,
				    unsigned int dst_height,
				    unsigned int src_stride,
				    unsigned int dst_stride);

void ms912x_downscale_xrgb8888_2x_scalar(const u32 *src, u32 *dst,
					unsigned int src_width,
					unsigned int src_height,
					unsigned int dst_width,
					unsigned int dst_height,
					unsigned int src_stride,
					unsigned int dst_stride);
void ms912x_downscale_xrgb8888_2x(const u32 *src, u32 *dst,
					 unsigned int src_width,
					 unsigned int src_height,
					 unsigned int dst_width,
					 unsigned int dst_height,
					 unsigned int src_stride,
					 unsigned int dst_stride);

#ifdef CONFIG_X86_64
void ms912x_downscale_xrgb8888_2x_sse2(const u32 *src, u32 *dst,
					       unsigned int src_width,
					       unsigned int src_height,
					       unsigned int dst_width,
					       unsigned int dst_height,
					       unsigned int src_stride,
					       unsigned int dst_stride);
void ms912x_downscale_xrgb8888_2x_avx2(const u32 *src, u32 *dst,
					       unsigned int src_width,
					       unsigned int src_height,
					       unsigned int dst_width,
					       unsigned int dst_height,
					       unsigned int src_stride,
					       unsigned int dst_stride);
#endif

#if MS912X_HAS_NEON
void ms912x_downscale_xrgb8888_2x_neon_rows_fpu(
		const u32 *src, u32 *dst, unsigned int src_width,
		unsigned int src_height, unsigned int dst_width,
		unsigned int src_stride, unsigned int dst_stride,
		unsigned int first_row, unsigned int end_row);
void ms912x_downscale_xrgb8888_2x_neon(const u32 *src, u32 *dst,
					      unsigned int src_width,
					      unsigned int src_height,
					      unsigned int dst_width,
					      unsigned int dst_height,
					      unsigned int src_stride,
					      unsigned int dst_stride);
#endif

#endif /* MS912X_DOWNSCALE_2X_H */
