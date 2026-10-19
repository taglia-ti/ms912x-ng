/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MS912X_MODE3_H
#define MS912X_MODE3_H

#include <linux/types.h>

#define MS912X_MODE3_HEADER_BYTES 8
#define MS912X_MODE3_TRAILER_BYTES 8
#define MS912X_MODE3_RECT_OVERHEAD \
	(MS912X_MODE3_HEADER_BYTES + MS912X_MODE3_TRAILER_BYTES)

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

#endif /* MS912X_MODE3_H */