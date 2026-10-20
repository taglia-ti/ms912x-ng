/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MS912X_CURSOR_H
#define MS912X_CURSOR_H

#include <linux/types.h>
#include <linux/string.h>

#define MS912X_CURSOR_MAX_WIDTH 128
#define MS912X_CURSOR_MAX_HEIGHT 128

static inline bool ms912x_cursor_source_matches(const u32 *candidate,
						const u32 *cached,
						unsigned int width,
						unsigned int height)
{
	unsigned int y;

	if (!candidate || !cached || !width || !height ||
	    width > MS912X_CURSOR_MAX_WIDTH ||
	    height > MS912X_CURSOR_MAX_HEIGHT)
		return false;
	for (y = 0; y < height; y++)
		if (memcmp(candidate + y * MS912X_CURSOR_MAX_WIDTH,
			   cached + y * MS912X_CURSOR_MAX_WIDTH,
			   width * sizeof(*candidate)))
			return false;
	return true;
}

static inline unsigned int
ms912x_cursor_source_index(unsigned int x, unsigned int y,
			   unsigned int alpha_x, unsigned int alpha_y)
{
	return (y + alpha_y) * MS912X_CURSOR_MAX_WIDTH + x + alpha_x;
}

static inline bool ms912x_cursor_alpha_bounds(const u32 *pixels,
					     unsigned int fb_width,
					     unsigned int fb_height,
					     unsigned int *offset_x,
					     unsigned int *offset_y,
					     unsigned int *width,
					     unsigned int *height)
{
	unsigned int min_x = fb_width, min_y = fb_height;
	unsigned int max_x = 0, max_y = 0, x, y;

	for (y = 0; y < fb_height; y++) {
		for (x = 0; x < fb_width; x++) {
			if (!(pixels[y * MS912X_CURSOR_MAX_WIDTH + x] >> 24))
				continue;
			if (x < min_x)
				min_x = x;
			if (y < min_y)
				min_y = y;
			if (x > max_x)
				max_x = x;
			if (y > max_y)
				max_y = y;
		}
	}
	if (min_x == fb_width) {
		*offset_x = 0;
		*offset_y = 0;
		*width = 0;
		*height = 0;
		return false;
	}

	*offset_x = min_x;
	*offset_y = min_y;
	*width = max_x - min_x + 1;
	*height = max_y - min_y + 1;
	return true;
}

#endif /* MS912X_CURSOR_H */