/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MS912X_SCAN_RECTS_H
#define MS912X_SCAN_RECTS_H

#include "ms912x_platform.h"

#define MS912X_SCAN_MAX_HEIGHT 8192
/* Shared UYVY geometry: eight words cover sixteen pixels. */
#define MS912X_SCAN_UYVY_RUN_WORDS 8

typedef struct drm_rect *(*ms912x_scan_rect_grow_fn)(
	struct drm_rect *, unsigned int *, unsigned int, unsigned int);

struct drm_rect;
struct ms912x_scan_rect_index;

struct ms912x_scan_rect_index *ms912x_scan_rect_index_alloc(void);
void ms912x_scan_rect_index_free(struct ms912x_scan_rect_index *index);
/* Reset alongside an empty vector; added rectangles must be clipped to the frame. */
void ms912x_scan_rect_index_reset(struct ms912x_scan_rect_index *index,
                                 unsigned int height);
/* A NULL growth callback uses fixed storage and the same overflow fallback. */
bool ms912x_scan_rect_add_with_growth(struct ms912x_scan_rect_index *index,
                         struct drm_rect **rects, unsigned int *capacity,
                         unsigned int *count, const struct drm_rect *rect,
                         unsigned int width, unsigned int height,
                         ms912x_scan_rect_grow_fn grow);
/* Return true only when vector exhaustion requires a bounding rectangle. */
bool ms912x_scan_rect_add(struct ms912x_scan_rect_index *index,
                         struct drm_rect **rects, unsigned int *capacity,
                         unsigned int *count, const struct drm_rect *rect,
                         unsigned int width, unsigned int height);

#endif /* MS912X_SCAN_RECTS_H */
