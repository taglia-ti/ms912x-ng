// SPDX-License-Identifier: GPL-2.0-only
#include <stdio.h>

#include "test_entrypoint_compat.h"

struct ms912x_parameters_test ms912x_parameters;
unsigned int test_drm_exit_count;
unsigned int test_cpu_end_count;
static int begin_ret;
static int collect_ret;
static int enqueue_ret;
static int pacing_ret;
static unsigned int begin_calls;
static unsigned int collect_calls;
static unsigned int publish_calls;
static unsigned int enqueue_calls;
static unsigned int pacing_calls;
static unsigned int schedule_calls;

static int ms912x_wait_for_pacing_release(struct ms912x_device *ms912x,
						 u64 sequence, u32 generation)
{
	(void)ms912x;
	assert(sequence == 42);
	assert(generation == 7);
	++pacing_calls;
	return pacing_ret;
}

static int ms912x_submit_begin_frame_locked(
	struct ms912x_framebuffer_submit *submit)
{
	++begin_calls;
	if (begin_ret < 0)
		return begin_ret;
	submit->cpu_access = true;
	submit->sequence = 42;
	submit->wait_generation = 7;
	return 0;
}

static int ms912x_submit_collect_damage_locked(
	struct ms912x_framebuffer_submit *submit)
{
	++collect_calls;
	submit->current_count = 1;
	return collect_ret;
}

static void ms912x_submit_publish_damage_locked(
	struct ms912x_framebuffer_submit *submit)
{
	++publish_calls;
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
	fb->dev = &ms912x->drm;
	fb->width = 640;
	fb->height = 480;
	begin_ret = 0;
	collect_ret = 0;
	enqueue_ret = 0;
	pacing_ret = 0;
	begin_calls = 0;
	collect_calls = 0;
	publish_calls = 0;
	enqueue_calls = 0;
	pacing_calls = 0;
	schedule_calls = 0;
	test_drm_exit_count = 0;
	test_cpu_end_count = 0;
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
	assert(ret == -ENODEV && !test_drm_exit_count && !begin_calls);

	reset_fixture(&ms912x, &fb, &map, rects);
	ms912x.transfer_stopping = true;
	ret = ms912x_framebuffer_submit(&fb, &map, &damage, NULL, 0,
					false, false, false, false);
	assert(ret == -ENODEV && test_drm_exit_count == 1 && !begin_calls);
	assert(!ms912x.transfer_lock);

	reset_fixture(&ms912x, &fb, &map, rects);
	ms912x.damage_scan_snapshot = true;
	begin_ret = -EIO;
	ret = ms912x_framebuffer_submit(&fb, &map, &damage, NULL, 0,
					false, false, false, false);
	assert(ret == -EIO && begin_calls == 1 && test_cpu_end_count == 0);
	assert(!ms912x.damage_scan_valid && ms912x.damage_scan_invalid == 1);
	assert(!ms912x.transfer_lock && !ms912x.damage_scan_lock);

	reset_fixture(&ms912x, &fb, &map, rects);
	ret = ms912x_framebuffer_submit(&fb, &map, &damage, NULL, 0,
					true, false, false, false);
	assert(ret == 0 && enqueue_calls == 1 && !collect_calls && !publish_calls);
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
	collect_ret = -ENODATA;
	ret = ms912x_framebuffer_submit(&fb, &map, &damage, NULL, 0,
					false, false, false, false);
	assert(ret == -ENODATA && collect_calls == 1 && !publish_calls);
	assert(test_cpu_end_count == 1 && !pacing_calls);
	assert(!ms912x.transfer_lock && !ms912x.damage_scan_lock);

	reset_fixture(&ms912x, &fb, &map, rects);
	ms912x.damage_scan_snapshot = true;
	collect_ret = -ENOMEM;
	ret = ms912x_framebuffer_submit(&fb, &map, &damage, NULL, 0,
					false, false, false, false);
	assert(ret == -ENOMEM && !ms912x.damage_scan_valid);
	assert(ms912x.damage_scan_invalid == 1 && test_cpu_end_count == 1);
	assert(!ms912x.transfer_lock && !ms912x.damage_scan_lock);

	reset_fixture(&ms912x, &fb, &map, rects);
	ms912x.damage_scan_snapshot = true;
	pacing_ret = -ETIMEDOUT;
	ret = ms912x_framebuffer_submit(&fb, &map, &damage, NULL, 0,
					false, false, false, false);
	assert(ret == -ETIMEDOUT && collect_calls == 1 && publish_calls == 1);
	assert(test_cpu_end_count == 1 && pacing_calls == 1 && test_drm_exit_count == 1);
	assert(!ms912x.transfer_lock && !ms912x.damage_scan_lock);

	puts("PASS framebuffer_submit entry: 8 lifecycle and error-path cases");
	return 0;
}
