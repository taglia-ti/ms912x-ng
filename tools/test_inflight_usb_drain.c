// SPDX-License-Identifier: GPL-2.0-only

#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
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

struct ms912x_scan_frame { bool queued, running; };

struct ms912x_device {
	pthread_mutex_t transfer_lock, scan_queue_lock;
	pthread_mutex_t submit_lock, cursor_prepare_lock;
	struct work_struct scan_work;
	struct delayed_work conversion_work;
	struct ms912x_usb_request requests[2];
	struct ms912x_scan_frame scan_frames[2];
	bool transfer_stopping, pending_valid, pending_force_full;
	bool present_backpressured, failed_rect_valid, cursor_commit_pending;
	bool scan_stopping, scan_work_scheduled, conversion_busy;
	unsigned int pending_deadline, pending_rect_count;
	unsigned int previous_damage_rect_count, scan_epoch, usb_successful_transfers;
	u64 present_queue_pressure_ns, present_queue_low_watermark_ns;
	u64 present_queue_high_watermark_ns, drm_released_sequence;
	u64 published_frame_sequence;
	int conversion_wait, frame_present_wait, scan_frame_wait;
	unsigned int scan_waits, scan_invalidations, stats_resets;
};

static pthread_mutex_t sync_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t sync_changed = PTHREAD_COND_INITIALIZER;
static struct work_struct *inflight_work;
static bool worker_entered, release_worker, stop_waiting_for_usb, stop_finished;

#define mutex_lock(lock) do { int err = pthread_mutex_lock(lock); assert(!err); } while (0)
#define mutex_unlock(lock) do { int err = pthread_mutex_unlock(lock); assert(!err); } while (0)

static void wake_up_all(int *wait) { (void)wait; }

static void cancel_work_sync(struct work_struct *work)
{
	work->canceled++;
	if (work != inflight_work)
		return;
	pthread_mutex_lock(&sync_lock);
	stop_waiting_for_usb = true;
	pthread_cond_broadcast(&sync_changed);
	while (!release_worker)
		pthread_cond_wait(&sync_changed, &sync_lock);
	pthread_mutex_unlock(&sync_lock);
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

static void *usb_worker(void *arg)
{
	(void)arg;
	pthread_mutex_lock(&sync_lock);
	worker_entered = true;
	pthread_cond_broadcast(&sync_changed);
	while (!release_worker)
		pthread_cond_wait(&sync_changed, &sync_lock);
	pthread_mutex_unlock(&sync_lock);
	return NULL;
}

static void *stop_worker(void *arg)
{
	ms912x_transfer_stop(arg);
	pthread_mutex_lock(&sync_lock);
	stop_finished = true;
	pthread_cond_broadcast(&sync_changed);
	pthread_mutex_unlock(&sync_lock);
	return NULL;
}

int main(void)
{
	struct ms912x_device first = {
		.transfer_lock = PTHREAD_MUTEX_INITIALIZER,
		.scan_queue_lock = PTHREAD_MUTEX_INITIALIZER,
		.submit_lock = PTHREAD_MUTEX_INITIALIZER,
		.cursor_prepare_lock = PTHREAD_MUTEX_INITIALIZER,
		.requests = { { .state = MS912X_REQUEST_USB_ACTIVE },
			      { .state = MS912X_REQUEST_USB_QUEUED } },
	};
	struct ms912x_device second = {
		.transfer_lock = PTHREAD_MUTEX_INITIALIZER,
		.scan_queue_lock = PTHREAD_MUTEX_INITIALIZER,
		.submit_lock = PTHREAD_MUTEX_INITIALIZER,
		.cursor_prepare_lock = PTHREAD_MUTEX_INITIALIZER,
		.pending_valid = true, .scan_epoch = 7,
		.requests = { { .state = MS912X_REQUEST_USB_ACTIVE },
			      { .state = MS912X_REQUEST_USB_QUEUED } },
	};
	pthread_t usb_thread, stop_thread;

	inflight_work = &first.requests[0].work;
	assert(!pthread_create(&usb_thread, NULL, usb_worker, NULL));
	pthread_mutex_lock(&sync_lock);
	while (!worker_entered)
		pthread_cond_wait(&sync_changed, &sync_lock);
	pthread_mutex_unlock(&sync_lock);

	assert(!pthread_create(&stop_thread, NULL, stop_worker, &first));
	pthread_mutex_lock(&sync_lock);
	while (!stop_waiting_for_usb)
		pthread_cond_wait(&sync_changed, &sync_lock);
	assert(!stop_finished);
	assert(second.pending_valid && second.scan_epoch == 7);
	assert(second.requests[0].state == MS912X_REQUEST_USB_ACTIVE &&
	       second.requests[1].state == MS912X_REQUEST_USB_QUEUED);
	release_worker = true;
	pthread_cond_broadcast(&sync_changed);
	pthread_mutex_unlock(&sync_lock);

	assert(!pthread_join(usb_thread, NULL));
	assert(!pthread_join(stop_thread, NULL));
	assert(stop_finished && first.transfer_stopping);
	assert(first.requests[0].state == MS912X_REQUEST_FREE &&
	       first.requests[1].state == MS912X_REQUEST_FREE);
	assert(second.pending_valid && second.scan_epoch == 7);
	assert(second.requests[0].state == MS912X_REQUEST_USB_ACTIVE &&
	       second.requests[1].state == MS912X_REQUEST_USB_QUEUED);

	puts("PASS in-flight USB drain: stop waits for its active worker and preserves other adapter");
	return 0;
}
