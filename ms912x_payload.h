/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MS912X_PAYLOAD_H
#define MS912X_PAYLOAD_H

#include <linux/types.h>

struct drm_rect;
struct ms912x_device;

/* All callers hold transfer_lock, including source publication and staging. */
void ms912x_payload_invalidate_all(struct ms912x_device *ms912x);
void ms912x_payload_invalidate_damage(struct ms912x_device *ms912x,
                                     const struct drm_rect *rects,
                                     unsigned int count);
void ms912x_payload_invalidate_frame(struct ms912x_device *ms912x,
                                    const u32 *frame,
                                    unsigned int width, unsigned int height);

#endif /* MS912X_PAYLOAD_H */
