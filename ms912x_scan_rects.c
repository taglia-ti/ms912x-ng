// SPDX-License-Identifier: GPL-2.0-only

#include "ms912x.h"
#include "ms912x_scan_rects.h"

bool ms912x_scan_rect_add(struct ms912x_scan_rect_index *index,
                         struct drm_rect **rects, unsigned int *capacity,
                         unsigned int *count, const struct drm_rect *rect,
                         unsigned int width, unsigned int height)
{
	return ms912x_scan_rect_add_with_growth(index, rects, capacity, count,
					       rect, width, height,
					       ms912x_rect_vector_grow);
}
