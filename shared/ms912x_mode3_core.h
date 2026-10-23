/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MS912X_MODE3_CORE_H
#define MS912X_MODE3_CORE_H

#ifdef __KERNEL__
#include <linux/stddef.h>
#include <linux/types.h>
typedef u8 ms912x_mode3_u8;
typedef u32 ms912x_mode3_u32;
#else
#include <stddef.h>
#include <stdint.h>
typedef uint8_t ms912x_mode3_u8;
typedef uint32_t ms912x_mode3_u32;
#endif
#include "ms912x_scan_core.h"
#include "../ms912x_mode3.h"

#define MS912X_CORE_MODE3_HEADER_BYTES MS912X_MODE3_HEADER_BYTES
#define MS912X_CORE_MODE3_TRAILER_BYTES MS912X_MODE3_TRAILER_BYTES
#define MS912X_CORE_MODE3_OVERHEAD MS912X_MODE3_RECT_OVERHEAD

size_t ms912x_core_mode3_frame_size(ms912x_mode3_u32 width,
				    ms912x_mode3_u32 height);
int ms912x_core_mode3_write_header(ms912x_mode3_u8 *dst, size_t dst_size,
				   ms912x_mode3_u32 x,
				   ms912x_mode3_u32 y,
				   ms912x_mode3_u32 width,
				   ms912x_mode3_u32 height);
void ms912x_core_mode3_write_trailer(ms912x_mode3_u8 *dst,
				     int final_rect);
int ms912x_core_mode3_encode_full_frame(ms912x_mode3_u8 *dst,
					 size_t dst_size,
					 const ms912x_mode3_u8 *uyvy,
					 ms912x_mode3_u32 width,
					 ms912x_mode3_u32 height);
int ms912x_core_mode3_encode_rectangles(
	ms912x_mode3_u8 *dst, size_t dst_size,
	const ms912x_mode3_u8 *uyvy, size_t row_stride_bytes,
	ms912x_mode3_u32 frame_width, ms912x_mode3_u32 frame_height,
	const struct ms912x_core_rect *rects, size_t rect_count);
int ms912x_core_mode3_generate_color_bars(ms912x_mode3_u8 *dst,
						 size_t dst_size,
						 ms912x_mode3_u32 width,
						 ms912x_mode3_u32 height);

#endif
