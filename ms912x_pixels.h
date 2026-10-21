/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MS912X_PIXELS_H
#define MS912X_PIXELS_H

/* Coordinates are pixels; only the scan workers count 32-bit words. */
static inline unsigned int ms912x_pixel_bytes(bool rgb565)
{
	return rgb565 ? 2 : 4;
}

static inline unsigned int ms912x_pixel_row_words(bool rgb565)
{
	return MS912X_MAX_WIDTH >> rgb565;
}

static inline void *ms912x_pixel_at(void *pixels, bool rgb565, unsigned int x,
				    unsigned int y)
{
	return (u8 *)pixels +
	       ((size_t)y * MS912X_MAX_WIDTH + x) * ms912x_pixel_bytes(rgb565);
}

static inline const void *ms912x_pixel_at_const(const void *pixels, bool rgb565,
						unsigned int x, unsigned int y)
{
	return (const u8 *)pixels +
	       ((size_t)y * MS912X_MAX_WIDTH + x) * ms912x_pixel_bytes(rgb565);
}

static inline void ms912x_pixels_copy_rect(void *dst, const void *src,
					   const struct drm_rect *rect,
					   bool rgb565)
{
	unsigned int y;
	size_t bytes =
		(size_t)drm_rect_width(rect) * ms912x_pixel_bytes(rgb565);

	for (y = rect->y1; y < rect->y2; y++)
		memcpy(ms912x_pixel_at(dst, rgb565, rect->x1, y),
		       ms912x_pixel_at_const(src, rgb565, rect->x1, y), bytes);
}

#endif
