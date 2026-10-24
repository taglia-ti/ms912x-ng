// SPDX-License-Identifier: GPL-2.0-only
#include <stdio.h>
#include "test_scan_queue_compat.h"

struct ms912x_device *test_scan_device;
unsigned int test_scan_copy_count, test_scan_downscale_count;
unsigned int test_scan_queue_count;
int test_scan_capture_action;

#include "scan-queue.h"

static u32 pixels[2][MS912X_SCAN_FRAME_SLOTS][MS912X_MAX_WIDTH * 4];
static const u32 source_pixels[MS912X_MAX_WIDTH * 4] = {
	1, 2, 3, 4, 5, 6, 7, 8,
	9, 10, 11, 12, 13, 14, 15, 16,
	17, 18, 19, 20, 21, 22, 23, 24,
};
static const struct drm_format_info xrgb8888_format = { .format = DRM_FORMAT_XRGB8888 };

static void reset(struct ms912x_device *device, unsigned int instance)
{
	unsigned int i;

	assert(instance < ARRAY_SIZE(pixels));
	memset(device, 0, sizeof(*device));
	test_scan_device = device;
	device->frame_generation = 7;
	device->scan_epoch = 3;
	device->video_in_width = 4;
	device->video_in_height = 3;
	device->scale_mode = MS912X_SCALE_MODE_NATIVE;
	device->scan_workqueue = device;
	for (i = 0; i < MS912X_SCAN_FRAME_SLOTS; i++) {
		memset(pixels[instance][i], 0, sizeof(pixels[instance][i]));
		device->scan_frames[i].pixels = pixels[instance][i];
	}
	test_scan_copy_count = 0;
	test_scan_downscale_count = 0;
	test_scan_queue_count = 0;
	test_scan_capture_action = 0;
}

static int enqueue(struct ms912x_device *device, u64 sequence,
		   bool force_full)
{
	struct drm_framebuffer fb = {
		.width = 4,
		.height = 3,
		.pitches = { 4 * sizeof(u32) },
		.format = &xrgb8888_format,
	};
	struct iosys_map map = { .data = (const unsigned char *)source_pixels };
	int ret;

	test_scan_device = device;
	device->transfer_lock = 1;
	ret = ms912x_scan_enqueue_frame_locked(device, &fb, &map,
					       force_full, sequence);
	assert(device->transfer_lock);
	device->transfer_lock = 0;
	return ret;
}

static void capture_releases_locks_and_publishes(void)
{
	struct ms912x_device device;
	struct ms912x_scan_frame *frame;
	unsigned int y;

	reset(&device, 0);
	device.scale_mode = 1;
	assert(!enqueue(&device, 10, false));
	frame = &device.scan_frames[0];
	assert(frame->queued && !frame->filling && !frame->running);
	assert(frame->sequence == 10 && frame->generation == 7);
	assert(frame->width == 4 && frame->height == 3 && !frame->force_full);
	for (y = 0; y < 3; y++)
		assert(!memcmp(frame->pixels + y * MS912X_MAX_WIDTH,
			       source_pixels + y * 4, 4 * sizeof(u32)));
	assert(test_scan_copy_count == 3 && test_scan_downscale_count == 1);
	assert(test_scan_queue_count == 1 && device.scan_work_scheduled);
	assert(!ms912x_scan_capture_pending(&device));
}

static void queued_replacement_preserves_full_update(void)
{
	struct ms912x_device device;
	struct ms912x_scan_frame *queued;

	reset(&device, 0);
	device.scan_frames[0].running = true;
	device.scan_frames[0].sequence = 1;
	device.scan_frames[1].queued = true;
	device.scan_frames[1].sequence = 8;
	device.scan_frames[1].force_full = true;
	device.scan_work_scheduled = true;
	assert(!enqueue(&device, 9, false));
	queued = &device.scan_frames[1];
	assert(device.scan_frames[0].running && !device.scan_frames[0].queued);
	assert(queued->queued && !queued->filling && queued->sequence == 9);
	assert(queued->force_full && test_scan_queue_count == 0);
}

static void ordering_and_slot_exhaustion(void)
{
	struct ms912x_device device;

	reset(&device, 0);
	device.scan_frames[0].filling = true;
	device.scan_frames[0].sequence = 4;
	device.scan_frames[1].queued = true;
	device.scan_frames[1].sequence = 5;
	assert(ms912x_scan_has_older_filling(&device, 5));
	assert(!ms912x_scan_has_older_filling(&device, 4));
	device.scan_frames[1].filling = true;
	device.scan_frames[1].queued = false;
	assert(ms912x_scan_capture_pending(&device));
	assert(enqueue(&device, 6, false) == -EBUSY);
	assert(device.scan_frames[0].filling && device.scan_frames[1].filling);
	assert(!test_scan_copy_count && !test_scan_queue_count);
	device.scan_frames[0].filling = false;
	device.scan_frames[1].filling = false;
	assert(!ms912x_scan_capture_pending(&device));
}

static void stale_capture_is_discarded(void)
{
	struct ms912x_device device;

	reset(&device, 0);
	test_scan_capture_action = 1;
	assert(enqueue(&device, 10, false) == -ECANCELED);
	assert(device.frame_generation == 8);
	assert(!device.scan_frames[0].queued && !device.scan_frames[0].filling);
	assert(!test_scan_queue_count && !ms912x_scan_capture_pending(&device));

	reset(&device, 0);
	test_scan_capture_action = 2;
	assert(enqueue(&device, 10, false) == -ECANCELED);
	assert(device.scan_epoch == 4);
	assert(!device.scan_frames[0].queued && !device.scan_frames[0].filling);
	assert(!test_scan_queue_count && !ms912x_scan_capture_pending(&device));
}

static void adapters_keep_scan_queues_and_frames_separate(void)
{
	struct ms912x_device first, second;

	reset(&first, 0);
	reset(&second, 1);
	assert(first.scan_workqueue != second.scan_workqueue);
	assert(!enqueue(&first, 10, false));
	assert(!enqueue(&second, 20, false));
	assert(test_scan_queue_count == 2);
	assert(first.scan_frames[0].queued &&
	       first.scan_frames[0].sequence == 10 &&
	       first.scan_frames[0].generation == first.frame_generation);
	assert(second.scan_frames[0].queued &&
	       second.scan_frames[0].sequence == 20 &&
	       second.scan_frames[0].generation == second.frame_generation);
	assert(first.scan_frames[0].pixels != second.scan_frames[0].pixels);

	first.frame_generation++;
	assert(second.frame_generation == 7);
	assert(second.scan_frames[0].generation == 7);
}

static void canceling_one_adapter_capture_preserves_the_other(void)
{
	struct ms912x_device first, second;

	reset(&first, 0);
	reset(&second, 1);
	test_scan_capture_action = 2;
	assert(enqueue(&first, 10, false) == -ECANCELED);
	assert(first.scan_stopping && first.scan_epoch == 4);
	assert(!first.scan_frames[0].queued && !first.scan_frames[0].filling);

	test_scan_capture_action = 0;
	assert(!enqueue(&second, 20, false));
	assert(!second.scan_stopping && second.scan_epoch == 3);
	assert(second.scan_frames[0].queued && second.scan_frames[0].sequence == 20);
	assert(second.scan_frames[0].generation == 7 &&
		second.scan_frames[0].pixels == pixels[1][0]);
	assert(!first.scan_frames[0].queued && !first.scan_frames[0].filling);
}

int main(void)
{
	capture_releases_locks_and_publishes();
	queued_replacement_preserves_full_update();
	ordering_and_slot_exhaustion();
	stale_capture_is_discarded();
	adapters_keep_scan_queues_and_frames_separate();
	canceling_one_adapter_capture_preserves_the_other();
	puts("PASS scan queue: capture, slot ordering, stale epochs and adapter isolation");
	return 0;
}
