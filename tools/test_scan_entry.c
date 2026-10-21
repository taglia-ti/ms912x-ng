// SPDX-License-Identifier: GPL-2.0-only
#include <stdio.h>

#include "test_entrypoint_compat.h"

struct ms912x_parameters_test ms912x_parameters;
unsigned int test_drm_exit_count;
unsigned int test_cpu_end_count;

struct ms912x_scan_rect_index {
	unsigned int reset_height;
};

static bool force_resolved_full;
static u64 changed_pixels;
static u64 bbox_pixels;
static unsigned int rows_scanned;
static int full_update_ret;
static unsigned int index_resets;
static unsigned int full_update_calls;
static unsigned int incremental_select_calls;
static unsigned int baseline_calls;
static unsigned int row_runner_calls;
static unsigned int extend_calls;
static unsigned int publish_calls;

static void ms912x_scan_rect_index_reset(struct ms912x_scan_rect_index *index,
						unsigned int height)
{
	++index_resets;
	index->reset_height = height;
}

static void ms912x_damage_scan_resolve_config_locked(
	struct ms912x_device *ms912x, u32 generation, unsigned int width,
	unsigned int height, bool force_full, struct ms912x_scan_config *config)
{
	(void)ms912x;
	(void)generation;
	(void)width;
	(void)height;
	config->full_update = force_full || force_resolved_full;
}

static int ms912x_damage_scan_prepare_full_update_locked(
	struct ms912x_device *ms912x, const u32 *frame,
	const struct drm_rect *bounds, struct ms912x_scan_work *work,
	struct ms912x_scan_config *config)
{
	(void)ms912x;
	(void)frame;
	(void)config;
	++full_update_calls;
	if (full_update_ret < 0)
		return full_update_ret;
	*work->rect_capacity = max(*work->rect_capacity, 1U);
	*work->rects = realloc(*work->rects,
			       *work->rect_capacity * sizeof(**work->rects));
	assert(*work->rects);
	(*work->rects)[0] = *bounds;
	*work->rect_count = 1;
	work->changed_pixels = (u64)work->width * work->height;
	return 0;
}

static void ms912x_damage_scan_select_incremental_backend_locked(
	struct ms912x_device *ms912x, struct ms912x_scan_config *config)
{
	(void)ms912x;
	(void)config;
	++incremental_select_calls;
}

static void ms912x_damage_scan_prepare_baseline(
	struct ms912x_scan_work *work, const struct ms912x_scan_config *config)
{
	(void)work;
	(void)config;
	++baseline_calls;
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
	force_resolved_full = false;
	changed_pixels = 0;
	bbox_pixels = 13;
	rows_scanned = 0;
	full_update_ret = 0;
	index_resets = 0;
	full_update_calls = 0;
	incremental_select_calls = 0;
	baseline_calls = 0;
	row_runner_calls = 0;
	extend_calls = 0;
	publish_calls = 0;
	test_drm_exit_count = 0;
	ms912x->damage_scan_valid = true;
	ms912x->damage_scan_width = 4;
	ms912x->damage_scan_height = 2;
}

int main(void)
{
	struct ms912x_device ms912x;
	struct ms912x_scan_rect_index index;
	struct drm_rect *rects = NULL;
	unsigned int capacity = 0, count = 0;
	u32 frame[MS912X_MAX_WIDTH * 4] = {};
	u32 baseline[MS912X_MAX_WIDTH * 4] = {};
	u32 next[MS912X_MAX_WIDTH * 4] = {};
	int ret;

	reset_fixture(&ms912x, &index, &rects, &capacity, &count);
	ret = ms912x_damage_scan_find_snapshot_changes(
		&ms912x, frame, 4, 2, baseline, next, MS912X_MAX_WIDTH + 1,
		2, false, &rects, &capacity, &count);
	assert(ret == -EINVAL && count == 77 && !index_resets);
	assert(!ms912x.damage_scan_lock);

	reset_fixture(&ms912x, &index, &rects, &capacity, &count);
	force_resolved_full = true;
	ret = ms912x_damage_scan_find_snapshot_changes(
		&ms912x, frame, 4, 2, baseline, next, 4, 2, false,
		&rects, &capacity, &count);
	assert(ret == 0 && count == 1 && rects);
	assert(rects[0].x1 == 0 && rects[0].y1 == 0 &&
	       rects[0].x2 == 4 && rects[0].y2 == 2);
	assert(index_resets == 1 && index.reset_height == 2);
	assert(full_update_calls == 1 && !row_runner_calls && publish_calls == 1);
	assert(ms912x.damage_scan_valid && !ms912x.damage_scan_invalid);
	assert(ms912x.damage_scan_width == 4 && ms912x.damage_scan_height == 2);
	free(rects);

	reset_fixture(&ms912x, &index, &rects, &capacity, &count);
	force_resolved_full = true;
	full_update_ret = -ENOMEM;
	ret = ms912x_damage_scan_find_snapshot_changes(
		&ms912x, frame, 4, 2, baseline, next, 4, 2, false,
		&rects, &capacity, &count);
	assert(ret == -ENOMEM && count == 0 && !publish_calls);
	assert(!ms912x.damage_scan_lock);
	free(rects);

	reset_fixture(&ms912x, &index, &rects, &capacity, &count);
	ret = ms912x_damage_scan_find_snapshot_changes(
		&ms912x, frame, 4, 5, baseline, next, 4, 2, false,
		&rects, &capacity, &count);
	assert(ret == 0 && count == 0 && row_runner_calls == 1);
	assert(incremental_select_calls == 1 && baseline_calls == 1);
	assert(!extend_calls && !publish_calls && ms912x.damage_scan_valid);
	assert(!ms912x.damage_scan_invalid && !ms912x.damage_scan_lock);

	reset_fixture(&ms912x, &index, &rects, &capacity, &count);
	changed_pixels = 1;
	rows_scanned = 2;
	ret = ms912x_damage_scan_find_snapshot_changes(
		&ms912x, frame, 5, 5, baseline, next, 4, 2, false,
		&rects, &capacity, &count);
	assert(ret == 1 && count == 0 && row_runner_calls == 1);
	assert(extend_calls == 1 && publish_calls == 1);
	assert(ms912x.damage_scan_valid && !ms912x.damage_scan_invalid);
	assert(!ms912x.damage_scan_lock);
	free(rects);

	puts("PASS damage_scan entry: 5 validation, full, incremental and cleanup cases");
	return 0;
}
