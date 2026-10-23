/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MS912X_PIXELS_H
#define MS912X_PIXELS_H

static inline void *ms912x_pixel_at(void *pixels, unsigned int x, unsigned int y)
{
	return (u32 *)pixels + (size_t)y * MS912X_MAX_WIDTH + x;
}

static inline const void *ms912x_pixel_at_const(const void *pixels,
					      unsigned int x, unsigned int y)
{
	return (const u32 *)pixels + (size_t)y * MS912X_MAX_WIDTH + x;
}

static inline void ms912x_pixels_copy_rect(void *dst, const void *src,
					 const struct drm_rect *rect)
{
	unsigned int y;

	for (y = rect->y1; y < rect->y2; y++)
		memcpy(ms912x_pixel_at(dst, rect->x1, y),
		       ms912x_pixel_at_const(src, rect->x1, y),
		       (size_t)drm_rect_width(rect) * sizeof(u32));
}
#endif
