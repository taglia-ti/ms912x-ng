// SPDX-License-Identifier: GPL-2.0-only

#include <linux/iosys-map.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/string.h>

#include <drm/drm_framebuffer.h>
#include <drm/drm_rect.h>

#include "ms912x.h"
#include "ms912x_scan_methods.h"

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

static u64 ms912x_damage_rect_merge_cost(const struct drm_rect *a,
					const struct drm_rect *b)
{
	struct drm_rect merged;
	u64 area_a = (u64)drm_rect_width(a) * drm_rect_height(a);
	u64 area_b = (u64)drm_rect_width(b) * drm_rect_height(b);
	u64 intersection;
	u64 area_merged;
	int overlap_width = min(a->x2, b->x2) - max(a->x1, b->x1);
	int overlap_height = min(a->y2, b->y2) - max(a->y1, b->y1);

	merged.x1 = min(a->x1, b->x1);
	merged.y1 = min(a->y1, b->y1);
	merged.x2 = max(a->x2, b->x2);
	merged.y2 = max(a->y2, b->y2);
	area_merged = (u64)drm_rect_width(&merged) *
		drm_rect_height(&merged);
	intersection = overlap_width > 0 && overlap_height > 0 ?
		(u64)overlap_width * overlap_height : 0;
	return area_merged > area_a + area_b - intersection ?
		area_merged - area_a - area_b + intersection : 0;
}

static bool ms912x_damage_rect_merge_within_stale_limit(
					const struct drm_rect *a,
					const struct drm_rect *b,
					unsigned int stale_percent)
{
	struct drm_rect merged;
	u64 area, cost = ms912x_damage_rect_merge_cost(a, b);

	merged.x1 = min(a->x1, b->x1);
	merged.y1 = min(a->y1, b->y1);
	merged.x2 = max(a->x2, b->x2);
	merged.y2 = max(a->y2, b->y2);
	area = (u64)drm_rect_width(&merged) * drm_rect_height(&merged);
	if (area * 2 + MS912X_FRAME_OVERHEAD <
	    MS912X_USB_COALESCE_MIN_BYTES)
		return true;
	return !stale_percent || cost * 100 <= area * stale_percent;
}

static void ms912x_damage_scan_add_rect(struct drm_rect *rects,
					unsigned int *count,
					const struct drm_rect *rect,
					struct ms912x_device *ms912x,
					unsigned int width,
					unsigned int height)
{
	static const unsigned int stale_percentages[] = {
		MS912X_DEFAULT_STALE_AREA_PERCENT,
		MS912X_DEFAULT_STALE_AREA_PERCENT / 2,
		MS912X_DEFAULT_STALE_AREA_PERCENT / 4,
		MS912X_DEFAULT_STALE_AREA_PERCENT / 8,
		MS912X_DEFAULT_STALE_AREA_PERCENT / 16,
		1, 0,
	};
	unsigned int i, threshold;

	if (*count < MS912X_MAX_DAMAGE_RECTS) {
		rects[(*count)++] = *rect;
		return;
	}
	{
		u64 best_cost = U64_MAX;
		unsigned int first = 0, second = 0;

		/* Relax the stale-area budget only when the scan list overflows. */
		for (threshold = 0;
		     threshold < ARRAY_SIZE(stale_percentages); threshold++) {
			best_cost = U64_MAX;
			for (i = 0; i <= *count; i++) {
				const struct drm_rect *a = i == *count ? rect :
					&rects[i];
				unsigned int j;

				for (j = i + 1; j <= *count; j++) {
					const struct drm_rect *b = j == *count ? rect :
						&rects[j];
					u64 cost;

					if (!ms912x_damage_rect_merge_within_stale_limit(
						    a, b,
						    stale_percentages[threshold]))
						continue;
					cost = ms912x_damage_rect_merge_cost(a, b);
					if (cost < best_cost) {
						best_cost = cost;
						first = i;
						second = j;
					}
				}
			}
			if (best_cost != U64_MAX)
				break;
		}
		if (best_cost == U64_MAX) {
			rects[0] = DRM_RECT_INIT(0, 0, width, height);
			*count = 1;
			MS912X_STAT_INC(ms912x,
					scan_rect_overflow_full_updates);
			return;
		}
		if (second == *count) {
			rects[first].x1 = min(rects[first].x1, rect->x1);
			rects[first].y1 = min(rects[first].y1, rect->y1);
			rects[first].x2 = max(rects[first].x2, rect->x2);
			rects[first].y2 = max(rects[first].y2, rect->y2);
			return;
		}
		rects[first].x1 = min(rects[first].x1, rects[second].x1);
		rects[first].y1 = min(rects[first].y1, rects[second].y1);
		rects[first].x2 = max(rects[first].x2, rects[second].x2);
		rects[first].y2 = max(rects[first].y2, rects[second].y2);
		memmove(&rects[second], &rects[second + 1],
			(*count - second - 1) * sizeof(*rects));
		(*count)--;
	}
	rects[(*count)++] = *rect;
}

static __always_inline void
ms912x_damage_scan_process_row(struct ms912x_device *ms912x,
			       const u32 *frame, u32 *next_snapshot,
			       unsigned int width, unsigned int height,
			       unsigned int stride, unsigned int y,
			       const struct ms912x_scan_row_result *row_result,
			       struct drm_rect *rects, unsigned int *rect_count,
			       bool stats_enabled, bool selective_baseline,
			       u64 *changed_pixels,
			       u64 *copy_ns, u64 *loop_copy_ns,
			       u64 *rect_merge_ns,
			       unsigned int *rect_add_calls)
{
	unsigned int copy_y, row_y1;
	u64 stage_start_ns;

	*changed_pixels += row_result->changed_pixels;
	if (row_result->changed_pixels) {
		struct drm_rect changed;

		row_y1 = y >= stride - 1 ? y - stride + 1 : 0;
		changed = DRM_RECT_INIT(row_result->min_x, row_y1,
				row_result->max_x + 1 - row_result->min_x,
				y + 1 - row_y1);
		if (stats_enabled)
			stage_start_ns = ktime_get_ns();
		ms912x_damage_scan_add_rect(rects, rect_count, &changed,
					   ms912x, width, height);
		if (stats_enabled) {
			*rect_merge_ns += ktime_get_ns() - stage_start_ns;
			(*rect_add_calls)++;
		}

		if (!selective_baseline) {
			if (stats_enabled)
				stage_start_ns = ktime_get_ns();
			for (copy_y = row_y1; copy_y < y; copy_y++)
				memcpy(next_snapshot + copy_y * MS912X_MAX_WIDTH +
				       row_result->min_x,
				       frame + copy_y * MS912X_MAX_WIDTH +
				       row_result->min_x,
				       (row_result->max_x + 1 -
					row_result->min_x) * sizeof(*frame));
			if (stats_enabled) {
				u64 backfill_copy_ns =
					ktime_get_ns() - stage_start_ns;

				*copy_ns += backfill_copy_ns;
				*loop_copy_ns += backfill_copy_ns;
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
		READ_ONCE(ms912x_parameters.damage_scan_tolerance_override) ?
		READ_ONCE(ms912x_parameters.damage_scan_tolerance) :
		READ_ONCE(ms912x->damage_scan_tolerance_default);

	if (method == MS912X_DAMAGE_SCAN_METHOD_AUTO)
		method = ms912x_damage_scan_select_auto(tolerance != 0, true);
	if (method == MS912X_DAMAGE_SCAN_AVX2 ||
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
	if (method == MS912X_DAMAGE_SCAN_AVX2)
		ms912x_scan_rows_avx2_blocks(old, new, width, row_stride,
					     row_count, results);
	else
		ms912x_scan_rows_sse2_blocks(old, new, width, row_stride,
					     row_count, results);
#elif defined(CONFIG_ARM64)
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
	else
		ms912x_scan_rows_sse2_relaxed(old, new, next, width, row_stride,
					      row_count, tolerance, results);
#elif defined(CONFIG_ARM64)
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
				   struct drm_rect *rects,
				   unsigned int *rect_count)
{
	struct drm_rect bounds = DRM_RECT_INIT(0, 0, width, height);
	u64 start_ns = 0, changed_pixels = 0;
	u64 memcmp_blocks = 0, memcmp_equal_blocks = 0;
	u64 avx2_blocks = 0, avx2_equal_blocks = 0;
	u64 copy_ns = 0, compare_ns = 0, rect_merge_ns = 0;
	u64 loop_copy_ns = 0, scan_loop_start_ns = 0, scan_loop_ns;
	u64 stage_start_ns;
	unsigned int rect_add_calls = 0;
	unsigned int y, stride, rows_scanned = 0, i;
	unsigned int config_generation;
	unsigned int tolerance;
	enum ms912x_damage_scan_method method;
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
	tolerance = min_t(unsigned int,
			  READ_ONCE(ms912x_parameters.damage_scan_tolerance_override) ?
			  READ_ONCE(ms912x_parameters.damage_scan_tolerance) :
			  READ_ONCE(ms912x->damage_scan_tolerance_default), 255);
	stride = clamp_t(unsigned int,
			 READ_ONCE(ms912x_parameters.damage_scan_stride_override) ?
			 READ_ONCE(ms912x_parameters.damage_scan_stride) :
			 READ_ONCE(ms912x->damage_scan_stride_default), 1, 16);
	full_update = force_full || atomic_read(&ms912x->damage_scan_invalid) ||
		!ms912x->damage_scan_valid ||
		ms912x->damage_scan_width != width ||
		ms912x->damage_scan_height != height;
	if (full_update) {
		rects[0] = bounds;
		*rect_count = 1;
		changed_pixels = (u64)width * height;
		if ((method == MS912X_DAMAGE_SCAN_METHOD_AUTO ||
		     method == MS912X_DAMAGE_SCAN_MEMCMP ||
		     ((method == MS912X_DAMAGE_SCAN_AVX2 ||
		       method == MS912X_DAMAGE_SCAN_NEON ||
		       method == MS912X_DAMAGE_SCAN_SSE2) && !tolerance)) &&
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
		     method == MS912X_DAMAGE_SCAN_SSE2) &&
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
		    method == MS912X_DAMAGE_SCAN_SSE2) && !tolerance &&
		ms912x->damage_scan_x_probe_done &&
		ms912x->damage_scan_x_stable &&
		ms912x_damage_scan_simd_method_available(method);
	use_avx2 = method == MS912X_DAMAGE_SCAN_AVX2 && use_simd &&
		ms912x_scan_avx2_available();
	use_relaxed = method != MS912X_DAMAGE_SCAN_MEMCMP && tolerance;
	use_avx2_relaxed = method == MS912X_DAMAGE_SCAN_AVX2 && tolerance &&
		ms912x_scan_avx2_available();
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
					stride, y, &results[r], rects, rect_count,
					stats_enabled, false, &changed_pixels, &copy_ns,
					&loop_copy_ns, &rect_merge_ns,
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
		    method == MS912X_DAMAGE_SCAN_SSE2) &&
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
					stride, y, &results[r], rects, rect_count,
					stats_enabled, true, &changed_pixels, &copy_ns,
					&loop_copy_ns, &rect_merge_ns,
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
				y, &row_result, rects, rect_count, stats_enabled, false,
				&changed_pixels, &copy_ns, &loop_copy_ns,
				&rect_merge_ns, &rect_add_calls);
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
				y, &row_result, rects, rect_count, stats_enabled, true,
				&changed_pixels, &copy_ns, &loop_copy_ns,
				&rect_merge_ns, &rect_add_calls);
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
				y, &row_result, rects, rect_count, stats_enabled, false,
				&changed_pixels, &copy_ns, &loop_copy_ns,
				&rect_merge_ns, &rect_add_calls);
		}
	}
	if (stats_enabled) {
		scan_loop_ns = ktime_get_ns() - scan_loop_start_ns;
		compare_ns = scan_loop_ns > rect_merge_ns + loop_copy_ns ?
			scan_loop_ns - rect_merge_ns - loop_copy_ns : 0;
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
	if (*rect_count > 1) {
		struct drm_rect merged = rects[0];

		for (i = 1; i < *rect_count; i++) {
			merged.x1 = min(merged.x1, rects[i].x1);
			merged.y1 = min(merged.y1, rects[i].y1);
			merged.x2 = max(merged.x2, rects[i].x2);
			merged.y2 = max(merged.y2, rects[i].y2);
		}
		rects[0] = merged;
		*rect_count = 1;
	}
	ms912x->damage_scan_width = width;
	ms912x->damage_scan_height = height;
	ms912x->damage_scan_valid = true;
	atomic_set(&ms912x->damage_scan_invalid, 0);
	/* A full update seeds the baseline; it does not measure pixel changes. */
	if (!full_update)
		MS912X_STAT_ADD(ms912x, software_scan_changed_pixels,
				changed_pixels);
	for (i = 0; i < *rect_count; i++)
		MS912X_STAT_ADD(ms912x, software_scan_bbox_pixels,
				(u64)drm_rect_width(&rects[i]) *
				drm_rect_height(&rects[i]));
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
			&ms912x->stats.software_scan_rect_merge_ns,
			&ms912x->stats.software_scan_rect_merge_last_ns,
			&ms912x->stats.software_scan_rect_merge_max_ns,
			rect_merge_ns);
		atomic64_add(rect_add_calls,
			     &ms912x->stats.software_scan_rect_add_calls);
		ms912x_stats_record_duration(&ms912x->stats.software_scan_ns,
					       &ms912x->stats.software_scan_last_ns,
					       &ms912x->stats.software_scan_max_ns,
					       ktime_get_ns() - start_ns);
	}
	return ret;
}

void ms912x_damage_scan_update_snapshot(struct ms912x_device *ms912x,
					  struct drm_framebuffer *fb,
					  const struct iosys_map *map,
					  const struct drm_rect *rect)
{
	unsigned int y;

	for (y = rect->y1; y < rect->y2; y++) {
		u32 *snapshot = ms912x->damage_scan_snapshot +
			y * MS912X_MAX_WIDTH + rect->x1;
		size_t width = drm_rect_width(rect);

		iosys_map_memcpy_from(snapshot, map,
				     y * fb->pitches[0] + rect->x1 * 4,
				     width * sizeof(*snapshot));
	}
	ms912x->damage_scan_width = fb->width;
	ms912x->damage_scan_height = fb->height;
	ms912x->damage_scan_valid = true;
	atomic_set(&ms912x->damage_scan_invalid, 0);
}
