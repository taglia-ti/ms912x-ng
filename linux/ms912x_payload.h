/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MS912X_PAYLOAD_H
#define MS912X_PAYLOAD_H

#include <linux/types.h>

struct drm_rect;
struct ms912x_device;

struct ms912x_payload_check {
	u64 source_generation;
	u32 generation;
	bool changed;
};

/* Callers hold transfer_lock. */
void ms912x_payload_invalidate_all(struct ms912x_device *ms912x);
void ms912x_payload_invalidate_damage(struct ms912x_device *ms912x,
                                     const struct drm_rect *rects,
                                     unsigned int count);
/*
 * Keep snapshot_lock from comparison through publication. The incoming frame
 * must remain privately owned. Provide one check per device request.
 * Comparison takes transfer_lock only to snapshot coverage, with neither
 * transfer_lock nor damage_scan_lock held by the caller.
 */
void ms912x_payload_compare_frame(struct ms912x_device *ms912x,
				 const u32 *frame,
				 unsigned int width, unsigned int height,
				 struct ms912x_payload_check *checks);
/* Apply under transfer_lock only when the compared frame will be published. */
void ms912x_payload_invalidate_compared(struct ms912x_device *ms912x,
				      const struct ms912x_payload_check *checks);

#endif /* MS912X_PAYLOAD_H */
