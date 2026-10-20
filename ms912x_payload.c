// SPDX-License-Identifier: GPL-2.0-only

#include <linux/string.h>

#include "ms912x.h"
#include "ms912x_payload.h"

static bool ms912x_payload_watched(const struct ms912x_device *ms912x,
				   const struct ms912x_usb_request *request)
{
	/* One invalidation is enough until another coherent snapshot is staged. */
	return request->source_generation == request->content_generation &&
		request->generation == ms912x->frame_generation &&
		(request->state == MS912X_REQUEST_CONVERTING ||
		 request->state == MS912X_REQUEST_USB_QUEUED);
}

static bool ms912x_payload_rgb_row_equal(const u32 *old, const u32 *new,
					 size_t pixels)
{
	if (!memcmp(old, new, pixels * sizeof(*old)))
		return true;
	while (pixels--)
		if (((*old++ ^ *new++) & 0x00ffffffU) != 0)
			return false;
	return true;
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

/* Compare before publishing the complete frame, including unsampled pixels. */
void ms912x_payload_invalidate_frame(struct ms912x_device *ms912x,
				     const u32 *frame,
				     unsigned int width, unsigned int height)
{
	unsigned int i, r, y;

	for (i = 0; i < ARRAY_SIZE(ms912x->requests); i++) {
		struct ms912x_usb_request *request = &ms912x->requests[i];

		if (!ms912x_payload_watched(ms912x, request))
			continue;
		if (request->width != width || request->height != height)
			goto changed;
		for (r = 0; r < request->rect_count; r++) {
			const struct drm_rect *rect = &request->rects[r];
			size_t pixels = drm_rect_width(rect);

			for (y = rect->y1; y < rect->y2; y++) {
				size_t offset = y * MS912X_MAX_WIDTH + rect->x1;

				if (!ms912x_payload_rgb_row_equal(
					    ms912x->latest_xrgb + offset,
					    frame + offset, pixels))
					goto changed;
			}
		}
		continue;
changed:
		request->content_generation++;
	}
}
