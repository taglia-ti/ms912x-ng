// SPDX-License-Identifier: GPL-2.0-only

#include <assert.h>
#include <stdio.h>

#include "test_rect_compat.h"

struct ms912x_device {
	struct drm_rect failed_rect;
	unsigned int failed_width, failed_height, failed_generation;
	bool failed_rect_valid;
};

struct ms912x_usb_request {
	struct drm_rect *rects;
	unsigned int rect_count, width, height, generation;
};

#include "failed-replay.h"

int main(void)
{
	struct ms912x_device first = { 0 }, second = { 0 };
	struct drm_rect first_rects[] = { DRM_RECT_INIT(32, 48, 16, 12) };
	struct drm_rect second_rects[] = { DRM_RECT_INIT(400, 300, 24, 20) };
	struct drm_rect first_full[] = { DRM_RECT_INIT(0, 0, 640, 240) };
	struct drm_rect second_full[] = { DRM_RECT_INIT(0, 0, 320, 480) };
	struct ms912x_usb_request first_request = {
		.rects = first_rects, .rect_count = 1,
		.width = 640, .height = 240, .generation = 7,
	};
	struct ms912x_usb_request second_request = {
		.rects = second_rects, .rect_count = 1,
		.width = 320, .height = 480, .generation = 12,
	};

	ms912x_usb_carry_failed_rect_locked(&first, &first_request);
	assert(first.failed_rect_valid);
	assert(first.failed_width == 640 && first.failed_height == 240);
	assert(first.failed_generation == 7);
	assert(first.failed_rect.x1 == 0 && first.failed_rect.y1 == 0);
	assert(first.failed_rect.x2 == 640 && first.failed_rect.y2 == 240);
	assert(!second.failed_rect_valid);

	ms912x_usb_carry_failed_rect_locked(&second, &second_request);
	assert(second.failed_rect_valid);
	assert(second.failed_width == 320 && second.failed_height == 480);
	assert(second.failed_generation == 12);
	first_request.rects = first_full;
	second_request.rects = second_full;
	assert(ms912x_usb_request_covers_rect(&first_request, &first.failed_rect));
	assert(ms912x_usb_request_covers_rect(&second_request, &second.failed_rect));
	assert(!ms912x_usb_request_covers_rect(&first_request, &second.failed_rect));
	assert(!ms912x_usb_request_covers_rect(&second_request, &first.failed_rect));

	first.failed_rect_valid = false;
	assert(second.failed_rect_valid && second.failed_generation == 12);
	puts("PASS replay isolation: failed damage geometry and generation stay per-adapter");
	return 0;
}
