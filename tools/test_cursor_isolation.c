// SPDX-License-Identifier: GPL-2.0-only

#include <stdio.h>

#include "test_cursor_entry_compat.h"

#define MS912X_CURSOR_MAX_WIDTH 128

#include "cursor-snapshot.h"

int main(void)
{
	struct ms912x_device first = { 0 }, second = { 0 };
	struct ms912x_usb_request first_request = { 0 }, second_request = { 0 };
	u32 first_pixels[128 * 128], second_pixels[128 * 128];
	struct drm_rect first_rect = DRM_RECT_INIT(10, 20, 8, 8);
	struct drm_rect second_rect = DRM_RECT_INIT(100, 200, 8, 8);

	first.cursor_pixels = first_pixels;
	first.cursor_enabled = true;
	first.cursor_x = 10;
	first.cursor_y = 20;
	first.cursor_width = 8;
	first.cursor_height = 8;
	first.cursor_source_height = 16;
	first.cursor_pixels[0] = 0xff123456;

	second.cursor_pixels = second_pixels;
	second.cursor_enabled = true;
	second.cursor_x = 100;
	second.cursor_y = 200;
	second.cursor_width = 8;
	second.cursor_height = 8;
	second.cursor_source_height = 16;
	second.cursor_pixels[0] = 0xffabcdef;

	ms912x_cursor_snapshot_request_locked(&first, &first_request,
		&first_rect, 1);
	ms912x_cursor_snapshot_request_locked(&second, &second_request,
		&second_rect, 1);

	assert(first_request.cursor_included && second_request.cursor_included);
	assert(first_request.cursor_x == 10 && second_request.cursor_x == 100);
	assert(first_request.cursor_pixels[0] == 0xff123456);
	assert(second_request.cursor_pixels[0] == 0xffabcdef);

	second.cursor_pixels[0] = 0xff000000;
	ms912x_cursor_snapshot_request_locked(&second, &second_request,
		&first_rect, 1);
	assert(!second_request.cursor_included);
	assert(first_request.cursor_pixels[0] == 0xff123456);
	assert(second_request.cursor_pixels[0] == 0xffabcdef);
	assert(first.cursor_pixels[0] == 0xff123456);

	puts("PASS cursor isolation: per-adapter snapshots and pixels stay independent");
	return 0;
}
