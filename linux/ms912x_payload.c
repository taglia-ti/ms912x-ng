// SPDX-License-Identifier: GPL-2.0-only

#include <linux/string.h>

#include "ms912x.h"
#include "ms912x_pixels.h"
#include "ms912x_payload.h"

static bool ms912x_payload_watched(const struct ms912x_device *ms912x,
				   const struct ms912x_usb_request *request)
{
	/* One invalidation is enough until another coherent snapshot is staged. */
	/* PREPARING geometry is private; staging captures the latest source. */
	return request->source_generation == request->content_generation &&
		request->generation == ms912x->frame_generation &&
		(request->state == MS912X_REQUEST_CONVERTING ||
		 request->state == MS912X_REQUEST_USB_QUEUED);
}

void ms912x_payload_invalidate_all(struct ms912x_device *ms912x)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(ms912x->requests); i++)
		if (ms912x_payload_watched(ms912x, &ms912x->requests[i]))
			ms912x->requests[i].content_generation++;
}

void ms912x_payload_invalidate_damage(struct ms912x_device *ms912x,
				      const struct drm_rect *rects,
				      unsigned int count)
{
	unsigned int i, r, d;

	for (i = 0; i < ARRAY_SIZE(ms912x->requests); i++) {
		struct ms912x_usb_request *request = &ms912x->requests[i];

		if (!ms912x_payload_watched(ms912x, request))
			continue;
		for (r = 0; r < request->rect_count; r++) {
			const struct drm_rect *covered = &request->rects[r];

			for (d = 0; d < count; d++)
				if (covered->x1 < rects[d].x2 &&
				    covered->x2 > rects[d].x1 &&
				    covered->y1 < rects[d].y2 &&
				    covered->y2 > rects[d].y1)
					goto changed;
		}
		continue;
changed:
		request->content_generation++;
	}
}

static bool ms912x_payload_frame_changed(const u32 *old, const u32 *frame,
					const struct drm_rect *rects,
					unsigned int count)
{
	unsigned int r, y;

	for (r = 0; r < count; r++) {
		const struct drm_rect *rect = &rects[r];
		size_t pixels = drm_rect_width(rect);

		for (y = rect->y1; y < rect->y2; y++) {
			const void *a = ms912x_pixel_at_const(old, rect->x1, y);
			const void *b = ms912x_pixel_at_const(frame, rect->x1, y);

			if (memcmp(a, b, pixels * 2))
				return true;
		}
	}
	return false;
}

/* Compare all covered pixels, including rows omitted by the damage scan. */
void ms912x_payload_compare_frame(struct ms912x_device *ms912x,
				 const u32 *frame,
				 unsigned int width, unsigned int height,
				 struct ms912x_payload_check *checks)
{
	struct drm_rect *rects = ms912x->payload_compare_rects;
	unsigned int i;

	lockdep_assert_held(&ms912x->snapshot_lock);
	lockdep_assert_not_held(&ms912x->transfer_lock);
	lockdep_assert_not_held(&ms912x->damage_scan_lock);

	for (i = 0; i < ARRAY_SIZE(ms912x->requests); i++) {
		struct ms912x_usb_request *request = &ms912x->requests[i];
		struct ms912x_payload_check *check = &checks[i];
		unsigned int count;

		*check = (struct ms912x_payload_check) {};
		mutex_lock(&ms912x->transfer_lock);
		if (ms912x->transfer_stopping ||
		    !ms912x_payload_watched(ms912x, request)) {
			mutex_unlock(&ms912x->transfer_lock);
			continue;
		}
		check->generation = request->generation;
		check->source_generation = request->source_generation;
		count = request->rect_count;
		if (request->width != width || request->height != height ||
		    WARN_ON_ONCE(count > MS912X_MAX_BATCH_RECTS)) {
			check->changed = true;
			mutex_unlock(&ms912x->transfer_lock);
			continue;
		}
		/* A completed request can be reused/reallocated during comparison. */
		memcpy(rects, request->rects, count * sizeof(*rects));
		mutex_unlock(&ms912x->transfer_lock);
		check->changed = ms912x_payload_frame_changed(
			ms912x->latest_pixels, frame, rects, count);
	}
}

void ms912x_payload_invalidate_compared(struct ms912x_device *ms912x,
				      const struct ms912x_payload_check *checks)
{
	unsigned int i;

	lockdep_assert_held(&ms912x->snapshot_lock);
	lockdep_assert_held(&ms912x->transfer_lock);
	if (ms912x->transfer_stopping)
		return;
	for (i = 0; i < ARRAY_SIZE(ms912x->requests); i++) {
		struct ms912x_usb_request *request = &ms912x->requests[i];
		const struct ms912x_payload_check *check = &checks[i];

		/*
		 * snapshot_lock prevents restaging while we compare. Reused requests
		 * are at most PREPARING; CONVERTING -> USB_QUEUED retains coverage.
		 */
		if (check->changed && ms912x_payload_watched(ms912x, request) &&
		    request->generation == check->generation &&
		    request->source_generation == check->source_generation)
			request->content_generation++;
	}
}
