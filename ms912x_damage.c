// SPDX-License-Identifier: GPL-2.0-only

#include <linux/iosys-map.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/string.h>

#include <drm/drm_framebuffer.h>
#include <drm/drm_rect.h>

#include "ms912x.h"
#include "ms912x_pixels.h"
#include "ms912x_scan_methods.h"
#include "shared/ms912x_scan_backend.h"
#include "ms912x_scan_rects.h"

unsigned int ms912x_damage_scan_effective_tolerance(
					struct ms912x_device *ms912x)
{
	unsigned int minimum, maximum;

	if (READ_ONCE(ms912x->damage_scan_tolerance_is_overridden)) {
		minimum = READ_ONCE(ms912x->damage_scan_tolerance_override_min);
		maximum = READ_ONCE(ms912x->damage_scan_tolerance_override_max);
	} else if (READ_ONCE(ms912x_parameters.damage_scan_tolerance_override)) {
		minimum = READ_ONCE(ms912x_parameters.damage_scan_tolerance);
		maximum = READ_ONCE(ms912x_parameters.damage_scan_tolerance_max);
	} else {
		struct ms912x_profile_values values =
			ms912x_profile_values_for(
				READ_ONCE(ms912x->effective_profile));

		minimum = values.tolerance_min;
		maximum = values.tolerance_max;
	}

	return ms912x_damage_scan_auto_tolerance(
		READ_ONCE(ms912x->present_queue_pressure_ns),
		READ_ONCE(ms912x->present_queue_low_watermark_ns),
		READ_ONCE(ms912x->present_queue_high_watermark_ns),
		minimum, maximum);
}

void ms912x_damage_scan_invalidate_locked(struct ms912x_device *ms912x)
{
	lockdep_assert_held(&ms912x->damage_scan_lock);
	ms912x->damage_scan_invalidation_epoch++;
	ms912x->damage_scan_valid = false;
	atomic_set(&ms912x->damage_scan_invalid, 1);
}

void ms912x_damage_scan_invalidate(struct ms912x_device *ms912x)
{
	mutex_lock(&ms912x->damage_scan_lock);
	if (ms912x->damage_scan_snapshot)
		ms912x_damage_scan_invalidate_locked(ms912x);
	mutex_unlock(&ms912x->damage_scan_lock);
}

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
	/* Scan kernels compare u32 words. */
	unsigned int word_width;
	unsigned int row_words;
	unsigned int pixel_shift;
	u32 pixel_mask;
	bool selective_baseline;
	u64 changed_pixels;
	struct ms912x_scan_frame_stats *frame_stats;
};

struct ms912x_scan_config {
	unsigned int generation;
	u32 local_generation;
	unsigned int tolerance;
	unsigned int stride;
	unsigned int run_block_pixels;
	u32 pixel_mask;
	enum ms912x_damage_scan_method method;
	bool full_update;
	bool use_memcmp;
	bool use_relaxed;
};

static __always_inline void
ms912x_damage_scan_process_row(struct ms912x_scan_work *work, unsigned int y,
			       const struct ms912x_scan_row_result *row_result)
{
	unsigned int copy_y, row_y1, cursor, run_start, run_end;

	work->changed_pixels += row_result->changed_pixels << work->pixel_shift;
	if (row_result->changed_pixels) {
		row_y1 = y >= work->stride - 1 ? y - work->stride + 1 : 0;
		cursor = row_result->min_x / work->block_pixels *
			 work->block_pixels;
		while (ms912x_scan_row_next_run_mask(
			work->baseline + y * work->row_words,
			work->frame + y * work->row_words, work->word_width,
			row_result->max_x, work->block_pixels, work->pixel_mask,
			&cursor, &run_start, &run_end)) {
			struct drm_rect changed = DRM_RECT_INIT(
				run_start << work->pixel_shift, row_y1,
				(run_end - run_start) << work->pixel_shift,
				y + 1 - row_y1);

			if (ms912x_scan_rect_add(work->index, work->rects,
						 work->rect_capacity,
						 work->rect_count, &changed,
						 work->width, work->height) &&
			    work->frame_stats)
				work->frame_stats->rect_overflows++;
			if (work->frame_stats)
				work->frame_stats->rect_add_calls++;

			/* Advance emitted margins and, for in-place scans, the compared run. */
			for (copy_y = row_y1; copy_y < y + (work->selective_baseline &&
					work->next_snapshot == work->baseline); copy_y++)
				memcpy(work->next_snapshot +
					       copy_y * work->row_words +
					       run_start,
				       work->frame + copy_y * work->row_words +
					       run_start,
				       (run_end - run_start) *
					       sizeof(*work->frame));
		}
	}

	if (!work->selective_baseline)
		memcpy(work->next_snapshot + y * work->row_words,
		       work->frame + y * work->row_words,
		       work->word_width * sizeof(*work->frame));
}

/* Add the lower margin after compaction; the index joins touching bottom edges. */
static void ms912x_damage_scan_extend_bottom(struct ms912x_scan_work *work)
{
	unsigned int i, y;

	if (work->stride == 1)
		return;
	for (i = 0; i < *work->rect_count; i++) {
		struct drm_rect *rect = &(*work->rects)[i];
		unsigned int y2 =
			min_t(unsigned int, rect->y2 + work->stride - 1,
			      work->height);

		unsigned int first_word = rect->x1 >> work->pixel_shift;
		size_t bytes = (drm_rect_width(rect) >> work->pixel_shift) *
			       sizeof(*work->frame);

		for (y = rect->y2; y < y2; y++)
			memcpy(work->next_snapshot + y * work->row_words +
				       first_word,
			       work->frame + y * work->row_words + first_word,
			       bytes);
		rect->y2 = y2;
	}
}

const char *ms912x_damage_scan_selected_backend_name(
	struct ms912x_device *ms912x)
{
	enum ms912x_damage_scan_method method =
		READ_ONCE(ms912x_parameters.damage_scan_method);

	method = (enum ms912x_damage_scan_method)ms912x_core_scan_resolve_backend(
		(enum ms912x_scan_backend)method);
	return ms912x_param_damage_scan_method_name(method);
}

/* Caller holds damage_scan_lock. Resolve frame and parameter generations. */
static void ms912x_damage_scan_resolve_config_locked(
	struct ms912x_device *ms912x, u32 frame_generation,
	unsigned int width, unsigned int height, bool force_full,
	struct ms912x_scan_config *config)
{
	config->generation = atomic_read(
		&ms912x_parameters.damage_scan_config_generation);
	if (config->generation !=
	    READ_ONCE(ms912x->damage_scan_config_generation)) {
		WRITE_ONCE(ms912x->damage_scan_config_generation,
			   config->generation);
		ms912x_damage_scan_invalidate_locked(ms912x);
	}

	config->local_generation =
		READ_ONCE(ms912x->damage_scan_device_config_generation);
	if (config->local_generation !=
	    READ_ONCE(ms912x->damage_scan_local_config_generation)) {
		WRITE_ONCE(ms912x->damage_scan_local_config_generation,
			   config->local_generation);
		ms912x_damage_scan_invalidate_locked(ms912x);
	}

	config->method = READ_ONCE(ms912x_parameters.damage_scan_method);
	config->tolerance = ms912x_damage_scan_effective_tolerance(ms912x);
	config->pixel_mask = ms912x_scan_uyvy_tolerance_mask(config->tolerance);
	config->run_block_pixels = 1;
	config->stride = clamp_t(unsigned int,
		READ_ONCE(ms912x->damage_scan_stride_is_overridden) ?
		READ_ONCE(ms912x->damage_scan_stride_override_value) :
		READ_ONCE(ms912x_parameters.damage_scan_stride_override) ?
		READ_ONCE(ms912x_parameters.damage_scan_stride) :
		READ_ONCE(ms912x->damage_scan_stride_default), 1, 16);
	config->full_update = force_full ||
		atomic_read(&ms912x->damage_scan_invalid) ||
		!ms912x->damage_scan_valid ||
		ms912x->damage_scan_width != width ||
		ms912x->damage_scan_height != height;
}

/* UYVY has four meaningful bytes per word and needs no RGB X-byte probe. */
static void ms912x_damage_scan_select_full_backend_locked(struct ms912x_device *ms912x,
							  const u32 *frame, unsigned int width,
							  unsigned int height,
							  struct ms912x_scan_config *config)
{
	enum ms912x_damage_scan_method method = config->method;

	method = (enum ms912x_damage_scan_method)ms912x_core_scan_resolve_backend(
		(enum ms912x_scan_backend)method);
	config->method = method;
	WRITE_ONCE(ms912x->damage_scan_backend, method);
}

/* The single scan worker owns the vector and baseline pixels. */
static int ms912x_damage_scan_prepare_full_update(const u32 *frame, const struct drm_rect *bounds,
						  struct ms912x_scan_work *work)
{
	struct drm_rect *grown;
	unsigned int y;

	grown =
	    ms912x_rect_vector_grow(*work->rects, work->rect_capacity, 1, MS912X_MAX_DAMAGE_RECTS);
	if (!grown)
		return -ENOMEM;
	*work->rects = grown;
	(*work->rects)[0] = *bounds;
	*work->rect_count = 1;
	work->changed_pixels = (u64)work->width * work->height;
	for (y = 0; y < work->height; y++)
		memcpy(work->next_snapshot + y * work->row_words, frame + y * work->row_words,
		       work->word_width * sizeof(*frame));
	return 0;
}

/* Caller holds damage_scan_lock. Configure incremental comparison semantics. */
static void ms912x_damage_scan_select_incremental_backend_locked(struct ms912x_device *ms912x,
								 struct ms912x_scan_config *config)
{
	ms912x_damage_scan_select_full_backend_locked(ms912x, NULL, 0, 0, config);
	config->use_memcmp = config->method == MS912X_DAMAGE_SCAN_MEMCMP;
	config->pixel_mask = ms912x_core_scan_backend_mask(
		(enum ms912x_scan_backend)config->method, config->pixel_mask);
	config->use_relaxed = !config->use_memcmp;
	config->run_block_pixels = ms912x_core_scan_run_words(
		(enum ms912x_scan_backend)config->method, 1);
}

static void ms912x_damage_scan_prepare_baseline(
	struct ms912x_scan_work *work, const struct ms912x_scan_config *config)
{
	work->stride = config->stride;
	work->block_pixels = config->run_block_pixels;
	work->pixel_mask = config->pixel_mask;
	work->selective_baseline = config->use_relaxed;
	/* Separate-destination callers retain unsampled rows; production updates in place. */
	if (config->stride != 1 && work->next_snapshot != work->baseline)
		for (unsigned int y = 0; y < work->height; y++)
			memcpy(work->next_snapshot + y * work->row_words,
			       work->baseline + y * work->row_words,
			       work->word_width * sizeof(*work->frame));
}

static void ms912x_damage_scan_run_rows(
	struct ms912x_scan_work *work,
	const struct ms912x_scan_config *config, u64 frame_sequence,
	unsigned int *rows_scanned)
{
	struct ms912x_scan_row_result results[MS912X_SCAN_SIMD_ROWS_PER_BATCH];
	unsigned int row = frame_sequence % config->stride;

	while (row < work->height) {
		unsigned int count = min_t(unsigned int, ARRAY_SIZE(results),
			DIV_ROUND_UP(work->height - row, config->stride));

		ms912x_core_scan_rows(work->baseline + row * work->row_words,
			work->frame + row * work->row_words,
			work->next_snapshot == work->baseline ? NULL :
			work->next_snapshot + row * work->row_words,
			work->word_width, config->stride * work->row_words,
			count, config->pixel_mask, results,
			(enum ms912x_scan_backend)config->method);
		for (unsigned int r = 0; r < count; r++) {
			(*rows_scanned)++;
			ms912x_damage_scan_process_row(work, row + r * config->stride, &results[r]);
		}
		row += count * config->stride;
	}
}

bool ms912x_damage_scan_commit_locked(
	struct ms912x_device *ms912x,
	const struct ms912x_damage_scan_result *result,
	unsigned int width, unsigned int height)
{
	lockdep_assert_held(&ms912x->damage_scan_lock);
	if (result->invalidation_epoch != ms912x->damage_scan_invalidation_epoch ||
	    result->config_generation != atomic_read(
		    &ms912x_parameters.damage_scan_config_generation) ||
	    result->local_config_generation !=
		READ_ONCE(ms912x->damage_scan_device_config_generation))
		return false;

	ms912x->damage_scan_width = width;
	ms912x->damage_scan_height = height;
	ms912x->damage_scan_valid = true;
	atomic_set(&ms912x->damage_scan_invalid, 0);
	return true;
}

int ms912x_damage_scan_find_snapshot_changes(
				   struct ms912x_device *ms912x,
				   const u32 *frame,
				   u64 frame_sequence,
				   u32 frame_generation,
				   const u32 *baseline,
				   u32 *next_snapshot,
				   unsigned int width,
				   unsigned int height,
				   bool force_full,
				   struct ms912x_damage_scan_result *result,
				   struct drm_rect **rects,
				   unsigned int *rect_capacity,
				   unsigned int *rect_count)
{
	struct drm_rect bounds = DRM_RECT_INIT(0, 0, width, height);
	u64 start_ns = 0;
	struct ms912x_scan_frame_stats frame_stats = {};
	struct ms912x_scan_config config = {};
	unsigned int capacity = *rect_capacity;
	struct ms912x_scan_work scan = {
		.index = ms912x->scan_rect_index,
		.frame = frame,
		.baseline = baseline,
		.next_snapshot = next_snapshot,
		.rects = rects,
		.rect_capacity = &capacity,
		.rect_count = rect_count,
		.width = width,
		.height = height,
		.word_width = width / 2,
		.row_words = MS912X_MAX_WIDTH / 2,
		.pixel_shift = 1,
	};
	u64 bbox_pixels = 0;
	unsigned int rows_scanned = 0;
	bool stats_enabled = ms912x_stats_enabled();
	int ret = 1;

	if (!width || !height || width > MS912X_MAX_WIDTH ||
	    height > MS912X_MAX_HEIGHT || (width & 1))
		return -EINVAL;
	if (stats_enabled)
		start_ns = ktime_get_ns();
	*rect_count = 0;
	mutex_lock(&ms912x->damage_scan_lock);
	ms912x_damage_scan_resolve_config_locked(ms912x, frame_generation,
						 width, height, force_full,
						 &config);
	result->invalidation_epoch = ms912x->damage_scan_invalidation_epoch;
	result->config_generation = config.generation;
	result->local_config_generation = config.local_generation;
	if (config.full_update)
		ms912x_damage_scan_select_full_backend_locked(
			ms912x, frame, width, height, &config);
	else
		ms912x_damage_scan_select_incremental_backend_locked(ms912x, &config);
	mutex_unlock(&ms912x->damage_scan_lock);
	/*
	 * The running slot pins the baseline until publication completes.
	 * Incremental scans update only admitted spans in place, after comparing
	 * them. A rejected publication invalidates this speculative baseline.
	 * Other primary submissions queue a snapshot while a scan is pending;
	 * invalidation changes only metadata, and stop drains this worker.
	 */
	ms912x_scan_rect_index_reset(ms912x->scan_rect_index, height);
	if (config.full_update) {
		ret = ms912x_damage_scan_prepare_full_update(frame, &bounds, &scan);
		goto out;
	}

	ms912x_damage_scan_prepare_baseline(&scan, &config);
	scan.frame_stats = stats_enabled ? &frame_stats : NULL;
	ms912x_damage_scan_run_rows(&scan, &config, frame_sequence,
					   &rows_scanned);
	if (!scan.changed_pixels) {
		ret = 0;
		goto out;
	}

	ms912x_damage_scan_extend_bottom(&scan);

out:
	/* Debugfs samples capacity under this lock; pixel/vector work is private. */
	mutex_lock(&ms912x->damage_scan_lock);
	*rect_capacity = capacity;
	mutex_unlock(&ms912x->damage_scan_lock);
	if (stats_enabled) {
		for (unsigned int i = 0; i < *rect_count; i++)
			bbox_pixels += (u64)drm_rect_width(&(*rects)[i]) *
				drm_rect_height(&(*rects)[i]);
		/* Publish each retained scan counter at most once per frame. */
		atomic64_inc(&ms912x->stats.software_scan_frames);
		atomic64_add((u64)width * height,
			     &ms912x->stats.software_scan_frame_pixels);
		if (rows_scanned)
			atomic64_add((u64)width * rows_scanned,
				     &ms912x->stats.software_scan_compared_pixels);
		if (bbox_pixels)
			atomic64_add(bbox_pixels,
				     &ms912x->stats.software_scan_bbox_pixels);
		if (frame_stats.rect_overflows)
			atomic64_add(frame_stats.rect_overflows,
				     &ms912x->stats.scan_rect_overflow_merged_updates);
		if (frame_stats.rect_add_calls) {
			atomic64_add(frame_stats.rect_add_calls,
				     &ms912x->stats.software_scan_rect_add_calls);
			ms912x_stats_record_max(
				&ms912x->stats.software_scan_rect_add_max_calls,
				frame_stats.rect_add_calls);
		}
		if (*rect_count) {
			atomic64_add(*rect_count,
				     &ms912x->stats.software_scan_rectangles);
			ms912x_stats_record_max(
				&ms912x->stats.software_scan_rectangles_max,
				*rect_count);
		}
		atomic64_add(ktime_get_ns() - start_ns,
			     &ms912x->stats.software_scan_ns);
	}
	return ret;
}

void ms912x_damage_scan_copy_snapshot(u32 *snapshot, const u32 *pixels,
				     const struct drm_rect *rect)
{
	ms912x_pixels_copy_rect(snapshot, pixels, rect);
}
