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

/* Probe the ignored X byte over a uniform 8-by-4 grid once per epoch. */
static bool ms912x_damage_scan_probe_x_uniform(const u32 *frame,
					       unsigned int width,
					       unsigned int height)
{
	unsigned int row, column;
	u32 x_byte = 0;
	bool first = true;

	for (row = 0; row < 4; row++) {
		unsigned int y = ((2 * row + 1) * height) / 8;

		for (column = 0; column < 8; column++) {
			unsigned int x = ((2 * column + 1) * width) / 16;
			u32 sample = frame[y * MS912X_MAX_WIDTH + x] & 0xff000000;

			if (first) {
				x_byte = sample;
				first = false;
			} else if (sample != x_byte) {
				return false;
			}
		}
	}
	return true;
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
	unsigned int tolerance;
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
	unsigned int run_tolerance;
	u32 pixel_mask;
	enum ms912x_damage_scan_method method;
	bool full_update;
	bool use_memcmp;
	bool use_simd;
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

			/* Skipped rows in the emitted margin also advance their baseline. */
			for (copy_y = row_y1; copy_y < y; copy_y++)
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

static bool
ms912x_damage_scan_simd_method_available(enum ms912x_damage_scan_method method)
{
	switch (method) {
	case MS912X_DAMAGE_SCAN_AVX:
		return ms912x_scan_avx_available();
	case MS912X_DAMAGE_SCAN_NEON:
		return ms912x_scan_neon_available();
	case MS912X_DAMAGE_SCAN_SSE2:
		return ms912x_scan_sse2_available();
	default:
		return false;
	}
}

static enum ms912x_damage_scan_method
ms912x_damage_scan_select_auto(bool tolerance, bool x_stable)
{
	if ((!tolerance && !x_stable))
		return MS912X_DAMAGE_SCAN_SCALAR;
	if (ms912x_scan_avx_available())
		return MS912X_DAMAGE_SCAN_AVX;
	if (ms912x_scan_neon_available())
		return MS912X_DAMAGE_SCAN_NEON;
	if (ms912x_scan_sse2_available())
		return MS912X_DAMAGE_SCAN_SSE2;
	if (x_stable)
		return MS912X_DAMAGE_SCAN_MEMCMP;
	return MS912X_DAMAGE_SCAN_SCALAR;
}

const char *ms912x_damage_scan_selected_backend_name(
	struct ms912x_device *ms912x)
{
	enum ms912x_damage_scan_method method =
		READ_ONCE(ms912x_parameters.damage_scan_method);
	unsigned int tolerance =
		ms912x_damage_scan_effective_tolerance(ms912x);

	if (method == MS912X_DAMAGE_SCAN_METHOD_AUTO)
		method = ms912x_damage_scan_select_auto(tolerance != 0, true);
	if (method == MS912X_DAMAGE_SCAN_AVX ||
	    method == MS912X_DAMAGE_SCAN_NEON ||
	    method == MS912X_DAMAGE_SCAN_SSE2) {
		if (!ms912x_damage_scan_simd_method_available(method))
			method = MS912X_DAMAGE_SCAN_SCALAR;
	}
	return ms912x_param_damage_scan_method_name(method);
}

static void ms912x_damage_scan_run_simd_blocks(
	enum ms912x_damage_scan_method method, const u32 *old, const u32 *new,
	unsigned int width, unsigned int row_stride, unsigned int row_count,
	struct ms912x_scan_row_result *results)
{
#ifdef CONFIG_X86_64
	if (method == MS912X_DAMAGE_SCAN_AVX)
		ms912x_scan_rows_avx_blocks(old, new, width, row_stride,
					     row_count, results);
	else
		ms912x_scan_rows_sse2_blocks(old, new, width, row_stride,
					     row_count, results);
#elif MS912X_HAS_NEON
	ms912x_scan_rows_neon_blocks(old, new, width, row_stride,
				     row_count, results);
#endif
}

static void ms912x_damage_scan_run_simd_relaxed(
	enum ms912x_damage_scan_method method, const u32 *old, const u32 *new,
	u32 *next, unsigned int width, unsigned int row_stride,
	unsigned int row_count, u32 pixel_mask,
	struct ms912x_scan_row_result *results)
{
#ifdef CONFIG_X86_64
	if (method == MS912X_DAMAGE_SCAN_AVX)
		ms912x_scan_rows_avx_relaxed_masked(old, new, next, width,
						     row_stride, row_count,
						     pixel_mask, results);
	else
		ms912x_scan_rows_sse2_relaxed_masked(old, new, next, width,
						      row_stride, row_count,
						      pixel_mask, results);
#elif MS912X_HAS_NEON
	ms912x_scan_rows_neon_relaxed_masked(old, new, next, width,
					     row_stride, row_count,
					     pixel_mask, results);
#endif
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
	if (config->generation !=
	    READ_ONCE(ms912x->damage_scan_probe_config_generation)) {
		WRITE_ONCE(ms912x->damage_scan_probe_config_generation,
			   config->generation);
		ms912x->damage_scan_x_probe_done = false;
		ms912x->damage_scan_x_stable = false;
	}
	config->local_generation =
		READ_ONCE(ms912x->damage_scan_device_config_generation);
	if (config->local_generation !=
	    READ_ONCE(ms912x->damage_scan_local_config_generation)) {
		WRITE_ONCE(ms912x->damage_scan_local_config_generation,
			   config->local_generation);
		ms912x_damage_scan_invalidate_locked(ms912x);
	}
	if (config->local_generation !=
	    READ_ONCE(ms912x->damage_scan_local_probe_config_generation)) {
		WRITE_ONCE(ms912x->damage_scan_local_probe_config_generation,
			   config->local_generation);
		ms912x->damage_scan_x_probe_done = false;
		ms912x->damage_scan_x_stable = false;
	}
	if (frame_generation != ms912x->damage_scan_probe_generation) {
		ms912x->damage_scan_probe_generation = frame_generation;
		ms912x->damage_scan_x_probe_done = false;
		ms912x->damage_scan_x_stable = false;
	}
	config->method = READ_ONCE(ms912x_parameters.damage_scan_method);
	config->tolerance = ms912x_damage_scan_effective_tolerance(ms912x);
	config->pixel_mask = ms912x_scan_tolerance_mask(config->tolerance);
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

/* Caller holds damage_scan_lock. Probe/select once before unlocked preparation. */
static void ms912x_damage_scan_select_full_backend_locked(
	struct ms912x_device *ms912x, const u32 *frame, unsigned int width,
	unsigned int height, struct ms912x_scan_config *config)
{
	enum ms912x_damage_scan_method method = config->method;
	unsigned int tolerance = config->tolerance;

	if ((method == MS912X_DAMAGE_SCAN_METHOD_AUTO ||
	     method == MS912X_DAMAGE_SCAN_MEMCMP ||
	     ((method == MS912X_DAMAGE_SCAN_AVX ||
	       method == MS912X_DAMAGE_SCAN_NEON ||
	       method == MS912X_DAMAGE_SCAN_SSE2) && !tolerance)) &&
	    !ms912x->damage_scan_x_probe_done) {
		ms912x->damage_scan_x_stable =
			ms912x_damage_scan_probe_x_uniform(frame, width, height);
		ms912x->damage_scan_x_probe_done = true;
	}
	if (method == MS912X_DAMAGE_SCAN_METHOD_AUTO)
		method = ms912x_damage_scan_select_auto(
			tolerance, ms912x->damage_scan_x_probe_done &&
			ms912x->damage_scan_x_stable);
	if ((method == MS912X_DAMAGE_SCAN_AVX ||
	     method == MS912X_DAMAGE_SCAN_NEON ||
	     method == MS912X_DAMAGE_SCAN_SSE2) &&
	    (!ms912x_damage_scan_simd_method_available(method) ||
	     (!tolerance && !ms912x->damage_scan_x_stable)))
		method = MS912X_DAMAGE_SCAN_SCALAR;
	if (method == MS912X_DAMAGE_SCAN_MEMCMP &&
	    !ms912x->damage_scan_x_stable)
		method = MS912X_DAMAGE_SCAN_SCALAR;
	config->method = method;
	WRITE_ONCE(ms912x->damage_scan_backend, method);
}

/* The single scan worker owns the vector and both baseline buffers. */
static int ms912x_damage_scan_prepare_full_update(
	const u32 *frame, const struct drm_rect *bounds,
	struct ms912x_scan_work *work)
{
	struct drm_rect *grown;
	unsigned int y;

	grown = ms912x_rect_vector_grow(*work->rects, work->rect_capacity, 1,
					MS912X_MAX_DAMAGE_RECTS);
	if (!grown)
		return -ENOMEM;
	*work->rects = grown;
	(*work->rects)[0] = *bounds;
	*work->rect_count = 1;
	work->changed_pixels = (u64)work->width * work->height;
	for (y = 0; y < work->height; y++)
		memcpy(work->next_snapshot + y * work->row_words,
		       frame + y * work->row_words,
		       work->word_width * sizeof(*frame));
	return 0;
}

/* Caller holds damage_scan_lock. Configure incremental comparison semantics. */
static void ms912x_damage_scan_select_incremental_backend_locked(
	struct ms912x_device *ms912x, struct ms912x_scan_config *config)
{
	enum ms912x_damage_scan_method method = config->method;
	unsigned int tolerance = config->tolerance;

	if (method == MS912X_DAMAGE_SCAN_METHOD_AUTO)
		method = ms912x_damage_scan_select_auto(
			tolerance, ms912x->damage_scan_x_probe_done &&
			ms912x->damage_scan_x_stable);
	config->method = method;
	config->use_memcmp = method == MS912X_DAMAGE_SCAN_MEMCMP &&
		ms912x->damage_scan_x_probe_done &&
		ms912x->damage_scan_x_stable;
	config->use_simd = (method == MS912X_DAMAGE_SCAN_AVX ||
			    method == MS912X_DAMAGE_SCAN_NEON ||
			    method == MS912X_DAMAGE_SCAN_SSE2) && !tolerance &&
		ms912x->damage_scan_x_probe_done &&
		ms912x->damage_scan_x_stable &&
		ms912x_damage_scan_simd_method_available(method);
	config->use_relaxed = method != MS912X_DAMAGE_SCAN_MEMCMP && tolerance;
	config->run_tolerance = config->use_relaxed ? tolerance : 0;
	if (config->use_relaxed)
		config->run_block_pixels = MS912X_SCAN_MASKED_BLOCK_PIXELS;
	else if (config->use_memcmp)
		config->run_block_pixels =
			MS912X_SCAN_MEMCMP_BLOCK_BYTES / sizeof(u32);
	else if (config->use_simd)
		config->run_block_pixels =
			method == MS912X_DAMAGE_SCAN_AVX ? 64 : 16;
	WRITE_ONCE(ms912x->damage_scan_backend,
		   config->use_memcmp ? MS912X_DAMAGE_SCAN_MEMCMP :
		   (config->use_simd || (config->use_relaxed &&
		    ms912x_damage_scan_simd_method_available(method))) ? method :
		   MS912X_DAMAGE_SCAN_SCALAR);
}

static void ms912x_damage_scan_prepare_baseline(
	struct ms912x_scan_work *work, const struct ms912x_scan_config *config)
{
	work->stride = config->stride;
	work->block_pixels = config->run_block_pixels;
	work->tolerance = config->run_tolerance;
	work->pixel_mask = config->pixel_mask;
	work->selective_baseline = config->use_relaxed;
	/* Every sampled row is written completely by the exact or masked path. */
	if (config->stride != 1)
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
	unsigned int y;

	if (config->use_simd && !config->use_relaxed) {
		struct ms912x_scan_row_result results[
			MS912X_SCAN_SIMD_ROWS_PER_BATCH];
		unsigned int row = frame_sequence % config->stride;

		while (row < work->height) {
			unsigned int count = min_t(unsigned int,
				ARRAY_SIZE(results),
				DIV_ROUND_UP(work->height - row, config->stride));
			unsigned int r;

			ms912x_damage_scan_run_simd_blocks(
				config->method,
				work->baseline + row * work->row_words,
				work->frame + row * work->row_words,
				work->word_width, config->stride * work->row_words,
				count, results);
			for (r = 0; r < count; r++) {
				y = row + r * config->stride;
				(*rows_scanned)++;
				ms912x_damage_scan_process_row(work, y, &results[r]);
			}
			row += count * config->stride;
		}
	} else if (config->use_relaxed &&
		   (config->method == MS912X_DAMAGE_SCAN_AVX ||
		    config->method == MS912X_DAMAGE_SCAN_NEON ||
		    config->method == MS912X_DAMAGE_SCAN_SSE2) &&
		   ms912x_damage_scan_simd_method_available(config->method)) {
		struct ms912x_scan_row_result results[
			MS912X_SCAN_SIMD_ROWS_PER_BATCH];
		unsigned int row = frame_sequence % config->stride;

		while (row < work->height) {
			unsigned int count = min_t(unsigned int,
				ARRAY_SIZE(results),
				DIV_ROUND_UP(work->height - row, config->stride));
			unsigned int r;

			ms912x_damage_scan_run_simd_relaxed(
				config->method,
				work->baseline + row * work->row_words,
				work->frame + row * work->row_words,
				work->next_snapshot + row * work->row_words,
				work->word_width, config->stride * work->row_words,
				count, config->pixel_mask, results);
			for (r = 0; r < count; r++) {
				y = row + r * config->stride;
				(*rows_scanned)++;
				ms912x_damage_scan_process_row(work, y, &results[r]);
			}
			row += count * config->stride;
		}
	} else if (config->use_memcmp) {
		for (y = frame_sequence % config->stride; y < work->height;
		     y += config->stride) {
			struct ms912x_scan_row_result row_result;

			(*rows_scanned)++;
			ms912x_scan_row_memcmp(
				work->baseline + y * work->row_words,
				work->frame + y * work->row_words,
				work->word_width, &row_result);
			ms912x_damage_scan_process_row(work, y, &row_result);
		}
	} else if (config->use_relaxed) {
		for (y = frame_sequence % config->stride; y < work->height;
		     y += config->stride) {
			struct ms912x_scan_row_result row_result;

			(*rows_scanned)++;
			ms912x_scan_row_scalar_masked(
				work->baseline + y * work->row_words,
				work->frame + y * work->row_words,
				work->next_snapshot + y * work->row_words,
				work->word_width, work->pixel_mask, &row_result);
			ms912x_damage_scan_process_row(work, y, &row_result);
		}
	} else {
		for (y = frame_sequence % config->stride; y < work->height;
		     y += config->stride) {
			struct ms912x_scan_row_result row_result;

			(*rows_scanned)++;
			ms912x_scan_row_scalar(
				work->baseline + y * work->row_words,
				work->frame + y * work->row_words,
				work->word_width, &row_result);
			ms912x_damage_scan_process_row(work, y, &row_result);
		}
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
		.word_width = width,
		.row_words = MS912X_MAX_WIDTH,
		.pixel_shift = 0,
	};
	u64 bbox_pixels = 0;
	unsigned int rows_scanned = 0;
	bool stats_enabled = ms912x_stats_enabled();
	int ret = 1;

	if (width > MS912X_MAX_WIDTH || height > MS912X_MAX_HEIGHT)
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
	 * The running slot pins both baseline roles until publication completes.
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
