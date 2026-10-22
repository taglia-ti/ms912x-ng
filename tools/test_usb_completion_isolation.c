// SPDX-License-Identifier: GPL-2.0-only

#include <assert.h>
#include <errno.h>
#include <stdio.h>

#include "test_rect_compat.h"

typedef uint32_t u32;
typedef uint64_t u64;

#define NSEC_PER_SEC 1000000000ULL
#define MS912X_STAT_INC(device, counter) ((void)(device))

enum ms912x_request_state {
	MS912X_REQUEST_FREE,
	MS912X_REQUEST_USB_QUEUED,
};

struct ms912x_device {
	struct drm_rect failed_rect;
	unsigned int failed_width, failed_height, failed_generation;
	unsigned int frame_generation;
	unsigned int usb_speed_samples, usb_successful_transfers;
	u64 usb_speed_mean_bytes_per_sec;
	bool failed_rect_valid, transfer_stopping, pending_valid;
	unsigned int pending_deadline;
	int frame_present_wait;
};

struct ms912x_usb_request {
	struct ms912x_device *ms912x;
	struct drm_rect rects[1];
	unsigned int rect_count, width, height, generation;
	size_t transfer_len;
	enum ms912x_request_state state;
	bool replay_included;
};

static unsigned int scheduled, pacing_updates, wakeups;

static u64 div64_u64(u64 numerator, u64 denominator)
{
	return numerator / denominator;
}

static void wake_up_all(int *wait)
{
	(void)wait;
	wakeups++;
}

static void ms912x_schedule_conversion_locked(struct ms912x_device *device)
{
	(void)device;
	scheduled++;
}

static void ms912x_transfer_update_pacing_locked(struct ms912x_device *device)
{
	(void)device;
	pacing_updates++;
}

#include "usb-completion.h"

int main(void)
{
	struct ms912x_device first = { .frame_generation = 7 };
	struct ms912x_device second = { .frame_generation = 12 };
	struct ms912x_usb_request first_request = {
		.ms912x = &first, .rects = { DRM_RECT_INIT(32, 48, 16, 12) },
		.rect_count = 1, .width = 640, .height = 240,
		.generation = 7, .transfer_len = 4096,
		.state = MS912X_REQUEST_USB_QUEUED,
	};
	struct ms912x_usb_request second_request = {
		.ms912x = &second, .rects = { DRM_RECT_INIT(160, 120, 24, 20) },
		.rect_count = 1, .width = 320, .height = 480,
		.generation = 12, .transfer_len = 2048,
		.state = MS912X_REQUEST_USB_QUEUED,
	};

	ms912x_usb_request_complete_locked(&first_request, -EIO, 0);
	assert(first.failed_rect_valid && first.failed_generation == 7);
	assert(first.failed_width == 640 && first.failed_height == 240);
	assert(first.failed_rect.x2 == 640 && first.failed_rect.y2 == 240);
	assert(!second.failed_rect_valid && !second.failed_generation);
	assert(first_request.state == MS912X_REQUEST_FREE);
	assert(second_request.state == MS912X_REQUEST_USB_QUEUED);
	assert(!first.usb_successful_transfers && !second.usb_successful_transfers);
	assert(scheduled == 0 && pacing_updates == 1 && wakeups == 1);

	second.pending_valid = true;
	ms912x_usb_request_complete_locked(&second_request, -EIO, 0);
	assert(second.failed_rect_valid && second.failed_generation == 12);
	assert(second.failed_width == 320 && second.failed_height == 480);
	assert(second.failed_rect.x2 == 320 && second.failed_rect.y2 == 480);
	assert(first.failed_generation == 7 && first.failed_width == 640);
	assert(scheduled == 1 && pacing_updates == 2 && wakeups == 2);

	puts("PASS USB completion isolation: failures and replay state stay per-adapter");
	return 0;
}
