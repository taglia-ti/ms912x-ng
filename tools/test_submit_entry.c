// SPDX-License-Identifier: GPL-2.0-only
#include <stdio.h>

#include "test_entrypoint_compat.h"

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

static int ms912x_wait_for_pacing_release(struct ms912x_device *ms912x,
						 u64 sequence, u32 generation)
{
	assert(!ms912x->submit_lock && !ms912x->transfer_lock &&
	       !ms912x->damage_scan_lock);
	assert(test_cpu_end_count == 1);
	assert(sequence == 42);
	assert(generation == expected_generation);
	++pacing_calls;
	return pacing_ret;
}

static void ms912x_submit_publish_damage_locked(
	struct ms912x_framebuffer_submit *submit)
{
	++publish_calls;
	assert(submit->current_count <= ARRAY_SIZE(published_rects));
	published_count = submit->current_count;
	published_full_update = submit->full_update;
	memcpy(published_rects, submit->current_rects,
	       published_count * sizeof(*published_rects));
	submit->wait_for_present = true;
}

static int ms912x_scan_enqueue_frame_locked(struct ms912x_device *ms912x,
						    struct drm_framebuffer *fb,
						    const struct iosys_map *map,
						    bool force_full,
						    u64 sequence)
{
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
	ms912x->drm.alive = true;
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
	ms912x.damage_scan_snapshot = true;
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
	ms912x.damage_scan_snapshot = true;
	ms912x.cursor_commit_pending = true;
	enqueue_ret = -ENOMEM;
	ret = ms912x_framebuffer_submit(&fb, &map, &damage, NULL, 0,
					true, false, false, true);
	assert(ret == -ENOMEM && enqueue_calls == 1 && !pacing_calls);
	assert(!ms912x.cursor_commit_pending && !ms912x.cursor_commit_sequence);
	assert(ms912x.damage_scan_invalid == 1 && !ms912x.damage_scan_valid);
	assert(schedule_calls == 1 && test_cpu_end_count == 1);

	reset_fixture(&ms912x, &fb, &map, rects);
	ms912x.damage_scan_snapshot = true;
	ret = ms912x_framebuffer_submit(&fb, &map, NULL, NULL, 0,
					false, false, false, false);
	assert(ret == -ENODATA && !publish_calls);
	assert(test_cpu_end_count == 1 && !pacing_calls);
	assert(!ms912x.transfer_lock && !ms912x.damage_scan_lock);

	reset_fixture(&ms912x, &fb, &map, rects);
	ms912x.damage_scan_snapshot = true;
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
	ms912x.damage_scan_snapshot = true;
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

	puts("PASS framebuffer_submit entry: 11 lifecycle, unlocked CPU-access and revalidation cases");
	return 0;
}
