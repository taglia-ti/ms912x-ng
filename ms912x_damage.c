// SPDX-License-Identifier: GPL-2.0-only

#include <linux/iosys-map.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/string.h>

#include <drm/drm_framebuffer.h>
#include <drm/drm_rect.h>

#include "ms912x.h"
#include "ms912x_scan_methods.h"

unsigned int ms912x_damage_scan_effective_tolerance(
					struct ms912x_device *ms912x)
{
	unsigned int minimum = min_t(unsigned int,
		READ_ONCE(ms912x_parameters.damage_scan_tolerance), 255);
	unsigned int maximum = min_t(unsigned int,
		READ_ONCE(ms912x_parameters.damage_scan_tolerance_max), 255);

	return ms912x_damage_scan_auto_tolerance(
		READ_ONCE(ms912x->present_queue_pressure_ns),
		READ_ONCE(ms912x->present_queue_low_watermark_ns),
		READ_ONCE(ms912x->present_queue_high_watermark_ns),
		minimum, maximum);
}

void ms912x_damage_scan_invalidate(struct ms912x_device *ms912x)
{
	if (!ms912x->damage_scan_snapshot)
		return;

	mutex_lock(&ms912x->damage_scan_lock);
	ms912x->damage_scan_valid = false;
	atomic_set(&ms912x->damage_scan_invalid, 1);
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

static void ms912x_damage_scan_add_rect(struct drm_rect **rects,
					unsigned int *capacity,
					unsigned int *count,
					const struct drm_rect *rect,
					struct ms912x_scan_frame_stats *frame_stats,
					unsigned int width,
					unsigned int height)
{
	struct drm_rect appended = *rect;
	struct drm_rect bounds = DRM_RECT_INIT(0, 0, width, height);
	struct drm_rect *grown;
	unsigned int join_y = appended.y1, i;

	if (*count == 1 && drm_rect_equals(&(*rects)[0], &bounds))
		return;
	while (true) {
		bool joined_layer = false;

		while (true) {
			bool joined = false;

			for (i = 0; i < *count; i++) {
				struct drm_rect *existing = &(*rects)[i];

				if (existing->y2 != join_y ||
				    existing->x1 >= appended.x2 ||
				    appended.x1 >= existing->x2)
					continue;

				appended.x1 = min(appended.x1, existing->x1);
				appended.x2 = max(appended.x2, existing->x2);
				appended.y1 = min(appended.y1, existing->y1);
				*existing = (*rects)[--(*count)];
				joined = true;
				joined_layer = true;
				break;
			}
			if (!joined)
				break;
		}
		if (!joined_layer)
			break;
		join_y = appended.y1;
	}
	if (drm_rect_equals(&appended, &bounds)) {
		(*rects)[0] = bounds;
		*count = 1;
		return;
	}
	if (*count == *capacity) {
		grown = ms912x_rect_vector_grow(*rects, capacity, *count + 1,
						MS912X_MAX_DAMAGE_RECTS);
		if (grown)
			*rects = grown;
	}
	if (*count < *capacity) {
		(*rects)[(*count)++] = appended;
		return;
	}
	for (i = 0; i < *count; i++) {
		appended.x1 = min(appended.x1, (*rects)[i].x1);
		appended.y1 = min(appended.y1, (*rects)[i].y1);
		appended.x2 = max(appended.x2, (*rects)[i].x2);
		appended.y2 = max(appended.y2, (*rects)[i].y2);
	}
	(*rects)[0] = appended;
	*count = 1;
	if (frame_stats)
		frame_stats->rect_overflows++;
}

static __always_inline void
ms912x_damage_scan_process_row(const u32 *frame, u32 *next_snapshot,
			       unsigned int width, unsigned int height,
			       unsigned int stride, unsigned int y,
			       const struct ms912x_scan_row_result *row_result,
			       unsigned int block_pixels,
			       unsigned int tolerance,
			       const u32 *baseline,
			       struct drm_rect **rects,
			       unsigned int *rect_capacity,
			       unsigned int *rect_count,
			       bool selective_baseline, u64 *changed_pixels,
			       struct ms912x_scan_frame_stats *frame_stats)
{
	unsigned int copy_y, row_y1, cursor = 0, run_start, run_end;

	*changed_pixels += row_result->changed_pixels;
	if (row_result->changed_pixels) {
		row_y1 = y >= stride - 1 ? y - stride + 1 : 0;
		while (ms912x_scan_row_next_run(
			       baseline + y * MS912X_MAX_WIDTH,
			       frame + y * MS912X_MAX_WIDTH, width, block_pixels,
			       tolerance, &cursor, &run_start, &run_end)) {
			struct drm_rect changed = DRM_RECT_INIT(run_start, row_y1,
						 run_end - run_start,
						 y + 1 - row_y1);

			ms912x_damage_scan_add_rect(rects, rect_capacity,
						   rect_count, &changed, frame_stats,
						   width, height);
			if (frame_stats)
				frame_stats->rect_add_calls++;

			if (!selective_baseline) {
				for (copy_y = row_y1; copy_y < y; copy_y++)
					memcpy(next_snapshot + copy_y *
					       MS912X_MAX_WIDTH + run_start,
					       frame + copy_y * MS912X_MAX_WIDTH +
					       run_start,
					       (run_end - run_start) * sizeof(*frame));
			}
		}
	}

	if (!selective_baseline)
		memcpy(next_snapshot + y * MS912X_MAX_WIDTH,
		       frame + y * MS912X_MAX_WIDTH, width * sizeof(*frame));
}

static bool ms912x_damage_scan_simd_method_available(
	enum ms912x_damage_scan_method method)
{
	switch (method) {
	case MS912X_DAMAGE_SCAN_AVX2:
		return ms912x_scan_avx2_available();
	case MS912X_DAMAGE_SCAN_NEON:
		return ms912x_scan_neon_available();
	case MS912X_DAMAGE_SCAN_SSE2:
		return ms912x_scan_sse2_available();
	case MS912X_DAMAGE_SCAN_SSE41:
		return ms912x_scan_sse41_available();
	default:
		return false;
	}
}

static enum ms912x_damage_scan_method
ms912x_damage_scan_select_auto(bool tolerance, bool x_stable)
{
	if ((!tolerance && !x_stable))
		return MS912X_DAMAGE_SCAN_SCALAR;
	if (ms912x_scan_avx2_available())
		return MS912X_DAMAGE_SCAN_AVX2;
	if (ms912x_scan_neon_available())
		return MS912X_DAMAGE_SCAN_NEON;
	/* SSE4.1 accelerates relaxed bounds; exact PTEST is an explicit option. */
	if (tolerance && ms912x_scan_sse41_available())
		return MS912X_DAMAGE_SCAN_SSE41;
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
	if (method == MS912X_DAMAGE_SCAN_AVX2 ||
	    method == MS912X_DAMAGE_SCAN_NEON ||
	    method == MS912X_DAMAGE_SCAN_SSE2 ||
	    method == MS912X_DAMAGE_SCAN_SSE41) {
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
	if (method == MS912X_DAMAGE_SCAN_AVX2)
		ms912x_scan_rows_avx2_blocks(old, new, width, row_stride,
					     row_count, results);
	else if (method == MS912X_DAMAGE_SCAN_SSE41)
		ms912x_scan_rows_sse41_blocks(old, new, width, row_stride,
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
	unsigned int row_count, unsigned int tolerance,
	struct ms912x_scan_row_result *results)
{
#ifdef CONFIG_X86_64
	if (method == MS912X_DAMAGE_SCAN_AVX2)
		ms912x_scan_rows_avx2_relaxed(old, new, next, width, row_stride,
					      row_count, tolerance, results);
	else if (method == MS912X_DAMAGE_SCAN_SSE41)
		ms912x_scan_rows_sse41_relaxed(old, new, next, width, row_stride,
					       row_count, tolerance, results);
	else
		ms912x_scan_rows_sse2_relaxed(old, new, next, width, row_stride,
					      row_count, tolerance, results);
#elif MS912X_HAS_NEON
	ms912x_scan_rows_neon_relaxed(old, new, next, width, row_stride,
					      row_count, tolerance, results);
#endif
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
				   struct drm_rect **rects,
				   unsigned int *rect_capacity,
				   unsigned int *rect_count)
{
	struct drm_rect bounds = DRM_RECT_INIT(0, 0, width, height);
	u64 start_ns = 0, changed_pixels = 0;
	struct ms912x_scan_frame_stats frame_stats = {};
	u64 bbox_pixels = 0;
	unsigned int y, stride, rows_scanned = 0, i;
	unsigned int run_block_pixels = 1, run_tolerance;
	unsigned int config_generation;
	unsigned int tolerance;
	enum ms912x_damage_scan_method method;
	struct drm_rect *grown;
	bool full_update, use_memcmp = false;
	bool use_simd, use_relaxed;
	bool stats_enabled = ms912x_stats_enabled();
	int ret = 1;

	if (width > MS912X_MAX_WIDTH || height > MS912X_MAX_HEIGHT)
		return -EINVAL;
	if (stats_enabled)
		start_ns = ktime_get_ns();
	*rect_count = 0;
	mutex_lock(&ms912x->damage_scan_lock);

	config_generation = atomic_read(
		&ms912x_parameters.damage_scan_config_generation);
	if (config_generation !=
	    READ_ONCE(ms912x->damage_scan_config_generation)) {
		WRITE_ONCE(ms912x->damage_scan_config_generation,
			   config_generation);
		atomic_set(&ms912x->damage_scan_invalid, 1);
	}
	if (config_generation !=
	    READ_ONCE(ms912x->damage_scan_probe_config_generation)) {
		WRITE_ONCE(ms912x->damage_scan_probe_config_generation,
			   config_generation);
		ms912x->damage_scan_x_probe_done = false;
		ms912x->damage_scan_x_stable = false;
	}
	if (frame_generation != ms912x->damage_scan_probe_generation) {
		ms912x->damage_scan_probe_generation = frame_generation;
		ms912x->damage_scan_x_probe_done = false;
		ms912x->damage_scan_x_stable = false;
	}
	method = READ_ONCE(ms912x_parameters.damage_scan_method);
	tolerance = ms912x_damage_scan_effective_tolerance(ms912x);
	stride = clamp_t(unsigned int,
				     READ_ONCE(ms912x_parameters.damage_scan_stride_override) ?
			 READ_ONCE(ms912x_parameters.damage_scan_stride) :
				     READ_ONCE(ms912x->damage_scan_stride_default), 1, 16);
	full_update = force_full || atomic_read(&ms912x->damage_scan_invalid) ||
		!ms912x->damage_scan_valid ||
		ms912x->damage_scan_width != width ||
		ms912x->damage_scan_height != height;
	if (full_update) {
		grown = ms912x_rect_vector_grow(*rects, rect_capacity, 1,
						MS912X_MAX_DAMAGE_RECTS);
		if (!grown) {
			ret = -ENOMEM;
			goto out_unlock;
		}
		*rects = grown;
		(*rects)[0] = bounds;
		*rect_count = 1;
		changed_pixels = (u64)width * height;
		if ((method == MS912X_DAMAGE_SCAN_METHOD_AUTO ||
		     method == MS912X_DAMAGE_SCAN_MEMCMP ||
		     ((method == MS912X_DAMAGE_SCAN_AVX2 ||
		       method == MS912X_DAMAGE_SCAN_NEON ||
		       method == MS912X_DAMAGE_SCAN_SSE2 ||
		       method == MS912X_DAMAGE_SCAN_SSE41) && !tolerance)) &&
		    !ms912x->damage_scan_x_probe_done) {
			ms912x->damage_scan_x_stable =
				ms912x_damage_scan_probe_x_uniform(frame,
								  width, height);
			ms912x->damage_scan_x_probe_done = true;
		}
		if (method == MS912X_DAMAGE_SCAN_METHOD_AUTO)
			method = ms912x_damage_scan_select_auto(
				tolerance,
				ms912x->damage_scan_x_probe_done &&
				ms912x->damage_scan_x_stable);
		if ((method == MS912X_DAMAGE_SCAN_AVX2 ||
		     method == MS912X_DAMAGE_SCAN_NEON ||
		     method == MS912X_DAMAGE_SCAN_SSE2 ||
		     method == MS912X_DAMAGE_SCAN_SSE41) &&
		    (!ms912x_damage_scan_simd_method_available(method) ||
		     (!tolerance && !ms912x->damage_scan_x_stable)))
			method = MS912X_DAMAGE_SCAN_SCALAR;
		if (method == MS912X_DAMAGE_SCAN_MEMCMP &&
		    !ms912x->damage_scan_x_stable)
			method = MS912X_DAMAGE_SCAN_SCALAR;
		WRITE_ONCE(ms912x->damage_scan_backend, method);
		for (y = 0; y < height; y++)
			memcpy(next_snapshot +
			       y * MS912X_MAX_WIDTH,
			       frame + y * MS912X_MAX_WIDTH,
			       width * sizeof(*frame));
		goto out_damage;
	}

	if (method == MS912X_DAMAGE_SCAN_METHOD_AUTO)
		method = ms912x_damage_scan_select_auto(
			tolerance, ms912x->damage_scan_x_probe_done &&
			ms912x->damage_scan_x_stable);
	use_memcmp = method == MS912X_DAMAGE_SCAN_MEMCMP &&
		ms912x->damage_scan_x_probe_done &&
		ms912x->damage_scan_x_stable;
	use_simd = (method == MS912X_DAMAGE_SCAN_AVX2 ||
		    method == MS912X_DAMAGE_SCAN_NEON ||
		    method == MS912X_DAMAGE_SCAN_SSE2 ||
		    method == MS912X_DAMAGE_SCAN_SSE41) && !tolerance &&
		ms912x->damage_scan_x_probe_done &&
		ms912x->damage_scan_x_stable &&
		ms912x_damage_scan_simd_method_available(method);
	use_relaxed = method != MS912X_DAMAGE_SCAN_MEMCMP && tolerance;
	run_tolerance = use_relaxed ? tolerance : 0;
	if (use_memcmp)
		run_block_pixels = MS912X_SCAN_MEMCMP_BLOCK_BYTES / sizeof(*frame);
	else if (use_simd)
		run_block_pixels = method == MS912X_DAMAGE_SCAN_AVX2 ? 64 : 16;
	else if (use_relaxed &&
		 (method == MS912X_DAMAGE_SCAN_AVX2 ||
		  method == MS912X_DAMAGE_SCAN_NEON ||
		  method == MS912X_DAMAGE_SCAN_SSE2 ||
		  method == MS912X_DAMAGE_SCAN_SSE41) &&
		 ms912x_damage_scan_simd_method_available(method))
		run_block_pixels = method == MS912X_DAMAGE_SCAN_AVX2 ? 8 : 4;
	WRITE_ONCE(ms912x->damage_scan_backend,
		   use_memcmp ? MS912X_DAMAGE_SCAN_MEMCMP :
		   (use_simd || (use_relaxed &&
		    ms912x_damage_scan_simd_method_available(method))) ? method :
		   MS912X_DAMAGE_SCAN_SCALAR);
	for (y = 0; y < height; y++)
		memcpy(next_snapshot + y * MS912X_MAX_WIDTH,
		       baseline + y * MS912X_MAX_WIDTH,
		       width * sizeof(*frame));
	if (use_simd) {
		struct ms912x_scan_row_result results[
			MS912X_SCAN_AVX2_ROWS_PER_BATCH];
		unsigned int row = frame_sequence % stride;

		while (row < height) {
			unsigned int count = min_t(unsigned int,
				ARRAY_SIZE(results), DIV_ROUND_UP(height - row, stride));
			unsigned int r;

			ms912x_damage_scan_run_simd_blocks(
				method, baseline + row * MS912X_MAX_WIDTH,
				frame + row * MS912X_MAX_WIDTH, width,
				stride * MS912X_MAX_WIDTH, count, results);
			for (r = 0; r < count; r++) {
				y = row + r * stride;
				rows_scanned++;
				ms912x_damage_scan_process_row(
					frame, next_snapshot, width, height,
					stride, y, &results[r], run_block_pixels,
					run_tolerance, baseline, rects, rect_capacity,
					rect_count, false, &changed_pixels,
					stats_enabled ? &frame_stats : NULL);
			}
			row += count * stride;
		}
	} else if (use_relaxed &&
		   (method == MS912X_DAMAGE_SCAN_AVX2 ||
		    method == MS912X_DAMAGE_SCAN_NEON ||
		    method == MS912X_DAMAGE_SCAN_SSE2 ||
		    method == MS912X_DAMAGE_SCAN_SSE41) &&
		   ms912x_damage_scan_simd_method_available(method)) {
		struct ms912x_scan_row_result results[
			MS912X_SCAN_AVX2_ROWS_PER_BATCH];
		unsigned int row = frame_sequence % stride;

		while (row < height) {
			unsigned int count = min_t(unsigned int,
				ARRAY_SIZE(results), DIV_ROUND_UP(height - row, stride));
			unsigned int r;

			ms912x_damage_scan_run_simd_relaxed(
				method, baseline + row * MS912X_MAX_WIDTH,
				frame + row * MS912X_MAX_WIDTH,
				next_snapshot + row * MS912X_MAX_WIDTH, width,
				stride * MS912X_MAX_WIDTH, count, tolerance, results);
			for (r = 0; r < count; r++) {
				y = row + r * stride;
				rows_scanned++;
				ms912x_damage_scan_process_row(
					frame, next_snapshot, width, height,
					stride, y, &results[r], run_block_pixels,
					tolerance, baseline, rects, rect_capacity,
					rect_count, true, &changed_pixels,
					stats_enabled ? &frame_stats : NULL);
			}
			row += count * stride;
		}
	} else if (use_memcmp) {
		for (y = frame_sequence % stride; y < height; y += stride) {
			struct ms912x_scan_row_result row_result;

			rows_scanned++;
			ms912x_scan_row_memcmp(
				baseline + y * MS912X_MAX_WIDTH,
				frame + y * MS912X_MAX_WIDTH, width, &row_result);
			ms912x_damage_scan_process_row(
				frame, next_snapshot, width, height, stride, y,
				&row_result,
				run_block_pixels, run_tolerance, baseline, rects,
				rect_capacity, rect_count, false, &changed_pixels,
				stats_enabled ? &frame_stats : NULL);
		}
	} else if (use_relaxed) {
		for (y = frame_sequence % stride; y < height; y += stride) {
			struct ms912x_scan_row_result row_result;

			rows_scanned++;
			ms912x_scan_row_scalar_relaxed(
				baseline + y * MS912X_MAX_WIDTH,
				frame + y * MS912X_MAX_WIDTH,
				next_snapshot + y * MS912X_MAX_WIDTH, width,
				tolerance, &row_result);
			ms912x_damage_scan_process_row(
				frame, next_snapshot, width, height, stride, y,
				&row_result,
				1, tolerance, baseline, rects, rect_capacity, rect_count,
				true, &changed_pixels,
				stats_enabled ? &frame_stats : NULL);
		}
	} else {
		for (y = frame_sequence % stride; y < height; y += stride) {
			struct ms912x_scan_row_result row_result;

			rows_scanned++;
			ms912x_scan_row_scalar(
				baseline + y * MS912X_MAX_WIDTH,
				frame + y * MS912X_MAX_WIDTH, width, &row_result);
			ms912x_damage_scan_process_row(
				frame, next_snapshot, width, height, stride, y,
				&row_result,
				1, 0, baseline, rects, rect_capacity, rect_count, false,
				&changed_pixels, stats_enabled ? &frame_stats : NULL);
		}
	}
	if (!changed_pixels) {
		ms912x->damage_scan_valid = true;
		atomic_set(&ms912x->damage_scan_invalid, 0);
		ret = 0;
		goto out_unlock;
	}

out_damage:
	ms912x->damage_scan_width = width;
	ms912x->damage_scan_height = height;
	ms912x->damage_scan_valid = true;
	atomic_set(&ms912x->damage_scan_invalid, 0);
	if (stats_enabled)
		for (i = 0; i < *rect_count; i++)
			bbox_pixels += (u64)drm_rect_width(&(*rects)[i]) *
				drm_rect_height(&(*rects)[i]);
out_unlock:
	mutex_unlock(&ms912x->damage_scan_lock);
	if (stats_enabled) {
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

void ms912x_damage_scan_update_snapshot(struct ms912x_device *ms912x,
					  const struct drm_rect *rect)
{
	unsigned int y;

	for (y = rect->y1; y < rect->y2; y++) {
		u32 *snapshot = ms912x->damage_scan_snapshot +
			y * MS912X_MAX_WIDTH + rect->x1;
		size_t width = drm_rect_width(rect);

		memcpy(snapshot, ms912x->latest_xrgb +
		       y * MS912X_MAX_WIDTH + rect->x1,
		       width * sizeof(*snapshot));
	}
	ms912x->damage_scan_width = ms912x->video_in_width;
	ms912x->damage_scan_height = ms912x->video_in_height;
	ms912x->damage_scan_valid = true;
	atomic_set(&ms912x->damage_scan_invalid, 0);
}
