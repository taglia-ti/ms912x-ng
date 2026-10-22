// SPDX-License-Identifier: GPL-2.0-only

#include <assert.h>
#include <stdio.h>

#include "test_rect_compat.h"

#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))

struct work_struct { unsigned int canceled; };
struct delayed_work { unsigned int canceled; };

enum ms912x_request_state {
	MS912X_REQUEST_FREE,
	MS912X_REQUEST_USB_QUEUED,
	MS912X_REQUEST_USB_ACTIVE,
};

struct ms912x_usb_request {
	struct work_struct work;
	enum ms912x_request_state state;
};

struct ms912x_scan_frame {
	bool queued, running;
};

struct ms912x_device {
	int transfer_lock, scan_queue_lock, submit_lock, cursor_prepare_lock;
	struct work_struct scan_work;
	struct delayed_work conversion_work;
	struct ms912x_usb_request requests[2];
	struct ms912x_scan_frame scan_frames[2];
	bool transfer_stopping, pending_valid, pending_force_full;
	bool present_backpressured, failed_rect_valid, cursor_commit_pending;
	bool scan_stopping, scan_work_scheduled, conversion_busy;
	unsigned int pending_deadline, previous_damage_rect_count, scan_epoch;
	u64 present_queue_pressure_ns, present_queue_low_watermark_ns;
	u64 present_queue_high_watermark_ns, drm_released_sequence;
	u64 published_frame_sequence;
	int conversion_wait, frame_present_wait, scan_frame_wait;
	unsigned int scan_waits;
};

#define mutex_lock(lock) do { assert(!*(lock)); *(lock) = 1; } while (0)
#define mutex_unlock(lock) do { assert(*(lock)); *(lock) = 0; } while (0)

static void wake_up_all(int *wait)
{
	(void)wait;
}

static void cancel_work_sync(struct work_struct *work)
{
	work->canceled++;
}

static void cancel_delayed_work_sync(struct delayed_work *work)
{
	work->canceled++;
}

static void ms912x_scan_wait_captures(struct ms912x_device *device)
{
	device->scan_waits++;
}

#include "transfer-stop.h"

int main(void)
{
	struct ms912x_device first = {
		.pending_valid = true, .pending_force_full = true,
		.pending_deadline = 99, .present_backpressured = true,
		.present_queue_pressure_ns = 100, .present_queue_low_watermark_ns = 40,
		.present_queue_high_watermark_ns = 120, .published_frame_sequence = 55,
		.failed_rect_valid = true, .previous_damage_rect_count = 3,
		.cursor_commit_pending = true, .scan_work_scheduled = true,
		.scan_epoch = 8, .conversion_busy = true,
		.scan_frames = { { .queued = true, .running = true }, { .queued = true } },
		.requests = { { .state = MS912X_REQUEST_USB_ACTIVE },
			      { .state = MS912X_REQUEST_USB_QUEUED } },
	};
	struct ms912x_device second = {
		.pending_valid = true, .pending_force_full = true,
		.pending_deadline = 77, .present_backpressured = true,
		.present_queue_pressure_ns = 200, .published_frame_sequence = 91,
		.failed_rect_valid = true, .previous_damage_rect_count = 5,
		.cursor_commit_pending = true, .scan_work_scheduled = true,
		.scan_epoch = 13, .conversion_busy = true,
		.scan_frames = { { .queued = true, .running = true }, { .running = true } },
		.requests = { { .state = MS912X_REQUEST_USB_ACTIVE },
			      { .state = MS912X_REQUEST_USB_QUEUED } },
	};

	ms912x_transfer_stop(&first);

	assert(first.transfer_stopping && !first.pending_valid &&
		!first.pending_force_full && !first.pending_deadline);
	assert(!first.present_backpressured && !first.present_queue_pressure_ns);
	assert(!first.present_queue_low_watermark_ns &&
		!first.present_queue_high_watermark_ns);
	assert(first.drm_released_sequence == 55 && !first.failed_rect_valid);
	assert(!first.previous_damage_rect_count && !first.cursor_commit_pending);
	assert(first.scan_stopping && !first.scan_work_scheduled && first.scan_epoch == 9);
	assert(!first.conversion_busy && first.scan_waits == 1);
	assert(!first.scan_frames[0].queued && !first.scan_frames[0].running);
	assert(!first.scan_frames[1].queued && !first.scan_frames[1].running);
	assert(first.requests[0].state == MS912X_REQUEST_FREE &&
		first.requests[1].state == MS912X_REQUEST_FREE);

	assert(second.pending_valid && second.pending_force_full &&
		second.pending_deadline == 77);
	assert(second.present_backpressured && second.present_queue_pressure_ns == 200);
	assert(second.published_frame_sequence == 91 && !second.drm_released_sequence);
	assert(second.failed_rect_valid && second.previous_damage_rect_count == 5);
	assert(second.cursor_commit_pending && !second.transfer_stopping);
	assert(!second.scan_stopping && second.scan_work_scheduled && second.scan_epoch == 13);
	assert(second.conversion_busy && !second.scan_waits);
	assert(second.scan_frames[0].queued && second.scan_frames[0].running);
	assert(!second.scan_frames[1].queued && second.scan_frames[1].running);
	assert(second.requests[0].state == MS912X_REQUEST_USB_ACTIVE &&
		second.requests[1].state == MS912X_REQUEST_USB_QUEUED);

	puts("PASS transfer stop isolation: draining one adapter preserves the other's state");
	return 0;
}
