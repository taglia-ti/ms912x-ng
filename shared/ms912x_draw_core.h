/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MS912X_DRAW_CORE_H
#define MS912X_DRAW_CORE_H

#include "../ms912x_platform.h"

/* Pair-aligned rectangles; payload excludes the transport header and trailer. */
static inline void ms912x_core_draw_uyvy_border(u8 *payload,
		const struct drm_rect *transfer_rect, const struct drm_rect *new_damage_rect)
{
	unsigned int transfer_width = drm_rect_width(transfer_rect);
	int x1 = max(transfer_rect->x1, new_damage_rect->x1);
	int y1 = max(transfer_rect->y1, new_damage_rect->y1);
	int x2 = min(transfer_rect->x2, new_damage_rect->x2);
	int y2 = min(transfer_rect->y2, new_damage_rect->y2);
	int y;

	if (x1 >= x2 || y1 >= y2)
		return;

	for (y = y1; y < y2; y++) {
		int x;
		u8 *line = payload + (y - transfer_rect->y1) *
			transfer_width * 2;
		bool horizontal_edge = y < new_damage_rect->y1 + 2 ||
			y >= new_damage_rect->y2 - 2;

		if (horizontal_edge) {
			for (x = (x1 & ~1); x < x2; x += 2) {
				u8 *pair = line + (x - transfer_rect->x1) * 2;

				pair[0] ^= 0xff;
				pair[1] ^= 0xff;
				pair[2] ^= 0xff;
				pair[3] ^= 0xff;
			}
		} else {
			u8 *left = line + (x1 - transfer_rect->x1) * 2;
			u8 *right = line + (x2 - transfer_rect->x1 - 2) * 2;

			left[0] ^= 0xff;
			left[1] ^= 0xff;
			left[2] ^= 0xff;
			left[3] ^= 0xff;
			if (x2 - x1 > 2) {
				right[0] ^= 0xff;
				right[1] ^= 0xff;
				right[2] ^= 0xff;
				right[3] ^= 0xff;
			}
		}
	}
}

#endif
