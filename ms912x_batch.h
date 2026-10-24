/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MS912X_BATCH_H
#define MS912X_BATCH_H

#include "ms912x_platform.h"
#include "ms912x_rect_limits.h"

struct drm_rect;

/* Private to one PREPARING request; never shared with pending-damage producers. */
#define MS912X_COMPACT_INDEX_BITS 15
#define MS912X_COMPACT_INDEX_SLOTS (1U << MS912X_COMPACT_INDEX_BITS)
#if MS912X_MAX_BATCH_RECTS > MS912X_COMPACT_INDEX_SLOTS / 2 + 1
#error "The compaction index must hold a maximum batch at half load"
#endif
struct ms912x_compact_index {
	struct {
		u32 key, epoch, index;
	} entries[MS912X_COMPACT_INDEX_SLOTS];
	u32 epoch;
};

#define MS912X_BATCH_EXTRA_DIVISOR 256
#define MS912X_BATCH_SMALL_AREA 128
#define MS912X_BATCH_TAPER_END_AREA 2048

u64 ms912x_batch_extra_limit(u64 covered_pixels);
u64 ms912x_rects_payload_bytes(const struct drm_rect *rects, unsigned int count);
void ms912x_rects_compact_exact(struct drm_rect *rects, unsigned int *count);
void ms912x_rects_compact_indexed(struct drm_rect *rects, unsigned int *count,
				u64 maximum_bytes, struct ms912x_compact_index *index);
/* Inputs are clipped to one frame; output capacity is at least one rectangle. */
void ms912x_batch_build(struct drm_rect *rects, unsigned int *count,
			unsigned int capacity,
			const struct drm_rect *current_rects, unsigned int current_count,
			const struct drm_rect *previous, unsigned int previous_count,
			u64 maximum_bytes, struct ms912x_compact_index *index);
void ms912x_batch_include_overlay(struct drm_rect *rects, unsigned int *count,
				 unsigned int capacity,
				 const struct drm_rect *overlay, u64 maximum_bytes);
bool ms912x_batch_backpressured(u64 pressure_ns, u64 low_watermark_ns,
			       u64 high_watermark_ns, bool was_backpressured);
u64 ms912x_batch_coalesce_ns(u64 bytes, u64 half_frame_ns, u64 pressure_ns,
			     u64 high_watermark_ns, bool backpressured,
			     bool force_full);

#endif /* MS912X_BATCH_H */
