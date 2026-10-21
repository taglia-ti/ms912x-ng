// SPDX-License-Identifier: GPL-2.0-only
#include <stdio.h>

#include "test_entrypoint_compat.h"

struct ms912x_parameters_test ms912x_parameters;
unsigned int test_drm_exit_count;
unsigned int test_cpu_end_count;
bool test_kvrealloc_fail;

struct ms912x_scan_rect_index {
	unsigned int reset_height;
};

static unsigned int effective_tolerance;
static u64 changed_pixels;
static u64 bbox_pixels;
static unsigned int rows_scanned;
static unsigned int index_resets;
static unsigned int full_backend_calls;
static unsigned int incremental_select_calls;
static unsigned int row_runner_calls;
static unsigned int extend_calls;
static unsigned int publish_calls;

static void ms912x_scan_rect_index_reset(struct ms912x_scan_rect_index *index,
						unsigned int height)
{
	++index_resets;
	index->reset_height = height;
}

unsigned int ms912x_damage_scan_effective_tolerance(
	struct ms912x_device *ms912x)
{
	(void)ms912x;
	return effective_tolerance;
}

static void ms912x_damage_scan_select_full_backend_locked(
	struct ms912x_device *ms912x, const u32 *frame, unsigned int width,
	unsigned int height, struct ms912x_scan_config *config)
{
	(void)ms912x;
	(void)frame;
	(void)width;
	(void)height;
	config->method = MS912X_DAMAGE_SCAN_SCALAR;
	++full_backend_calls;
}

static void ms912x_damage_scan_select_incremental_backend_locked(
	struct ms912x_device *ms912x, struct ms912x_scan_config *config)
{
	(void)ms912x;
	(void)config;
	++incremental_select_calls;
}

static void ms912x_damage_scan_run_rows(
	struct ms912x_scan_work *work, const struct ms912x_scan_config *config,
	u64 sequence, unsigned int *scanned)
{
	(void)config;
	(void)sequence;
	++row_runner_calls;
	work->changed_pixels = changed_pixels;
	*scanned = rows_scanned;
}

static void ms912x_damage_scan_extend_bottom(struct ms912x_scan_work *work)
{
	(void)work;
	++extend_calls;
}

static u64 ms912x_damage_scan_publish_locked(
	struct ms912x_device *ms912x, unsigned int width, unsigned int height,
	struct drm_rect *rects, unsigned int count, bool stats_enabled)
{
	(void)rects;
	(void)count;
	(void)stats_enabled;
	++publish_calls;
	ms912x->damage_scan_width = width;
	ms912x->damage_scan_height = height;
	ms912x->damage_scan_valid = true;
	atomic_set(&ms912x->damage_scan_invalid, 0);
	return bbox_pixels;
}

#include "scan-entry.h"

static void reset_fixture(struct ms912x_device *ms912x,
			  struct ms912x_scan_rect_index *index,
			  struct drm_rect **rects, unsigned int *capacity,
			  unsigned int *count)
{
	memset(ms912x, 0, sizeof(*ms912x));
	memset(index, 0, sizeof(*index));
	ms912x->scan_rect_index = index;
	ms912x->damage_scan_stride_default = 1;
	ms912x_parameters.damage_scan_method = MS912X_DAMAGE_SCAN_SCALAR;
	ms912x_parameters.damage_scan_stride = 1;
	ms912x_parameters.damage_scan_stride_override = 0;
	*rects = NULL;
	*capacity = 0;
	*count = 77;
	effective_tolerance = 0;
	changed_pixels = 0;
	bbox_pixels = 13;
	rows_scanned = 0;
	test_kvrealloc_fail = false;
	index_resets = 0;
	full_backend_calls = 0;
	incremental_select_calls = 0;
	row_runner_calls = 0;
	extend_calls = 0;
	publish_calls = 0;
	test_drm_exit_count = 0;
	ms912x->damage_scan_valid = true;
	ms912x->damage_scan_width = 4;
	ms912x->damage_scan_height = 2;
	ms912x_parameters.damage_scan_config_generation = 0;
	ms912x_parameters.damage_scan_method = MS912X_DAMAGE_SCAN_SCALAR;
	ms912x_parameters.damage_scan_stride = 1;
	ms912x_parameters.damage_scan_stride_override = 0;
	ms912x_parameters.damage_scan_tolerance = 0;
	ms912x_parameters.damage_scan_tolerance_max = 0;
	test_stats_enabled = false;
}

int main(void)
{
	struct ms912x_device ms912x;
	struct ms912x_scan_rect_index index;
	struct drm_rect *rects = NULL;
	unsigned int capacity = 0, count = 0;
	u32 frame[MS912X_MAX_WIDTH * 8] = {};
	u32 baseline[MS912X_MAX_WIDTH * 8] = {};
	u32 next[MS912X_MAX_WIDTH * 8] = {};
	unsigned int x, y;
	int ret;

	reset_fixture(&ms912x, &index, &rects, &capacity, &count);
	ret = ms912x_damage_scan_find_snapshot_changes(
		&ms912x, frame, 4, 2, baseline, next, MS912X_MAX_WIDTH + 1,
		2, false, &rects, &capacity, &count);
	assert(ret == -EINVAL && count == 77 && !index_resets);
	assert(!ms912x.damage_scan_lock);

	reset_fixture(&ms912x, &index, &rects, &capacity, &count);
	for (y = 0; y < 2; y++)
		for (x = 0; x < 4; x++)
			frame[y * MS912X_MAX_WIDTH + x] = 0x100 + y * 4 + x;
	memset(next, 0xa5, sizeof(next));
	ret = ms912x_damage_scan_find_snapshot_changes(
		&ms912x, frame, 4, 2, baseline, next, 4, 2, true,
		&rects, &capacity, &count);
	assert(ret == 0 && count == 1 && rects);
	assert(rects[0].x1 == 0 && rects[0].y1 == 0 &&
	       rects[0].x2 == 4 && rects[0].y2 == 2);
	assert(index_resets == 1 && index.reset_height == 2);
	assert(full_backend_calls == 1 && !row_runner_calls && publish_calls == 1);
	assert(ms912x.damage_scan_valid && !ms912x.damage_scan_invalid);
	assert(ms912x.damage_scan_width == 4 && ms912x.damage_scan_height == 2);
	for (y = 0; y < 2; y++)
		for (x = 0; x < 4; x++)
			assert(next[y * MS912X_MAX_WIDTH + x] ==
			       frame[y * MS912X_MAX_WIDTH + x]);
	assert(next[4] == 0xa5a5a5a5U);
	assert(!ms912x.stats.software_scan_frames &&
	       !ms912x.stats.software_scan_frame_pixels &&
	       !ms912x.stats.software_scan_bbox_pixels);
	free(rects);

	reset_fixture(&ms912x, &index, &rects, &capacity, &count);
	test_kvrealloc_fail = true;
	ret = ms912x_damage_scan_find_snapshot_changes(
		&ms912x, frame, 4, 2, baseline, next, 4, 2, true,
		&rects, &capacity, &count);
	assert(ret == -ENOMEM && count == 0 && !publish_calls);
	assert(!ms912x.damage_scan_lock);
	free(rects);

	reset_fixture(&ms912x, &index, &rects, &capacity, &count);
	ms912x.damage_scan_width = 4;
	ms912x.damage_scan_height = 2;
	ms912x.damage_scan_stride_default = 1;
	ms912x_parameters.damage_scan_stride = 2;
	ms912x_parameters.damage_scan_stride_override = 1;
	memset(next, 0xa5, sizeof(next));
	for (y = 0; y < 2; y++)
		for (x = 0; x < 4; x++)
			baseline[y * MS912X_MAX_WIDTH + x] = 0x200 + y * 4 + x;
	ret = ms912x_damage_scan_find_snapshot_changes(
			&ms912x, frame, 4, 0, baseline, next, 4, 2, false,
		&rects, &capacity, &count);
	assert(ret == 0 && count == 0 && row_runner_calls == 1);
	assert(incremental_select_calls == 1);
	for (y = 0; y < 2; y++)
		for (x = 0; x < 4; x++)
			assert(next[y * MS912X_MAX_WIDTH + x] ==
			       baseline[y * MS912X_MAX_WIDTH + x]);
	assert(next[4] == 0xa5a5a5a5U);
	assert(!extend_calls && !publish_calls && ms912x.damage_scan_valid);
	assert(!ms912x.damage_scan_invalid && !ms912x.damage_scan_lock);

	reset_fixture(&ms912x, &index, &rects, &capacity, &count);
	ms912x.damage_scan_width = 4;
	ms912x.damage_scan_height = 2;
	changed_pixels = 1;
	rows_scanned = 2;
	ret = ms912x_damage_scan_find_snapshot_changes(
		&ms912x, frame, 5, 0, baseline, next, 4, 2, false,
		&rects, &capacity, &count);
	assert(ret == 1 && count == 0 && row_runner_calls == 1);
	assert(extend_calls == 1 && publish_calls == 1);
	assert(ms912x.damage_scan_valid && !ms912x.damage_scan_invalid);
	assert(!ms912x.damage_scan_lock);
	free(rects);

	reset_fixture(&ms912x, &index, &rects, &capacity, &count);
	ms912x.damage_scan_config_generation = 3;
	ms912x.damage_scan_probe_config_generation = 3;
	ms912x.damage_scan_x_probe_done = true;
	ms912x.damage_scan_x_stable = true;
	ms912x_parameters.damage_scan_config_generation = 4;
	test_stats_enabled = true;
	ret = ms912x_damage_scan_find_snapshot_changes(
		&ms912x, frame, 4, 2, baseline, next, 4, 2, false,
		&rects, &capacity, &count);
	assert(ret == 0 && count == 1 && full_backend_calls == 1);
	assert(ms912x.damage_scan_config_generation == 4 &&
	       ms912x.damage_scan_probe_config_generation == 4);
	assert(!ms912x.damage_scan_x_probe_done && !ms912x.damage_scan_x_stable);
	assert(ms912x.stats.software_scan_frames == 1 &&
	       ms912x.stats.software_scan_frame_pixels == 8 &&
	       ms912x.stats.software_scan_bbox_pixels == bbox_pixels &&
	       ms912x.stats.software_scan_rectangles == 1);
	assert(!ms912x.damage_scan_lock);
	free(rects);

	puts("PASS damage_scan entry: real config/baseline helpers, generations, stats, full/incremental paths and cleanup");
	return 0;
}
