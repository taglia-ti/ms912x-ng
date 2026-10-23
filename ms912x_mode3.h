/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MS912X_MODE3_H
#define MS912X_MODE3_H

#include "ms912x_platform.h"
#ifdef __KERNEL__
#include <linux/errno.h>
#else
#include <errno.h>
#endif

#define MS912X_MODE3_HEADER_BYTES 8
#define MS912X_MODE3_TRAILER_BYTES 8
#define MS912X_MODE3_RECT_OVERHEAD \
	(MS912X_MODE3_HEADER_BYTES + MS912X_MODE3_TRAILER_BYTES)

/* Coordinates and dimensions have been validated by the caller. */
static inline void ms912x_mode3_write_header(u8 *dst, unsigned int x,
	unsigned int y, unsigned int width, unsigned int height)
{
	u32 position = ((x & 0xfffU) << 12) | (y & 0xfffU);
	u32 dimensions = ((width & 0xfffU) << 12) | (height & 0xfffU);
	dst[0] = 0xff;
	dst[1] = 0;
	dst[2] = position >> 16;
	dst[3] = position >> 8;
	dst[4] = position;
	dst[5] = dimensions >> 16;
	dst[6] = dimensions >> 8;
	dst[7] = dimensions;
}

static inline bool ms912x_mode3_append_rects(struct drm_rect *dst,
					     unsigned int capacity,
					     unsigned int *dst_count,
					     const struct drm_rect *src,
					     unsigned int src_count)
{
	unsigned int i;

	if (*dst_count > capacity || src_count > capacity - *dst_count)
		return false;
	for (i = 0; i < src_count; i++)
		dst[(*dst_count)++] = src[i];
	return true;
}

static inline size_t ms912x_mode3_rect_encoded_bytes(unsigned int width,
						     unsigned int height)
{
	return (size_t)width * height * 2 + MS912X_MODE3_RECT_OVERHEAD;
}

static inline void ms912x_mode3_write_trailer(u8 *dst, bool final_rect)
{
	dst[0] = 0xff;
	dst[1] = final_rect ? 0xc0 : 0x80;
	memset(dst + 2, 0, MS912X_MODE3_TRAILER_BYTES - 2);
}

int ms912x_mode3_pack_rects(u8 *dst, size_t capacity,
	const u8 *src, size_t pitch, unsigned int width, unsigned int height,
	const struct drm_rect *rects, size_t count);

#endif /* MS912X_MODE3_H */
