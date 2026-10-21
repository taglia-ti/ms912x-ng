/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MS912X_TEST_SCAN_QUEUE_COMPAT_H
#define MS912X_TEST_SCAN_QUEUE_COMPAT_H

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef uint32_t u32;
typedef uint64_t u64;

#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))
#define MS912X_MAX_WIDTH 8
#define MS912X_SCAN_FRAME_SLOTS 2
enum ms912x_scale_mode { MS912X_SCALE_MODE_NATIVE, MS912X_SCALE_MODE_OTHER };

struct work_struct { int unused; };
struct drm_framebuffer {
	unsigned int width, height;
	unsigned int pitches[1];
};
struct iosys_map { const unsigned char *data; };
struct ms912x_scan_frame {
	u32 *pixels;
	u64 sequence;
	u32 generation;
	unsigned int width, height;
	bool force_full, queued, running, filling;
};
struct ms912x_device {
	int transfer_lock, scan_queue_lock;
	bool scan_stopping, transfer_stopping, scan_work_scheduled;
	u32 frame_generation, scan_epoch;
	unsigned int video_in_width, video_in_height;
	enum ms912x_scale_mode scale_mode;
	struct ms912x_scan_frame scan_frames[MS912X_SCAN_FRAME_SLOTS];
	int scan_frame_wait;
	struct work_struct scan_work;
};

extern struct ms912x_device *test_scan_device;
extern unsigned int test_scan_copy_count, test_scan_downscale_count;
extern unsigned int test_scan_queue_count;
extern int test_scan_capture_action;
extern void *system_dfl_wq;

static inline void mutex_lock(int *lock)
{
	assert(!*lock);
	*lock = 1;
}

static inline void mutex_unlock(int *lock)
{
	assert(*lock);
	*lock = 0;
}

static inline void wake_up_all(int *wait)
{
	(void)wait;
}

static inline void iosys_map_memcpy_from(void *destination,
					 const struct iosys_map *map,
					 size_t offset, size_t length)
{
	assert(!test_scan_device->transfer_lock);
	assert(!test_scan_device->scan_queue_lock);
	memcpy(destination, map->data + offset, length);
	if (!test_scan_copy_count++) {
		if (test_scan_capture_action == 1)
			test_scan_device->frame_generation++;
		else if (test_scan_capture_action == 2) {
			test_scan_device->scan_stopping = true;
			test_scan_device->scan_epoch++;
		}
	}
}

static inline void ms912x_downscale_xrgb8888(enum ms912x_scale_mode mode,
					     u32 *destination,
					     const u32 *source,
					     unsigned int source_width,
					     unsigned int source_height,
					     unsigned int width,
					     unsigned int height,
					     unsigned int source_stride,
					     unsigned int destination_stride)
{
	assert(!test_scan_device->transfer_lock);
	assert(!test_scan_device->scan_queue_lock);
	assert(mode != MS912X_SCALE_MODE_NATIVE);
	assert(destination == source);
	assert(source_width && source_height && width && height);
	assert(source_stride == MS912X_MAX_WIDTH);
	assert(destination_stride == MS912X_MAX_WIDTH);
	++test_scan_downscale_count;
}

static inline bool queue_work(void *queue, struct work_struct *work)
{
	(void)queue;
	assert(work == &test_scan_device->scan_work);
	assert(test_scan_device->transfer_lock);
	assert(!test_scan_device->scan_queue_lock);
	++test_scan_queue_count;
	return true;
}

#endif
