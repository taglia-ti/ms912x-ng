/* SPDX-License-Identifier: GPL-2.0-only */

#include "ms912x_mode3_core.h"
#include "ms912x_color_core.h"

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
	ms912x_mode3_u32 position, dimensions;

	if (!dst || dst_size < MS912X_CORE_MODE3_HEADER_BYTES ||
	    !width || !height || x > 0xfffU || y > 0xfffU ||
	    width > 0xfffU || height > 0xfffU ||
	    x + width > 0x1000U || y + height > 0x1000U)
		return -1;

	dst[0] = 0xff;
	dst[1] = 0x00;
	position = (x << 12) | y;
	dimensions = (width << 12) | height;
	dst[2] = (ms912x_mode3_u8)(position >> 16);
	dst[3] = (ms912x_mode3_u8)(position >> 8);
	dst[4] = (ms912x_mode3_u8)position;
	dst[5] = (ms912x_mode3_u8)(dimensions >> 16);
	dst[6] = (ms912x_mode3_u8)(dimensions >> 8);
	dst[7] = (ms912x_mode3_u8)dimensions;
	return 0;
}

void ms912x_core_mode3_write_trailer(ms912x_mode3_u8 *dst, int final_rect)
{
	dst[0] = 0xff;
	dst[1] = final_rect ? 0xc0 : 0x80;
	for (unsigned int i = 2; i < MS912X_CORE_MODE3_TRAILER_BYTES; ++i)
		dst[i] = 0;
}

int ms912x_core_mode3_encode_full_frame(ms912x_mode3_u8 *dst,
					 size_t dst_size,
					 const ms912x_mode3_u8 *uyvy,
					 ms912x_mode3_u32 width,
					 ms912x_mode3_u32 height)
{
	size_t frame_size = ms912x_core_mode3_frame_size(width, height);
	size_t pixel_bytes;

	if (!dst || !uyvy || !frame_size || dst_size < frame_size)
		return -1;
	if (ms912x_core_mode3_write_header(dst, dst_size, 0, 0, width, height))
		return -1;
	pixel_bytes = frame_size - MS912X_CORE_MODE3_OVERHEAD;
	for (size_t i = 0; i < pixel_bytes; ++i)
		dst[MS912X_CORE_MODE3_HEADER_BYTES + i] = uyvy[i];
	ms912x_core_mode3_write_trailer(dst + MS912X_CORE_MODE3_HEADER_BYTES +
					pixel_bytes, 1);
	return (int)frame_size;
}

static int mode3_rect_size(const struct ms912x_core_rect *rect,
			   ms912x_mode3_u32 frame_width,
			   ms912x_mode3_u32 frame_height, size_t *size)
{
	size_t width, height, pixels;

	if (!rect || !size || rect->x1 < 0 || rect->y1 < 0 ||
	    rect->x2 <= rect->x1 || rect->y2 <= rect->y1 ||
	    (rect->x1 & 1) || ((rect->x2 - rect->x1) & 1) ||
	    (ms912x_mode3_u32)rect->x2 > frame_width ||
	    (ms912x_mode3_u32)rect->y2 > frame_height)
		return -EINVAL;
	width = (size_t)(rect->x2 - rect->x1);
	height = (size_t)(rect->y2 - rect->y1);
	if (width > (size_t)-1 / 2U / height)
		return -EOVERFLOW;
	pixels = width * height * 2U;
	if (pixels > (size_t)-1 - MS912X_CORE_MODE3_OVERHEAD)
		return -EOVERFLOW;
	*size = pixels + MS912X_CORE_MODE3_OVERHEAD;
	return 0;
}

int ms912x_core_mode3_encode_rectangles(
	ms912x_mode3_u8 *dst, size_t dst_size,
	const ms912x_mode3_u8 *uyvy, size_t row_stride_bytes,
	ms912x_mode3_u32 frame_width, ms912x_mode3_u32 frame_height,
	const struct ms912x_core_rect *rects, size_t rect_count)
{
	size_t total = 0, i, offset = 0;

	if (!dst || !uyvy || !rects || !rect_count ||
	    frame_width > 0xfffU || frame_height > 0xfffU ||
	    (frame_width & 1U) || row_stride_bytes < (size_t)frame_width * 2U)
		return -EINVAL;
	for (i = 0; i < rect_count; ++i) {
		size_t size;
		int ret = mode3_rect_size(&rects[i], frame_width, frame_height, &size);

		if (ret)
			return ret;
		if (total > (size_t)-1 - size)
			return -EOVERFLOW;
		total += size;
	}
	if (total > dst_size)
		return -ENOSPC;
	for (i = 0; i < rect_count; ++i) {
		const struct ms912x_core_rect *rect = &rects[i];
		size_t width = (size_t)(rect->x2 - rect->x1);
		size_t height = (size_t)(rect->y2 - rect->y1);
		size_t row;

		if (ms912x_core_mode3_write_header(dst + offset, total - offset,
						   rect->x1, rect->y1,
						   width, height))
			return -EINVAL;
		offset += MS912X_CORE_MODE3_HEADER_BYTES;
		for (row = 0; row < height; ++row) {
			const ms912x_mode3_u8 *source = uyvy +
				(size_t)(rect->y1 + (int)row) * row_stride_bytes +
				(size_t)rect->x1 * 2U;
			memcpy(dst + offset, source, width * 2U);
			offset += width * 2U;
		}
		ms912x_core_mode3_write_trailer(dst + offset, i + 1U == rect_count);
		offset += MS912X_CORE_MODE3_TRAILER_BYTES;
	}
	return (int)total;
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
