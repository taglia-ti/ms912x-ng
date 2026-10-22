/* SPDX-License-Identifier: GPL-2.0-only */
#include "pipeline-compat.h"
struct pipeline {
	bool rgb565, scan;
	unsigned width, height, stride, count, capacity;
	u64 sequence;
	u32 *sources[2], *capture, *baseline, *next, *staging;
	u8 *output;
	struct drm_rect *rects;
	struct ms912x_scan_rect_index *index;
	bool simd;
};

void pipeline_init(struct pipeline *, unsigned, unsigned, unsigned, bool, bool,
		   const struct drm_rect *, bool);
size_t pipeline_frame(struct pipeline *);
void pipeline_destroy(struct pipeline *);
