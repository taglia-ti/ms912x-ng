// SPDX-License-Identifier: GPL-2.0-only
#include <stdio.h>
#include <pthread.h>

#define MS912X_TEST_SUBMIT_LOCK_HOOK
#include "test_entrypoint_compat.h"
#include "scan-invalidate.h"

struct ms912x_parameters_test ms912x_parameters;
unsigned int test_drm_exit_count;
unsigned int test_cpu_end_count;
int test_cpu_begin_ret;
unsigned int test_cpu_begin_count;
void (*test_cpu_begin_hook)(struct ms912x_device *);
bool test_kvrealloc_fail;
static int enqueue_ret;
static int pacing_ret;
static unsigned int publish_calls;
static unsigned int published_count;
static bool published_full_update;
static struct drm_rect published_rects[4];
static unsigned int enqueue_calls;
static unsigned int pacing_calls;
static unsigned int schedule_calls;
static unsigned int expected_generation;
static struct drm_rect replacement_rects[4];
static struct ms912x_device *active_device;
static unsigned int snapshot_lock_action;
enum { SOURCE_WIDTH = 640, SOURCE_HEIGHT = 480, SOURCE_STRIDE = 648 };
static u32 source_pixels[SOURCE_STRIDE * SOURCE_HEIGHT];
static u32 latest_pixels[MS912X_MAX_WIDTH * SOURCE_HEIGHT];
static u32 baseline_pixels[MS912X_MAX_WIDTH * SOURCE_HEIGHT];
static u32 staging_pixels[MS912X_MAX_WIDTH * SOURCE_HEIGHT];
static u32 raw_pixels[MS912X_MAX_WIDTH * SOURCE_HEIGHT];
static const struct drm_format_info xrgb8888_format = { .format = DRM_FORMAT_XRGB8888 };
static struct drm_rect pending_rects[16];
static unsigned int capture_calls, baseline_calls, downscale_calls;
static unsigned int capture_action, baseline_action;
static unsigned int scenarios;
static pthread_mutex_t native_locks[4] = {
	PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER,
	PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER,
};
static _Thread_local unsigned int owned_locks;
static pthread_t concurrent_worker;
static pthread_mutex_t probe_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t probe_cond = PTHREAD_COND_INITIALIZER;
static bool worker_waiting, reader_finished, stop_drained;

static unsigned int lock_index(int *lock)
{
	if (lock == &active_device->submit_lock) return 0;
	if (lock == &active_device->snapshot_lock) return 1;
	if (lock == &active_device->transfer_lock) return 2;
	assert(lock == &active_device->damage_scan_lock);
	return 3;
}

bool test_submit_lock_owned(int *lock)
{
	return (owned_locks & (1U << lock_index(lock))) != 0;
}

void test_mutex_lock(int *lock)
{
	unsigned int index = lock_index(lock);

	if (index == 1) {
		assert(!(owned_locks & ((1U << 2) | (1U << 3))));
		test_submit_lock_hook(lock);
	}
	assert(!pthread_mutex_lock(&native_locks[index]));
	assert(!*lock); *lock = 1;
	owned_locks |= 1U << index;
}

void test_mutex_unlock(int *lock)
{
	unsigned int index = lock_index(lock);

	assert(*lock && (owned_locks & (1U << index)));
	*lock = 0;
	owned_locks &= ~(1U << index);
	assert(!pthread_mutex_unlock(&native_locks[index]));
}

void test_submit_lock_hook(int *lock)
{
	if (lock != &active_device->snapshot_lock)
		return;
	if (!(owned_locks & 1U))
		return;
	assert(active_device->submit_lock && !active_device->transfer_lock &&
	       !active_device->damage_scan_lock);
	if (!snapshot_lock_action)
		return;
	mutex_lock(&active_device->transfer_lock);
	if (snapshot_lock_action == 1)
		active_device->transfer_stopping = true;
	else if (snapshot_lock_action == 2)
		active_device->frame_generation++;
	else
		ms912x_damage_scan_invalidate(active_device);
	mutex_unlock(&active_device->transfer_lock);
}

static int ms912x_wait_for_pacing_release(struct ms912x_device *ms912x,
						 u64 sequence, u32 generation)
{
	assert(!owned_locks);
	assert(test_cpu_end_count == 1);
	assert(sequence == 42);
	assert(generation == expected_generation);
	++pacing_calls;
	return pacing_ret;
}

static void ms912x_payload_invalidate_damage(struct ms912x_device *ms912x,
					     const struct drm_rect *rects,
					     unsigned int count)
{
	assert(ms912x->transfer_lock && ms912x->snapshot_lock);
	++publish_calls;
	assert(count <= ARRAY_SIZE(published_rects));
	published_count = count;
	published_full_update = count == 1 && rects[0].x1 == 0 && rects[0].y1 == 0 &&
		rects[0].x2 == ms912x->frame_snapshot_width &&
		rects[0].y2 == ms912x->frame_snapshot_height;
	memcpy(published_rects, rects,
	       published_count * sizeof(*published_rects));
}

static void ms912x_pending_add_rect_locked(struct ms912x_device *ms912x,
					  const struct drm_rect *rect)
{
	assert(ms912x->transfer_lock);
	assert(ms912x->pending_rect_count < ARRAY_SIZE(pending_rects));
	ms912x->pending_rects[ms912x->pending_rect_count++] = *rect;
	ms912x->pending_valid = true;
}
static void ms912x_pending_compact_locked(struct ms912x_device *ms912x)
{
	assert(ms912x->transfer_lock);
}
static void ms912x_pending_update_deadline_locked(struct ms912x_device *ms912x,
						 bool had_pending)
{
	assert(ms912x->transfer_lock);
}
static u64 ms912x_rects_payload_bytes(const struct drm_rect *rects, unsigned int count)
{
	u64 bytes = 0;
	for (unsigned int i = 0; i < count; i++)
		bytes += drm_rect_width(&rects[i]) * drm_rect_height(&rects[i]) * 2 + 16;
	return bytes;
}
static void wake_up_all(int *wait) { (void)wait; }

static void verify_pixels(void)
{
	for (unsigned int y = 0; y < SOURCE_HEIGHT; y++)
		for (unsigned int x = 0; x < MS912X_MAX_WIDTH; x += 2) {
			bool covered = false;
			unsigned int offset = y * (MS912X_MAX_WIDTH / 2) + x / 2;
			for (unsigned int i = 0; i < published_count; i++)
				covered |= x >= (unsigned int)published_rects[i].x1 &&
					x < (unsigned int)published_rects[i].x2 &&
					y >= (unsigned int)published_rects[i].y1 &&
					y < (unsigned int)published_rects[i].y2;
			u32 expected = covered ?
				(source_pixels[y * SOURCE_STRIDE + x] & 0xffffU) |
				((source_pixels[y * SOURCE_STRIDE + x + 1] & 0xffffU) << 16) : 0xa5a5a5a5;
			assert(latest_pixels[offset] == expected);
			if (active_device->damage_scan_snapshot)
				assert(baseline_pixels[offset] ==
				       (covered ? expected : 0x5a5a5a5a));
		}
}

static void *concurrent_capture_worker(void *unused)
{
	struct ms912x_device *ms912x = active_device;

	mutex_lock(&ms912x->transfer_lock);
	assert(ms912x->published_frame_sequence == 42 && ms912x->pending_valid);
	if (capture_action == 4) {
		ms912x->transfer_stopping = true;
		ms912x->pending_valid = false;
	}
	if (capture_action == 5) {
		struct drm_rect cursor = DRM_RECT_INIT(600, 400, 8, 8);
		ms912x_pending_add_rect_locked(ms912x, &cursor);
		ms912x->pending_deadline = 99;
		ms912x->cursor_commit_pending = true;
	}
	mutex_unlock(&ms912x->transfer_lock);
	assert(pthread_mutex_trylock(&native_locks[1]) == EBUSY);
	assert(!pthread_mutex_lock(&probe_lock));
	worker_waiting = true;
	assert(!pthread_cond_signal(&probe_cond));
	assert(!pthread_mutex_unlock(&probe_lock));
	if (capture_action == 4) {
		mutex_lock(&ms912x->submit_lock);
		assert(test_cpu_end_count == 1);
		stop_drained = true;
		mutex_unlock(&ms912x->submit_lock);
	} else {
		mutex_lock(&ms912x->snapshot_lock);
		verify_pixels();
		reader_finished = true;
		mutex_unlock(&ms912x->snapshot_lock);
	}
	return NULL;
}

static void invalidate_during_copy(void)
{
	ms912x_damage_scan_invalidate(active_device);
}

static void iosys_map_memcpy_from(void *dest, const struct iosys_map *map,
				 size_t offset, size_t bytes)
{
	assert(active_device->submit_lock && active_device->snapshot_lock &&
	       !active_device->transfer_lock && !active_device->damage_scan_lock);
	assert(publish_calls == 1 && active_device->published_frame_sequence == 42);
	if (!capture_calls++) {
		if (capture_action == 1) invalidate_during_copy();
		if (capture_action == 2) active_device->scale_mode = MS912X_SCALE_MODE_NATIVE;
		if (capture_action >= 3) {
			assert(!pthread_create(&concurrent_worker, NULL, concurrent_capture_worker, NULL));
			assert(!pthread_mutex_lock(&probe_lock));
			while (!worker_waiting)
				assert(!pthread_cond_wait(&probe_cond, &probe_lock));
			assert(!pthread_mutex_unlock(&probe_lock));
		}
	}
	memcpy(dest, (const u8 *)map->vaddr + offset, bytes);
}

static void baseline_memcpy(void *dest, const void *src, size_t bytes)
{
	assert(active_device->submit_lock && active_device->snapshot_lock &&
	       !active_device->transfer_lock && !active_device->damage_scan_lock);
	if (!baseline_calls++ && baseline_action) invalidate_during_copy();
	memcpy(dest, src, bytes);
}
#define memcpy baseline_memcpy
#include "ms912x_pixels.h"
#include "baseline-copy.h"
#undef memcpy

static void ms912x_downscale_xrgb8888_region(enum ms912x_scale_mode mode,
	const u32 *source, u32 *dest, unsigned int source_width, unsigned int source_height,
	unsigned int width, unsigned int height, unsigned int source_stride,
	unsigned int dest_stride, unsigned int center_x, unsigned int center_y)
{
	/* Geometry/lifecycle mock; filter arithmetic has its own production fixtures. */
	assert(mode == MS912X_SCALE_MODE_DOWNSCALE_2_TO_1);
	assert(!active_device->transfer_lock && !active_device->damage_scan_lock);
	assert(width * 2 <= source_width && height * 2 <= source_height);
	downscale_calls++;
	for (unsigned int y = 0; y < height; y++)
		for (unsigned int x = 0; x < width; x++)
			dest[y * dest_stride + x] = source[2 * y * source_stride + 2 * x];
}

static int ms912x_scan_enqueue_frame_locked(struct ms912x_device *ms912x,
						    struct drm_framebuffer *fb,
						    const struct iosys_map *map,
						    bool force_full,
						    u64 sequence)
{
	assert(!ms912x->snapshot_lock);
	(void)ms912x;
	(void)fb;
	(void)map;
	(void)force_full;
	assert(sequence == 42);
	++enqueue_calls;
	return enqueue_ret;
}

static void ms912x_schedule_conversion_locked(struct ms912x_device *ms912x)
{
	assert(ms912x->transfer_lock);
	++schedule_calls;
}

static u32 fixture_pair(u32 a, u32 b)
{
 return (a & 0xffffU) | ((b & 0xffffU) << 16);
}
static int ms912x_convert_snapshot(struct ms912x_device *ms912x, u32 *dst,
                                   const u32 *src, const struct drm_rect *rect, u32 format)
{
 assert(format == DRM_FORMAT_XRGB8888 || format == DRM_FORMAT_XBGR8888);
 assert(ms912x->snapshot_lock && ms912x->submit_lock);
 assert(!ms912x->transfer_lock && !ms912x->damage_scan_lock);
 for (int y = rect->y1; y < rect->y2; y++)
  for (int x = rect->x1; x < rect->x2; x += 2)
   dst[y * (MS912X_MAX_WIDTH / 2) + x / 2] = fixture_pair(
    src[y * MS912X_MAX_WIDTH + x], src[y * MS912X_MAX_WIDTH + x + 1]);
 return 0;
}

#include "submit-entry.h"

static void reset_fixture(struct ms912x_device *ms912x,
			  struct drm_framebuffer *fb,
			  struct iosys_map *map,
			  struct drm_rect *rects)
{
	memset(ms912x, 0, sizeof(*ms912x));
	memset(fb, 0, sizeof(*fb));
	memset(map, 0, sizeof(*map));
	memset(rects, 0, 2 * sizeof(*rects));
	active_device = ms912x;
	snapshot_lock_action = 0;
	ms912x->drm.alive = true;
	ms912x->latest_pixels = latest_pixels;
	ms912x->scan_frames[0].pixels = raw_pixels;
	ms912x->damage_scan_snapshot_back = staging_pixels;
	ms912x->scale_scratch = staging_pixels;
	fb->format = &xrgb8888_format;
	ms912x->primary_plane_format = DRM_FORMAT_XRGB8888;
	ms912x->pending_rects = pending_rects;
	memset(latest_pixels, 0xa5, sizeof(latest_pixels));
	memset(baseline_pixels, 0x5a, sizeof(baseline_pixels));
	for (unsigned int i = 0; i < ARRAY_SIZE(source_pixels); i++)
		source_pixels[i] = 0x12340000U + i;
	map->vaddr = source_pixels;
	ms912x->submit_damage_rects = rects;
	ms912x->submit_damage_rect_capacity = ARRAY_SIZE(published_rects);
	ms912x->frame_sequence = 41;
	ms912x->frame_generation = 7;
	ms912x->framebuffer_width = 640;
	ms912x->framebuffer_height = 480;
	ms912x->video_in_width = 640;
	ms912x->video_in_height = 480;
	ms912x->frame_snapshot_width = 640;
	ms912x->frame_snapshot_height = 480;
	ms912x->scale_mode = MS912X_SCALE_MODE_NATIVE;
	fb->dev = &ms912x->drm;
	fb->width = 640;
	fb->height = 480;
	fb->pitches[0] = SOURCE_STRIDE * sizeof(u32);
	test_cpu_begin_ret = 0;
	test_cpu_begin_count = 0;
	test_cpu_begin_hook = NULL;
	test_kvrealloc_fail = false;
	enqueue_ret = 0;
	pacing_ret = 0;
	publish_calls = 0;
	published_count = 0;
	published_full_update = false;
	enqueue_calls = 0;
	pacing_calls = 0;
	schedule_calls = 0;
	test_drm_exit_count = 0;
	test_cpu_end_count = 0;
	expected_generation = 7;
	memset(replacement_rects, 0, sizeof(replacement_rects));
	capture_calls = baseline_calls = downscale_calls = 0;
	capture_action = baseline_action = 0;
	worker_waiting = reader_finished = stop_drained = false;
	assert(!owned_locks);
	scenarios++;
}

static void stop_during_cpu_access(struct ms912x_device *ms912x)
{
	mutex_lock(&ms912x->transfer_lock);
	ms912x->transfer_stopping = true;
	mutex_unlock(&ms912x->transfer_lock);
}

static void change_geometry_during_cpu_access(struct ms912x_device *ms912x)
{
	mutex_lock(&ms912x->transfer_lock);
	ms912x->video_in_width = 320;
	ms912x->video_in_height = 240;
	ms912x->submit_damage_rects = replacement_rects;
	mutex_unlock(&ms912x->transfer_lock);
}

static void direct_capture_cases(void)
{
	struct ms912x_device ms912x;
	struct drm_framebuffer fb;
	struct iosys_map map;
	struct drm_rect rects[2];
	struct drm_rect damage = DRM_RECT_INIT(5, 3, 44, 12);
	struct drm_mode_rect clips[] = {{5, 3, 11, 7}, {37, 11, 49, 15}};
	int ret;

	reset_fixture(&ms912x, &fb, &map, rects);
	ms912x.damage_scan_snapshot = baseline_pixels;
	ret = ms912x_framebuffer_submit(&fb, &map, &damage, clips, ARRAY_SIZE(clips),
					false, false, false, false);
	assert(!ret && published_count == 2 && !published_full_update);
	assert(capture_calls == 8 && baseline_calls == 8 && !downscale_calls);
	assert(ms912x.damage_scan_valid && !ms912x.damage_scan_invalid);
	verify_pixels();

	for (unsigned int waiting_invalidation = 0; waiting_invalidation < 2; waiting_invalidation++) {
		reset_fixture(&ms912x, &fb, &map, rects);
		ms912x.damage_scan_snapshot = baseline_pixels;
		if (waiting_invalidation) snapshot_lock_action = 3;
		else atomic_set(&ms912x.damage_scan_invalid, 1);
		ret = ms912x_framebuffer_submit(&fb, &map, &damage, NULL, 0,
						false, false, false, false);
		assert(!ret && published_full_update && capture_calls == SOURCE_HEIGHT);
		assert(baseline_calls == SOURCE_HEIGHT && !ms912x.damage_scan_invalid);
		assert(ms912x.damage_scan_width == SOURCE_WIDTH &&
		       ms912x.damage_scan_height == SOURCE_HEIGHT);
		verify_pixels();
	}

	for (unsigned int during_baseline = 0; during_baseline < 2; during_baseline++) {
		reset_fixture(&ms912x, &fb, &map, rects);
		ms912x.damage_scan_snapshot = baseline_pixels;
		if (during_baseline) baseline_action = 1;
		else capture_action = 1;
		ret = ms912x_framebuffer_submit(&fb, &map, &damage, NULL, 0,
						false, false, false, false);
		assert(!ret && capture_calls && baseline_calls);
		assert(ms912x.damage_scan_invalid && !ms912x.damage_scan_valid);
		verify_pixels();
	}

	reset_fixture(&ms912x, &fb, &map, rects);
	ret = ms912x_framebuffer_submit(&fb, &map, &damage, NULL, 0,
					false, false, false, false);
	assert(!ret && capture_calls && !baseline_calls);
	verify_pixels();

	reset_fixture(&ms912x, &fb, &map, rects);
	ms912x.damage_scan_snapshot = baseline_pixels;
	ms912x.scan_pending = true;
	ret = ms912x_framebuffer_submit(&fb, &map, &damage, NULL, 0,
					false, false, false, false);
	assert(!ret && enqueue_calls == 1 && !capture_calls && !baseline_calls && !publish_calls);
	verify_pixels();

	reset_fixture(&ms912x, &fb, &map, rects);
	ms912x.damage_scan_snapshot = baseline_pixels;
	ret = ms912x_framebuffer_submit(&fb, &map, &damage, NULL, 0,
					false, false, false, true);
	assert(!ret && capture_calls && !pacing_calls);
	assert(ms912x.cursor_commit_sequence == 42);
	verify_pixels();

	for (unsigned int concurrent = 3; concurrent <= 5; concurrent++)
		for (unsigned int repeat = 0; repeat < 20; repeat++) {
			reset_fixture(&ms912x, &fb, &map, rects);
			ms912x.damage_scan_snapshot = baseline_pixels;
			capture_action = concurrent;
			ret = ms912x_framebuffer_submit(&fb, &map, &damage, NULL, 0,
							false, false, false, false);
			assert(!ret);
			assert(!pthread_join(concurrent_worker, NULL));
			assert(!owned_locks && capture_calls && baseline_calls);
			if (concurrent == 4) assert(stop_drained && !ms912x.pending_valid);
			else assert(reader_finished);
			if (concurrent == 5) {
				assert(ms912x.pending_rect_count == 2 && ms912x.pending_deadline == 99);
				assert(ms912x.cursor_commit_pending);
			}
			verify_pixels();
		}

	/* Use captured geometry even if the live scale changes during a source copy. */
	reset_fixture(&ms912x, &fb, &map, rects);
	ms912x.damage_scan_snapshot = baseline_pixels;
	ms912x.scale_mode = MS912X_SCALE_MODE_DOWNSCALE_2_TO_1;
	ms912x.video_in_width = ms912x.frame_snapshot_width = SOURCE_WIDTH / 2;
	ms912x.video_in_height = ms912x.frame_snapshot_height = SOURCE_HEIGHT / 2;
	capture_action = 2;
	ret = ms912x_framebuffer_submit(&fb, &map, &damage, NULL, 0,
					false, true, false, false);
	assert(!ret && downscale_calls == 1 && published_full_update);
	for (unsigned int y = 0; y < SOURCE_HEIGHT; y++)
		for (unsigned int x = 0; x < MS912X_MAX_WIDTH; x += 2) {
			bool inside = y < SOURCE_HEIGHT / 2 && x < SOURCE_WIDTH / 2;
			u32 pixel = inside ? fixture_pair(source_pixels[2 * y * SOURCE_STRIDE + 2 * x],
				 source_pixels[2 * y * SOURCE_STRIDE + 2 * x + 2]) : 0;
			assert(latest_pixels[y * (MS912X_MAX_WIDTH / 2) + x / 2] ==
			       (inside ? pixel : 0xa5a5a5a5));
			assert(baseline_pixels[y * (MS912X_MAX_WIDTH / 2) + x / 2] ==
			       (inside ? pixel : 0x5a5a5a5a));
		}
}

/* Android RGBX memory order maps to DRM XBGR8888 on little-endian hosts. */
static void rgbx_capture_case(void)
{
	struct ms912x_device ms912x;
	struct drm_framebuffer fb;
	struct iosys_map map;
	struct drm_rect rects[2];
	const struct drm_format_info rgbx = {.format = DRM_FORMAT_XBGR8888};
	u32 destination[4 * 16];

	reset_fixture(&ms912x, &fb, &map, rects);
	fb.format = &rgbx;
	memset(destination, 0xa5, sizeof(destination));
	publish_calls = 1;
	ms912x.published_frame_sequence = 42;
	mutex_lock(&ms912x.submit_lock);
	mutex_lock(&ms912x.snapshot_lock);
	ms912x_capture_pixel_rows(destination, 16, 3, 1, &fb, &map, 5, 7, 6, 2);
	mutex_unlock(&ms912x.snapshot_lock);
	mutex_unlock(&ms912x.submit_lock);
	for (unsigned int y = 0; y < 4; y++)
		for (unsigned int x = 0; x < 16; x++) {
			u32 expected = 0xa5a5a5a5U;

			if (y >= 1 && y < 3 && x >= 3 && x < 9) {
				u32 source = source_pixels[(y + 6) * SOURCE_STRIDE + x + 2];

				expected = source;
			}
			assert(destination[y * 16 + x] == expected);
		}
}

static void format_epoch_cases(void)
{
	const struct drm_format_info formats[] = {
		{ .format = DRM_FORMAT_XRGB8888 },
		{ .format = DRM_FORMAT_XBGR8888 },
	};

	/* Format is display state; only a change invalidates queued RGB snapshots. */
	for (unsigned int old = 0; old <= ARRAY_SIZE(formats); old++)
		for (unsigned int next = 0; next < ARRAY_SIZE(formats); next++) {
			struct ms912x_device ms912x;
			struct drm_framebuffer fb;
			struct iosys_map map;
			struct drm_rect rects[2];
			struct ms912x_framebuffer_submit submit = { .ms912x = &ms912x, .fb = &fb };
			bool change = old < ARRAY_SIZE(formats) && old != next;

			reset_fixture(&ms912x, &fb, &map, rects);
			ms912x.damage_scan_snapshot = baseline_pixels;
			ms912x.primary_plane_format = old < ARRAY_SIZE(formats) ? formats[old].format : 0;
			fb.format = &formats[next];
			ms912x.failed_rect_valid = true;
			ms912x.previous_damage_rect_count = 1;
			ms912x.pending_rect_count = 1;
			ms912x.pending_valid = ms912x.pending_force_full = true;
			ms912x.pending_deadline = 123;
			mutex_lock(&ms912x.submit_lock);
			mutex_lock(&ms912x.transfer_lock);
			ms912x_submit_begin_frame_locked(&submit);
			mutex_unlock(&ms912x.transfer_lock);
			mutex_unlock(&ms912x.submit_lock);
			assert(ms912x.primary_plane_format == formats[next].format);
			assert(submit.new_epoch == change && submit.force_full_scan == change);
			assert(submit.wait_generation == 7U + change);
			assert(ms912x.frame_generation == 7U + change);
			assert(ms912x.damage_scan_invalid == change);
			assert(ms912x.failed_rect_valid == !change);
			assert(ms912x.previous_damage_rect_count == (unsigned int)!change);
			assert(ms912x.pending_rect_count == (unsigned int)!change);
			assert(ms912x.pending_valid == !change && ms912x.pending_force_full == !change);
			assert(ms912x.pending_deadline == (change ? 0 : 123));
		}
}

int main(void)
{
	struct ms912x_device ms912x;
	struct drm_framebuffer fb;
	struct iosys_map map;
	struct drm_rect rects[2];
	struct drm_rect damage = DRM_RECT_INIT(0, 0, 32, 32);
	int ret;

	reset_fixture(&ms912x, &fb, &map, rects);
	ms912x.drm.alive = false;
	ret = ms912x_framebuffer_submit(&fb, &map, &damage, NULL, 0,
					false, false, false, false);
	assert(ret == -ENODEV && !test_drm_exit_count &&
	       !test_cpu_begin_count);

	reset_fixture(&ms912x, &fb, &map, rects);
	ms912x.transfer_stopping = true;
	ret = ms912x_framebuffer_submit(&fb, &map, &damage, NULL, 0,
					false, false, false, false);
	assert(ret == -ENODEV && test_drm_exit_count == 1 &&
	       !test_cpu_begin_count);
	assert(!ms912x.transfer_lock);

	reset_fixture(&ms912x, &fb, &map, rects);
	ms912x.damage_scan_snapshot = baseline_pixels;
	test_cpu_begin_ret = -EIO;
	ret = ms912x_framebuffer_submit(&fb, &map, &damage, NULL, 0,
					false, false, false, false);
	assert(ret == -EIO && test_cpu_begin_count == 1 &&
	       test_cpu_end_count == 0);
	assert(!ms912x.damage_scan_valid && ms912x.damage_scan_invalid == 1);
	assert(!ms912x.transfer_lock && !ms912x.damage_scan_lock);

	reset_fixture(&ms912x, &fb, &map, rects);
	ret = ms912x_framebuffer_submit(&fb, &map, &damage, NULL, 0,
					true, false, false, false);
	assert(ret == 0 && enqueue_calls == 1 && !publish_calls);
	assert(test_cpu_end_count == 1 && pacing_calls == 1 && test_drm_exit_count == 1);
	assert(!ms912x.transfer_lock && !ms912x.damage_scan_lock);

	reset_fixture(&ms912x, &fb, &map, rects);
	ms912x.damage_scan_snapshot = baseline_pixels;
	ms912x.cursor_commit_pending = true;
	enqueue_ret = -ENOMEM;
	ret = ms912x_framebuffer_submit(&fb, &map, &damage, NULL, 0,
					true, false, false, true);
	assert(ret == -ENOMEM && enqueue_calls == 1 && !pacing_calls);
	assert(!ms912x.cursor_commit_pending && !ms912x.cursor_commit_sequence);
	assert(ms912x.damage_scan_invalid == 1 && !ms912x.damage_scan_valid);
	assert(schedule_calls == 1 && test_cpu_end_count == 1);

	reset_fixture(&ms912x, &fb, &map, rects);
	ms912x.damage_scan_snapshot = baseline_pixels;
	ret = ms912x_framebuffer_submit(&fb, &map, NULL, NULL, 0,
					false, false, false, false);
	assert(ret == -ENODATA && !publish_calls);
	assert(test_cpu_end_count == 1 && !pacing_calls);
	assert(!ms912x.transfer_lock && !ms912x.damage_scan_lock);

	reset_fixture(&ms912x, &fb, &map, rects);
	ms912x.damage_scan_snapshot = baseline_pixels;
	test_kvrealloc_fail = true;
	ms912x.submit_damage_rect_capacity = 0;
	{
		struct drm_mode_rect clips[] = {
			{ .x1 = 0, .y1 = 0, .x2 = 12, .y2 = 12 },
			{ .x1 = 20, .y1 = 20, .x2 = 32, .y2 = 32 },
		};
		ret = ms912x_framebuffer_submit(&fb, &map, &damage, clips,
						ARRAY_SIZE(clips),
						false, false, false, false);
	}
	assert(ret == 0 && publish_calls == 1 && published_count == 1);
	assert(published_rects[0].x1 == 0 && published_rects[0].y1 == 0 &&
	       published_rects[0].x2 == 32 && published_rects[0].y2 == 32);
	assert(test_cpu_end_count == 1);
	assert(!ms912x.transfer_lock && !ms912x.damage_scan_lock);

	reset_fixture(&ms912x, &fb, &map, rects);
	ms912x.damage_scan_snapshot = baseline_pixels;
	pacing_ret = -ETIMEDOUT;
	ret = ms912x_framebuffer_submit(&fb, &map, &damage, NULL, 0,
					false, false, false, false);
	assert(ret == -ETIMEDOUT && publish_calls == 1 && published_count == 1);
	assert(test_cpu_end_count == 1 && pacing_calls == 1 && test_drm_exit_count == 1);
	assert(!ms912x.transfer_lock && !ms912x.damage_scan_lock);

	reset_fixture(&ms912x, &fb, &map, rects);
	test_cpu_begin_hook = stop_during_cpu_access;
	ret = ms912x_framebuffer_submit(&fb, &map, &damage, NULL, 0,
					false, false, false, false);
	assert(ret == -ENODEV && test_cpu_begin_count == 1 &&
	       test_cpu_end_count == 1);
	assert(!publish_calls && !enqueue_calls && !pacing_calls);
	assert(ms912x.frame_sequence == 41);
	assert(!ms912x.submit_lock && !ms912x.transfer_lock &&
	       !ms912x.damage_scan_lock);

	reset_fixture(&ms912x, &fb, &map, rects);
	test_cpu_begin_hook = change_geometry_during_cpu_access;
	expected_generation = 8;
	ret = ms912x_framebuffer_submit(&fb, &map, &damage, NULL, 0,
					false, false, false, false);
	assert(!ret && publish_calls == 1 && published_full_update);
	assert(test_cpu_begin_count == 1 && test_cpu_end_count == 1);
	assert(ms912x.frame_snapshot_width == 320 &&
	       ms912x.frame_snapshot_height == 240);
	assert(replacement_rects[0].x2 == 320 && replacement_rects[0].y2 == 240);
	assert(!ms912x.submit_lock && !ms912x.transfer_lock &&
	       !ms912x.damage_scan_lock);

	reset_fixture(&ms912x, &fb, &map, rects);
	ms912x.cursor_commit_pending = true;
	test_cpu_begin_ret = -EIO;
	ret = ms912x_framebuffer_submit(&fb, &map, &damage, NULL, 0,
					false, false, false, true);
	assert(ret == -EIO && test_cpu_begin_count == 1 && !test_cpu_end_count);
	assert(!ms912x.cursor_commit_pending && schedule_calls == 1);
	assert(!ms912x.submit_lock && !ms912x.transfer_lock &&
	       !ms912x.damage_scan_lock);

	for (unsigned int action = 1; action <= 2; action++) {
		reset_fixture(&ms912x, &fb, &map, rects);
		snapshot_lock_action = action;
		ret = ms912x_framebuffer_submit(&fb, &map, &damage, NULL, 0,
						false, false, false, false);
		assert(ret == -ECANCELED && !publish_calls && !enqueue_calls);
		assert(test_cpu_begin_count == 1 && test_cpu_end_count == 1);
		assert(!ms912x.submit_lock && !ms912x.snapshot_lock &&
		       !ms912x.transfer_lock && !ms912x.damage_scan_lock);
		assert(!capture_calls && !baseline_calls);
		verify_pixels();
	}

	rgbx_capture_case();
	format_epoch_cases();
	direct_capture_cases();
	printf("PASS framebuffer_submit entry: %u lifecycle/capture cases; exact pixels, "
	       "unlocked copies, invalidation, source ownership and stop drain\n", scenarios);
	return 0;
}
