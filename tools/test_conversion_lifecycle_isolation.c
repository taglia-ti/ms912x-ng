// SPDX-License-Identifier: GPL-2.0-only

#include <assert.h>
#include <stdio.h>

#include "test_rect_compat.h"

typedef uint32_t u32;
typedef uint64_t u64;

#define MS912X_STAT_INC(device, counter) ((void)(device))
#define WARN_ON_ONCE(condition) (condition)
#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))
#define container_of(pointer, type, member) \
	((type *)((char *)(pointer) - offsetof(type, member)))

enum ms912x_request_state {
	MS912X_REQUEST_FREE,
	MS912X_REQUEST_USB_QUEUED,
};

struct ms912x_device;
struct ms912x_usb_request;
struct work_struct {
	struct ms912x_usb_request *owner;
};

struct ms912x_device {
	void *workqueue;
	struct drm_rect *previous_damage_rects;
	unsigned int previous_damage_rect_capacity;
	unsigned int previous_damage_rect_count;
	unsigned int previous_damage_width, previous_damage_height;
	unsigned int previous_damage_generation;
	unsigned int pending_width, pending_height, pending_rect_count;
	u32 frame_generation;
	u64 failed_present_sequence;
	bool transfer_stopping, conversion_busy, pending_force_full, pending_valid;
	struct drm_rect pending_rect;
	int conversion_wait;
	unsigned int queued, scheduled, pacing_updates, wakeups;
};

struct ms912x_usb_request {
	struct ms912x_device *ms912x;
	struct drm_rect *current_rects;
	unsigned int current_rect_capacity, current_rect_count;
	unsigned int width, height, generation;
	enum ms912x_request_state state;
	bool replay_included;
	struct work_struct work;
};

static void ms912x_pending_add_rect_locked(struct ms912x_device *device,
		const struct drm_rect *rect)
{
	device->pending_rect = *rect;
	device->pending_rect_count++;
}

static void ms912x_transfer_update_pacing_locked(struct ms912x_device *device)
{
	device->pacing_updates++;
}

static void ms912x_schedule_conversion_locked(struct ms912x_device *device)
{
	device->scheduled++;
}

static void wake_up_all(int *wait)
{
	struct ms912x_device *device =
		container_of(wait, struct ms912x_device, conversion_wait);
	device->wakeups++;
}

static bool queue_work(void *queue, struct work_struct *work)
{
	struct ms912x_device *device = work->owner->ms912x;

	assert(queue == device->workqueue);
	assert(work->owner->state == MS912X_REQUEST_USB_QUEUED);
	device->queued++;
	return true;
}

#include "conversion-lifecycle.h"

int main(void)
{
	struct drm_rect first_previous[4], second_previous[4];
	struct drm_rect first_current[2] = { DRM_RECT_INIT(4, 8, 12, 6) };
	struct drm_rect second_current[2] = { DRM_RECT_INIT(60, 20, 16, 8) };
	struct ms912x_device first = {
		.workqueue = &first, .previous_damage_rects = first_previous,
		.previous_damage_rect_capacity = ARRAY_SIZE(first_previous),
		.previous_damage_rect_count = 1, .frame_generation = 4,
		.transfer_stopping = true, .conversion_busy = true,
	};
	struct ms912x_device second = {
		.workqueue = &second, .previous_damage_rects = second_previous,
		.previous_damage_rect_capacity = ARRAY_SIZE(second_previous),
		.previous_damage_rect_count = 2, .frame_generation = 9,
		.conversion_busy = true,
	};
	struct ms912x_usb_request first_request = {
		.ms912x = &first, .current_rects = first_current,
		.current_rect_capacity = ARRAY_SIZE(first_current),
		.current_rect_count = 1, .width = 640, .height = 240,
		.generation = 4, .state = MS912X_REQUEST_FREE,
	};
	struct ms912x_usb_request second_request = {
		.ms912x = &second, .current_rects = second_current,
		.current_rect_capacity = ARRAY_SIZE(second_current),
		.current_rect_count = 1, .width = 320, .height = 480,
		.generation = 9, .state = MS912X_REQUEST_FREE,
	};

	first_request.work.owner = &first_request;
	second_request.work.owner = &second_request;

	ms912x_conversion_complete_locked(&first_request, 101, 0);
	assert(!first.conversion_busy && first_request.state == MS912X_REQUEST_FREE);
	assert(!first.queued && first.scheduled == 1 && first.pacing_updates == 1);
	assert(first.wakeups == 1 && first.previous_damage_rects == first_previous);
	assert(first.previous_damage_rect_count == 1);
	assert(second.conversion_busy && second_request.state == MS912X_REQUEST_FREE);
	assert(!second.queued && !second.scheduled && !second.pacing_updates);
	assert(second.previous_damage_rects == second_previous);
	assert(second.previous_damage_rect_count == 2 && !second.wakeups);

	ms912x_conversion_complete_locked(&second_request, 202, 0);
	assert(!second.conversion_busy &&
		second_request.state == MS912X_REQUEST_USB_QUEUED);
	assert(second.queued == 1 && second.scheduled == 1 &&
		second.pacing_updates == 1 && second.wakeups == 1);
	assert(second.previous_damage_rects == second_current);
	assert(second.previous_damage_rect_capacity == ARRAY_SIZE(second_current));
	assert(second.previous_damage_rect_count == 1);
	assert(second.previous_damage_width == 320 &&
		second.previous_damage_height == 480 &&
		second.previous_damage_generation == 9);
	assert(second_request.current_rects == second_previous &&
		!second_request.current_rect_count);
	assert(first.previous_damage_rects == first_previous &&
		first.previous_damage_rect_count == 1 && !first.queued);
	assert(first.scheduled == 1 && first.wakeups == 1);

	puts("PASS conversion lifecycle isolation: stopping adapter does not publish or steal replay state");
	return 0;
}
