// SPDX-License-Identifier: GPL-2.0-only

#include "ms912x_mode3.h"

/* Serialize UYVY from either fixed kernel pitch or an ImageReader-owned snapshot.
 * Validate the entire batch before touching output; return encoded byte count.
 */
int ms912x_mode3_pack_rects(u8 *dst, size_t capacity,
	const u8 *src, size_t pitch, unsigned int width, unsigned int height,
	const struct drm_rect *rects, size_t count)
{
	size_t total = 0, offset = 0, i;

	if (!dst || !src || !rects || !count || !width || !height ||
	    width > 0xfffU || height > 0xfffU || (width & 1U) ||
	    pitch < (size_t)width * 2 || pitch > (size_t)-1 / height)
		return -EINVAL;
	for (i = 0; i < count; ++i) {
		const struct drm_rect *r = &rects[i];
		size_t bytes;

		if (r->x1 < 0 || r->y1 < 0 || r->x2 <= r->x1 || r->y2 <= r->y1 ||
		    (unsigned int)r->x2 > width || (unsigned int)r->y2 > height || (r->x1 & 1) || (r->x2 & 1))
			return -EINVAL;
		bytes = ms912x_mode3_rect_encoded_bytes(drm_rect_width(r), drm_rect_height(r));
		if (bytes > (size_t)-1 - total)
			return -EOVERFLOW;
		total += bytes;
	}
	if (total > capacity)
		return -ENOSPC;
	if (total > 0x7fffffffU)
		return -EOVERFLOW;
	for (i = 0; i < count; ++i) {
		const struct drm_rect *r = &rects[i];
		unsigned int y;
		size_t bytes = (size_t)drm_rect_width(r) * 2;

		ms912x_mode3_write_header(dst + offset, r->x1, r->y1,
			drm_rect_width(r), drm_rect_height(r));
		offset += MS912X_MODE3_HEADER_BYTES;
		for (y = r->y1; y < (unsigned int)r->y2; ++y) {
			memcpy(dst + offset, src + (size_t)y * pitch + (size_t)r->x1 * 2, bytes);
			offset += bytes;
		}
		ms912x_mode3_write_trailer(dst + offset, i + 1 == count);
		offset += MS912X_MODE3_TRAILER_BYTES;
	}
	return (int)total;
}
