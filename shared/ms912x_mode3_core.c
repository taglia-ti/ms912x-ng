/* SPDX-License-Identifier: GPL-2.0-only */

#include "ms912x_mode3_core.h"
#include "ms912x_color_core.h"
#include "../ms912x_mode3.h"

#ifdef __KERNEL__
#include <linux/errno.h>
#include <linux/string.h>
#else
#include <errno.h>
#include <string.h>
#endif

size_t ms912x_core_mode3_frame_size(ms912x_mode3_u32 width,
				    ms912x_mode3_u32 height)
{
	if (!width || !height || width > 0xfffU || height > 0xfffU ||
	    width > (size_t)-1 / 2U / height)
		return 0;
	if ((size_t)width * height > (size_t)-1 - MS912X_CORE_MODE3_OVERHEAD)
		return 0;
	return (size_t)width * height * 2U + MS912X_CORE_MODE3_OVERHEAD;
}

int ms912x_core_mode3_write_header(ms912x_mode3_u8 *dst,
				   size_t dst_size,
				   ms912x_mode3_u32 x,
				   ms912x_mode3_u32 y,
				   ms912x_mode3_u32 width,
				   ms912x_mode3_u32 height)
{

	if (!dst || dst_size < MS912X_CORE_MODE3_HEADER_BYTES ||
	    !width || !height || x > 0xfffU || y > 0xfffU ||
	    width > 0xfffU || height > 0xfffU ||
	    x + width > 0x1000U || y + height > 0x1000U)
		return -1;

	ms912x_mode3_write_header(dst, x, y, width, height);
	return 0;
}

void ms912x_core_mode3_write_trailer(ms912x_mode3_u8 *dst, int final_rect)
{
	ms912x_mode3_write_trailer(dst, final_rect);
}

int ms912x_core_mode3_encode_full_frame(ms912x_mode3_u8 *dst,
					 size_t dst_size,
					 const ms912x_mode3_u8 *uyvy,
					 ms912x_mode3_u32 width,
					 ms912x_mode3_u32 height)
{
	size_t frame_size = ms912x_core_mode3_frame_size(width, height);
	struct drm_rect rect = {0, 0, width, height};

	if (!dst || !uyvy || !frame_size || dst_size < frame_size)
		return -1;
	return ms912x_mode3_pack_rects(dst, dst_size, uyvy, (size_t)width * 2,
		width, height, &rect, 1);
}

int ms912x_core_mode3_encode_rectangles(
	ms912x_mode3_u8 *dst, size_t dst_size,
	const ms912x_mode3_u8 *uyvy, size_t row_stride_bytes,
	ms912x_mode3_u32 frame_width, ms912x_mode3_u32 frame_height,
	const struct ms912x_core_rect *rects, size_t rect_count)
{
	return ms912x_mode3_pack_rects(dst, dst_size, uyvy, row_stride_bytes,
		frame_width, frame_height, rects, rect_count);
}

int ms912x_core_mode3_generate_color_bars(ms912x_mode3_u8 *dst,
						 size_t dst_size,
						 ms912x_mode3_u32 width,
						 ms912x_mode3_u32 height)
{
	static const ms912x_mode3_u8 colors[8][3] = {
		{ 255, 255, 255 }, { 255, 255, 0 }, { 0, 255, 255 },
		{ 0, 255, 0 }, { 255, 0, 255 }, { 255, 0, 0 },
		{ 0, 0, 255 }, { 0, 0, 0 },
	};
	size_t frame_size = ms912x_core_mode3_frame_size(width, height);
	size_t pixel_bytes;
	ms912x_mode3_u8 *uyvy;
	ms912x_mode3_u32 y, x;
	int result;

	if (!dst || !frame_size || (width & 1U) || dst_size < frame_size)
		return -1;
	pixel_bytes = frame_size - MS912X_CORE_MODE3_OVERHEAD;
	uyvy = (ms912x_mode3_u8 *)dst + MS912X_CORE_MODE3_HEADER_BYTES;
	for (y = 0; y < height; ++y) {
		for (x = 0; x < width; x += 2) {
			unsigned int bar0 = (x * 8U) / width;
			unsigned int bar1 = ((x + 1U) * 8U) / width;
			unsigned int r0, g0, b0, r1, g1, b1, pair;
			if (y >= height * 4U / 5U) {
				unsigned int level0 = bar0 * 255U / 7U;
				unsigned int level1 = bar1 * 255U / 7U;
				r0 = g0 = b0 = level0;
				r1 = g1 = b1 = level1;
			} else {
				r0 = colors[bar0][0]; g0 = colors[bar0][1]; b0 = colors[bar0][2];
				r1 = colors[bar1][0]; g1 = colors[bar1][1]; b1 = colors[bar1][2];
			}
			pair = ms912x_core_rgb_pair_to_uyvy(r0, g0, b0, r1, g1, b1);
			uyvy[((size_t)y * width + x) * 2U] = (ms912x_mode3_u8)pair;
			uyvy[((size_t)y * width + x) * 2U + 1U] = (ms912x_mode3_u8)(pair >> 8);
			uyvy[((size_t)y * width + x) * 2U + 2U] = (ms912x_mode3_u8)(pair >> 16);
			uyvy[((size_t)y * width + x) * 2U + 3U] = (ms912x_mode3_u8)(pair >> 24);
		}
	}
	ms912x_core_mode3_write_trailer(uyvy + pixel_bytes, 1);
	result = ms912x_core_mode3_write_header(dst, dst_size, 0, 0, width, height);
	return result ? result : (int)frame_size;
}
