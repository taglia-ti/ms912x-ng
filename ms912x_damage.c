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
	unsigned int minimum = 1, maximum = 64;

	if (READ_ONCE(ms912x_parameters.damage_scan_tolerance_override)) {
		minimum = min_t(unsigned int,
				READ_ONCE(ms912x_parameters.damage_scan_tolerance),
				255);
		maximum = min_t(unsigned int,
				READ_ONCE(ms912x_parameters.damage_scan_tolerance_max),
				255);
	}
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

static void ms912x_damage_scan_add_rect(struct drm_rect **rects,
					unsigned int *capacity,
					unsigned int *count,
					const struct drm_rect *rect,
					struct ms912x_device *ms912x,
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
	MS912X_STAT_INC(ms912x, scan_rect_overflow_merged_updates);
}

static __always_inline void
ms912x_damage_scan_process_row(struct ms912x_device *ms912x,
			       const u32 *frame, u32 *next_snapshot,
			       unsigned int width, unsigned int height,
			       unsigned int stride, unsigned int y,
			       const struct ms912x_scan_row_result *row_result,
				       unsigned int block_pixels,
				       unsigned int tolerance,
				       const u32 *baseline,
				       struct drm_rect **rects,
				       unsigned int *rect_capacity,
				       unsigned int *rect_count,
			       bool stats_enabled, bool selective_baseline,
			       u64 *changed_pixels,
			       u64 *copy_ns, u64 *loop_copy_ns,
				       u64 *rect_add_ns,
			       unsigned int *rect_add_calls)
{
	unsigned int copy_y, row_y1, cursor = 0, run_start, run_end;
	u64 stage_start_ns;

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

			if (stats_enabled)
				stage_start_ns = ktime_get_ns();
			ms912x_damage_scan_add_rect(rects, rect_capacity,
						   rect_count, &changed, ms912x,
						   width, height);
			if (stats_enabled) {
				*rect_add_ns += ktime_get_ns() - stage_start_ns;
				(*rect_add_calls)++;
			}

			if (!selective_baseline) {
				if (stats_enabled)
					stage_start_ns = ktime_get_ns();
				for (copy_y = row_y1; copy_y < y; copy_y++)
					memcpy(next_snapshot + copy_y *
					       MS912X_MAX_WIDTH + run_start,
					       frame + copy_y * MS912X_MAX_WIDTH +
				       run_start,
				       (run_end - run_start) * sizeof(*frame));
				if (stats_enabled) {
					u64 backfill_copy_ns =
						ktime_get_ns() - stage_start_ns;

					*copy_ns += backfill_copy_ns;
					*loop_copy_ns += backfill_copy_ns;
				}
			}
		}
	}

	if (!selective_baseline) {
		if (stats_enabled)
			stage_start_ns = ktime_get_ns();
		memcpy(next_snapshot + y * MS912X_MAX_WIDTH,
		       frame + y * MS912X_MAX_WIDTH, width * sizeof(*frame));
		if (stats_enabled) {
			u64 row_copy_ns = ktime_get_ns() - stage_start_ns;

			*copy_ns += row_copy_ns;
			*loop_copy_ns += row_copy_ns;
		}
	}
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
	u64 memcmp_blocks = 0, memcmp_equal_blocks = 0;
	u64 avx2_blocks = 0, avx2_equal_blocks = 0;
	u64 copy_ns = 0, compare_ns = 0, rect_add_ns = 0;
	u64 loop_copy_ns = 0, scan_loop_start_ns = 0, scan_loop_ns;
	u64 stage_start_ns;
	unsigned int rect_add_calls = 0;
	unsigned int y, stride, rows_scanned = 0, i;
	unsigned int run_block_pixels = 1, run_tolerance;
	unsigned int config_generation;
	unsigned int tolerance;
	enum ms912x_damage_scan_method method;
	struct drm_rect *grown;
	bool full_update, use_memcmp = false, use_avx2 = false;
	bool use_simd, use_relaxed, use_avx2_relaxed = false;
	bool stats_enabled = ms912x_stats_enabled();
	int ret = 1;

	if (width > MS912X_MAX_WIDTH || height > MS912X_MAX_HEIGHT)
		return -EINVAL;
	if (stats_enabled)
		start_ns = ktime_get_ns();
	MS912X_STAT_INC(ms912x, software_scan_frames);
	MS912X_STAT_ADD(ms912x, software_scan_frame_pixels,
			(u64)width * height);
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
		if (stats_enabled)
			stage_start_ns = ktime_get_ns();
		for (y = 0; y < height; y++)
			memcpy(next_snapshot +
			       y * MS912X_MAX_WIDTH,
			       frame + y * MS912X_MAX_WIDTH,
			       width * sizeof(*frame));
		if (stats_enabled)
			copy_ns += ktime_get_ns() - stage_start_ns;
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
	use_avx2 = method == MS912X_DAMAGE_SCAN_AVX2 && use_simd &&
		ms912x_scan_avx2_available();
	use_relaxed = method != MS912X_DAMAGE_SCAN_MEMCMP && tolerance;
	use_avx2_relaxed = method == MS912X_DAMAGE_SCAN_AVX2 && tolerance &&
		ms912x_scan_avx2_available();
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
	if (use_memcmp)
		MS912X_STAT_INC(ms912x, software_scan_memcmp_frames);
	if (use_avx2)
		MS912X_STAT_INC(ms912x, software_scan_avx2_frames);
	if (use_avx2_relaxed)
		MS912X_STAT_INC(ms912x, software_scan_avx2_relaxed_frames);
	if (stats_enabled)
		stage_start_ns = ktime_get_ns();
	for (y = 0; y < height; y++)
		memcpy(next_snapshot + y * MS912X_MAX_WIDTH,
		       baseline + y * MS912X_MAX_WIDTH,
		       width * sizeof(*frame));
	if (stats_enabled)
		copy_ns += ktime_get_ns() - stage_start_ns;
	if (stats_enabled)
		scan_loop_start_ns = ktime_get_ns();
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
				if (use_avx2) {
					avx2_blocks += results[r].avx2_blocks;
					avx2_equal_blocks += results[r].avx2_equal_blocks;
				}
				ms912x_damage_scan_process_row(
					ms912x, frame, next_snapshot, width, height,
					stride, y, &results[r], run_block_pixels,
					run_tolerance, baseline, rects, rect_capacity,
					rect_count,
					stats_enabled, false, &changed_pixels, &copy_ns,
					&loop_copy_ns, &rect_add_ns,
					&rect_add_calls);
			}
			if (use_avx2)
				MS912X_STAT_ADD(ms912x, software_scan_avx2_rows,
						count);
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
					ms912x, frame, next_snapshot, width, height,
					stride, y, &results[r], run_block_pixels,
					tolerance, baseline, rects, rect_capacity,
					rect_count,
					stats_enabled, true, &changed_pixels, &copy_ns,
					&loop_copy_ns, &rect_add_ns,
					&rect_add_calls);
			}
			if (use_avx2_relaxed)
				MS912X_STAT_ADD(ms912x,
						software_scan_avx2_relaxed_rows,
						count);
			row += count * stride;
		}
	} else if (use_memcmp) {
		for (y = frame_sequence % stride; y < height; y += stride) {
			struct ms912x_scan_row_result row_result;

			rows_scanned++;
			ms912x_scan_row_memcmp(
				baseline + y * MS912X_MAX_WIDTH,
				frame + y * MS912X_MAX_WIDTH, width, &row_result);
			MS912X_STAT_INC(ms912x, software_scan_memcmp_rows);
			memcmp_blocks += row_result.memcmp_blocks;
			memcmp_equal_blocks += row_result.memcmp_equal_blocks;
			if (row_result.equal_row)
				MS912X_STAT_INC(ms912x,
					software_scan_memcmp_equal_rows);
			ms912x_damage_scan_process_row(
				ms912x, frame, next_snapshot, width, height, stride,
				y, &row_result, run_block_pixels, run_tolerance, baseline,
				rects, rect_capacity, rect_count, stats_enabled, false,
				&changed_pixels, &copy_ns, &loop_copy_ns,
				&rect_add_ns, &rect_add_calls);
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
				ms912x, frame, next_snapshot, width, height, stride,
				y, &row_result, 1, tolerance, baseline, rects,
				rect_capacity, rect_count,
				stats_enabled, true,
				&changed_pixels, &copy_ns, &loop_copy_ns,
				&rect_add_ns, &rect_add_calls);
		}
	} else {
		for (y = frame_sequence % stride; y < height; y += stride) {
			struct ms912x_scan_row_result row_result;

			rows_scanned++;
			ms912x_scan_row_scalar(
				baseline + y * MS912X_MAX_WIDTH,
				frame + y * MS912X_MAX_WIDTH, width, &row_result);
			ms912x_damage_scan_process_row(
				ms912x, frame, next_snapshot, width, height, stride,
				y, &row_result, 1, 0, baseline, rects, rect_capacity,
				rect_count,
				stats_enabled, false,
				&changed_pixels, &copy_ns, &loop_copy_ns,
				&rect_add_ns, &rect_add_calls);
		}
	}
	if (stats_enabled) {
		scan_loop_ns = ktime_get_ns() - scan_loop_start_ns;
		compare_ns = scan_loop_ns > rect_add_ns + loop_copy_ns ?
			scan_loop_ns - rect_add_ns - loop_copy_ns : 0;
	}
	if (stats_enabled) {
		atomic64_add(memcmp_blocks,
			     &ms912x->stats.software_scan_memcmp_blocks);
		atomic64_add(memcmp_equal_blocks,
			     &ms912x->stats.software_scan_memcmp_equal_blocks);
		atomic64_add(avx2_blocks,
			     &ms912x->stats.software_scan_avx2_blocks);
		atomic64_add(avx2_equal_blocks,
			     &ms912x->stats.software_scan_avx2_equal_blocks);
	}
	MS912X_STAT_ADD(ms912x, software_scan_compared_pixels,
			(u64)width * rows_scanned);
	if (!changed_pixels) {
		MS912X_STAT_INC(ms912x, software_scan_no_change);
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
	for (i = 0; i < *rect_count; i++)
		MS912X_STAT_ADD(ms912x, software_scan_bbox_pixels,
				(u64)drm_rect_width(&(*rects)[i]) *
				drm_rect_height(&(*rects)[i]));
out_unlock:
	mutex_unlock(&ms912x->damage_scan_lock);
	if (stats_enabled) {
		ms912x_stats_record_duration(&ms912x->stats.software_scan_copy_ns,
					       &ms912x->stats.software_scan_copy_last_ns,
					       &ms912x->stats.software_scan_copy_max_ns,
					       copy_ns);
		ms912x_stats_record_duration(
			&ms912x->stats.software_scan_compare_ns,
			&ms912x->stats.software_scan_compare_last_ns,
			&ms912x->stats.software_scan_compare_max_ns,
			compare_ns);
		ms912x_stats_record_duration(
			&ms912x->stats.software_scan_rect_add_ns,
			&ms912x->stats.software_scan_rect_add_last_ns,
			&ms912x->stats.software_scan_rect_add_max_ns,
			rect_add_ns);
		atomic64_add(rect_add_calls,
			     &ms912x->stats.software_scan_rect_add_calls);
		ms912x_stats_record_max(
			&ms912x->stats.software_scan_rect_add_max_calls,
			rect_add_calls);
		atomic64_add(*rect_count, &ms912x->stats.software_scan_rectangles);
		ms912x_stats_record_max(
			&ms912x->stats.software_scan_rectangles_max, *rect_count);
		ms912x_stats_record_duration(&ms912x->stats.software_scan_ns,
					       &ms912x->stats.software_scan_last_ns,
					       &ms912x->stats.software_scan_max_ns,
					       ktime_get_ns() - start_ns);
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
