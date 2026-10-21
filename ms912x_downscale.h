/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MS912X_DOWNSCALE_H
#define MS912X_DOWNSCALE_H

#define MS912X_DOWNSCALE_ROWS_PER_FPU 16

#include "ms912x_scale.h"
#include "ms912x_downscale_2x.h"
#include "ms912x_downscale_3to2.h"
#include "ms912x_downscale_5to4.h"

void ms912x_downscale_xrgb8888(enum ms912x_scale_mode mode,
		const u32 *src, u32 *dst,
		unsigned int src_width, unsigned int src_height,
		unsigned int dst_width, unsigned int dst_height,
		unsigned int src_stride, unsigned int dst_stride);
void ms912x_downscale_xrgb8888_region(enum ms912x_scale_mode mode,
		const u32 *src, u32 *dst, unsigned int src_width,
		unsigned int src_height, unsigned int dst_width,
		unsigned int dst_height, unsigned int src_stride,
		unsigned int dst_stride, unsigned int offset_x,
		unsigned int offset_y);

#endif /* MS912X_DOWNSCALE_H */
