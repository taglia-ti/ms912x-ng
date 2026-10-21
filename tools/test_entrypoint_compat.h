/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MS912X_TEST_ENTRYPOINT_COMPAT_H
#define MS912X_TEST_ENTRYPOINT_COMPAT_H

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "test_rect_compat.h"

typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int atomic_t;
typedef long long atomic64_t;

#define READ_ONCE(value) (value)
#define WRITE_ONCE(value, next) ((value) = (next))
#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))
#define MS912X_MAX_WIDTH 1920
#define MS912X_MAX_HEIGHT 1200
#define MS912X_MAX_DAMAGE_RECTS 2048
#define MS912X_INITIAL_DAMAGE_RECTS 512
#define MS912X_DAMAGE_RECTS_GROWTH 256
#define MS912X_SMALL_DAMAGE_THRESHOLD_BYTES (256U * 1024U)
enum ms912x_scale_mode {
	MS912X_SCALE_MODE_NATIVE,
	MS912X_SCALE_MODE_DOWNSCALE_2_TO_1,
	MS912X_SCALE_MODE_DOWNSCALE_3_TO_2,
	MS912X_SCALE_MODE_DOWNSCALE_5_TO_4,
};
#define MS912X_DAMAGE_SCAN_SCALAR 0
#define MS912X_DAMAGE_SCAN_MEMCMP 1
#define MS912X_DAMAGE_SCAN_AVX 2
#define MS912X_DAMAGE_SCAN_SSE2 3
#define MS912X_DAMAGE_SCAN_NEON 4
#define MS912X_DAMAGE_SCAN_METHOD_AUTO 5
#define MS912X_STAT_INC(device, counter) ((void)(device))
#define DMA_FROM_DEVICE 0
#ifdef MS912X_TEST_SUBMIT_LOCK_HOOK
#define lockdep_assert_held(lock) assert(test_submit_lock_owned(lock))
#define lockdep_assert_not_held(lock) assert(!test_submit_lock_owned(lock))
#else
#define lockdep_assert_held(lock) assert(*(lock))
#define lockdep_assert_not_held(lock) assert(!*(lock))
#endif
#define GFP_KERNEL 0
#define DIV_ROUND_UP(value, divisor) (((value) + (divisor) - 1) / (divisor))
#define ALIGN(value, alignment) (((value) + (alignment) - 1) & ~((alignment) - 1))
#define ALIGN_DOWN(value, alignment) ((value) & ~((alignment) - 1))
#define clamp_t(type, value, low, high) \
	min_t(type, max_t(type, value, low), high)

extern unsigned int test_drm_exit_count;
extern unsigned int test_cpu_end_count;

struct drm_device {
	bool alive;
};

struct drm_framebuffer {
	struct drm_device *dev;
	unsigned int width;
	unsigned int height;
	unsigned int pitches[1];
};

struct drm_mode_rect {
	int x1, y1, x2, y2;
};

struct iosys_map {
	const void *vaddr;
};

struct ms912x_scan_rect_index;

struct ms912x_damage_scan_result {
	u64 invalidation_epoch;
	int config_generation;
};

struct work_struct { int unused; };
struct ms912x_scan_frame {
	u32 *pixels;
	u64 sequence;
	u32 generation;
	unsigned int width, height;
	bool force_full, queued, running, filling;
};

struct ms912x_scan_frame_stats {
	unsigned int rect_add_calls;
	unsigned int rect_overflows;
};

struct ms912x_scan_work {
	struct ms912x_scan_rect_index *index;
	const u32 *frame;
	const u32 *baseline;
	u32 *next_snapshot;
	struct drm_rect **rects;
	unsigned int *rect_capacity;
	unsigned int *rect_count;
	unsigned int width;
	unsigned int height;
	unsigned int stride;
	unsigned int block_pixels;
	unsigned int tolerance;
	bool selective_baseline;
	u64 changed_pixels;
	struct ms912x_scan_frame_stats *frame_stats;
};

struct ms912x_scan_config {
	unsigned int generation;
	unsigned int tolerance;
	unsigned int stride;
	unsigned int run_block_pixels;
	unsigned int run_tolerance;
	int method;
	bool full_update;
	bool use_memcmp;
	bool use_simd;
	bool use_relaxed;
};

struct ms912x_framebuffer_submit {
	struct ms912x_device *ms912x;
	struct drm_framebuffer *fb;
	const struct iosys_map *map;
	const struct drm_rect *damage;
	const struct drm_mode_rect *clips;
	unsigned int clip_count;
	struct drm_rect output_bounds;
	struct drm_rect input_bounds;
	struct drm_rect *current_rects;
	unsigned int input_width;
	unsigned int input_height;
	int scale_mode;
	unsigned int current_count;
	u64 sequence;
	u32 wait_generation;
	bool software_scan;
	bool force_full_scan;
	bool new_epoch;
	bool cursor_commit;
	bool snapshot_enabled;
	bool full_update;
	bool cpu_access;
	bool scan_lock_held;
	bool wait_for_present;
};

struct ms912x_stats {
	atomic64_t software_scan_frames;
	atomic64_t software_scan_frame_pixels;
	atomic64_t software_scan_compared_pixels;
	atomic64_t software_scan_bbox_pixels;
	atomic64_t scan_rect_overflow_merged_updates;
	atomic64_t software_scan_rect_add_calls;
	atomic64_t software_scan_rect_add_max_calls;
	atomic64_t software_scan_rectangles;
	atomic64_t software_scan_rectangles_max;
	atomic64_t software_scan_ns;
};

struct ms912x_device {
	struct drm_device drm;
	int transfer_lock;
	int snapshot_lock;
	int submit_lock;
	int damage_scan_lock;
	int scan_queue_lock;
	struct work_struct scan_work;
	struct ms912x_scan_frame scan_frames[2];
	bool scan_stopping, scan_work_scheduled;
	int scan_frame_wait;
	int requests[2];
	bool transfer_stopping;
	bool cursor_commit_pending;
	u32 *damage_scan_snapshot;
	u32 *damage_scan_snapshot_back;
	u32 *latest_xrgb;
	bool damage_scan_valid;
	bool damage_scan_x_probe_done;
	bool damage_scan_x_stable;
	bool pending_valid;
	bool pending_force_full;
	bool failed_rect_valid;
	bool wait_for_present;
	bool scan_pending;
	bool damage_scan_generation_invalid;
	atomic_t damage_scan_invalid;
	u64 damage_scan_invalidation_epoch;
	unsigned int frame_generation;
	unsigned int frame_sequence;
	unsigned int cursor_commit_sequence;
	unsigned int frame_snapshot_width;
	unsigned int frame_snapshot_height;
	unsigned int framebuffer_width;
	unsigned int framebuffer_height;
	unsigned int video_in_width;
	unsigned int video_in_height;
	unsigned int pending_width;
	unsigned int pending_height;
	unsigned int scale_mode;
	unsigned int damage_scan_width;
	unsigned int damage_scan_height;
	unsigned int damage_scan_config_generation;
	int damage_scan_mode_generation;
	unsigned int damage_scan_probe_config_generation;
	unsigned int damage_scan_probe_generation;
	unsigned int damage_scan_stride_default;
	unsigned int damage_scan_backend;
	unsigned int failed_generation;
	unsigned int failed_width;
	unsigned int failed_height;
	unsigned int pending_rect_count;
	unsigned int submit_damage_rect_capacity;
	unsigned int scan_damage_rect_capacity;
	unsigned int previous_damage_rect_count;
	struct drm_rect *submit_damage_rects;
	struct drm_rect *scan_damage_rects;
	struct drm_rect *pending_rects;
	struct drm_rect failed_rect;
	struct ms912x_scan_rect_index *scan_rect_index;
	struct ms912x_stats stats;
	u64 frame_sequence64;
	u64 pending_deadline;
	u64 published_frame_sequence;
	u64 small_damage_sequence;
	u64 failed_present_sequence;
	int frame_present_wait;
};

struct ms912x_parameters_test {
	unsigned int damage_scan_generation;
	int damage_scan;
	unsigned int damage_scan_config_generation;
	unsigned int damage_scan_method;
	unsigned int damage_scan_stride;
	unsigned int damage_scan_stride_override;
	unsigned int damage_scan_tolerance;
	unsigned int damage_scan_tolerance_max;
};

extern struct ms912x_parameters_test ms912x_parameters;
extern int test_cpu_begin_ret;
extern unsigned int test_cpu_begin_count;
extern void (*test_cpu_begin_hook)(struct ms912x_device *);
extern bool test_kvrealloc_fail;

static inline bool drm_dev_enter(struct drm_device *drm, int *idx)
{
	*idx = 1;
	return drm->alive;
}

static inline void drm_dev_exit(int idx)
{
	(void)idx;
	++test_drm_exit_count;
}

static inline struct ms912x_device *test_to_ms912x(struct drm_device *drm)
{
	return (struct ms912x_device *)drm;
}

#define to_ms912x(drm) test_to_ms912x(drm)

#ifdef MS912X_TEST_SUBMIT_LOCK_HOOK
void test_submit_lock_hook(int *lock);
bool test_submit_lock_owned(int *lock);
void test_mutex_lock(int *lock);
void test_mutex_unlock(int *lock);
#else
static inline void test_mutex_lock(int *lock)
{
	assert(!*lock);
	*lock = 1;
}

static inline void test_mutex_unlock(int *lock)
{
	assert(*lock);
	*lock = 0;
}
#endif

#define mutex_lock(lock) test_mutex_lock(lock)
#define mutex_unlock(lock) test_mutex_unlock(lock)
#define atomic_read(value) (*(value))
#define atomic_set(value, next) (*(value) = (next))

static inline void *kvrealloc(const void *old, size_t size, int flags)
{
	(void)flags;
#ifdef MS912X_TEST_ALLOC_HOOK
	void test_scan_alloc(void);
	test_scan_alloc();
#endif
	if (test_kvrealloc_fail)
		return NULL;
	return realloc((void *)old, size);
}

static inline bool drm_rect_intersect(struct drm_rect *rect,
				      const struct drm_rect *bounds)
{
	rect->x1 = max(rect->x1, bounds->x1);
	rect->y1 = max(rect->y1, bounds->y1);
	rect->x2 = min(rect->x2, bounds->x2);
	rect->y2 = min(rect->y2, bounds->y2);
	return rect->x1 < rect->x2 && rect->y1 < rect->y2;
}

#ifndef MS912X_TEST_REAL_SCAN_QUEUE
static inline bool ms912x_scan_is_pending(struct ms912x_device *ms912x)
{
	return ms912x->scan_pending;
}
#endif

static inline int drm_gem_fb_begin_cpu_access(struct drm_framebuffer *fb,
					      int direction)
{
	struct ms912x_device *ms912x = to_ms912x(fb->dev);

	(void)direction;
	assert(ms912x->submit_lock);
	lockdep_assert_not_held(&ms912x->transfer_lock);
	lockdep_assert_not_held(&ms912x->damage_scan_lock);
	lockdep_assert_not_held(&ms912x->snapshot_lock);
	++test_cpu_begin_count;
	if (test_cpu_begin_hook)
		test_cpu_begin_hook(ms912x);
	return test_cpu_begin_ret;
}

static inline void drm_gem_fb_end_cpu_access(struct drm_framebuffer *fb,
					     int direction)
{
	struct ms912x_device *ms912x = to_ms912x(fb->dev);

	(void)direction;
	assert(ms912x->submit_lock);
	lockdep_assert_not_held(&ms912x->transfer_lock);
	lockdep_assert_not_held(&ms912x->damage_scan_lock);
	lockdep_assert_not_held(&ms912x->snapshot_lock);
	++test_cpu_end_count;
}

static inline void atomic64_inc(atomic64_t *value) { ++*value; }
static inline void atomic64_add(u64 add, atomic64_t *value) { *value += add; }
static inline u64 ktime_get_ns(void) { return 1; }
static bool test_stats_enabled;
static inline bool ms912x_stats_enabled(void) { return test_stats_enabled; }
static inline void ms912x_stats_record_max(atomic64_t *value, u64 next)
{
	if (*value < (long long)next)
		*value = (long long)next;
}

#endif
