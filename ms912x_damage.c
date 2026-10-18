// SPDX-License-Identifier: GPL-2.0-only

#include <linux/iosys-map.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/string.h>

#include <drm/drm_framebuffer.h>
#include <drm/drm_rect.h>

#include "ms912x.h"

void ms912x_damage_scan_invalidate(struct ms912x_device *ms912x)
{
	if (!ms912x->damage_scan_snapshot)
		return;

	mutex_lock(&ms912x->damage_scan_lock);
	ms912x->damage_scan_valid = false;
	ms912x->damage_scan_x_probe_frames = 0;
	ms912x->damage_scan_x_stable = false;
	atomic_set(&ms912x->damage_scan_invalid, 1);
	mutex_unlock(&ms912x->damage_scan_lock);
}

/* Require four consecutive frame samples at all four quadrant positions. */
#define MS912X_X_STABLE_PROBES 4
#define MS912X_DAMAGE_SCAN_MEMCMP_BLOCK_BYTES 128

static bool ms912x_damage_scan_probe_snapshot_stability(
		const u32 *baseline, const u32 *frame,
		unsigned int width, unsigned int height)
{
	const unsigned int x[4] = {
		width / 4, (width * 3) / 4,
		width / 4, (width * 3) / 4,
	};
	const unsigned int y[4] = {
		height / 4, height / 4,
		(height * 3) / 4, (height * 3) / 4,
	};
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(x); i++) {
		u32 old = baseline[
			y[i] * MS912X_MAX_WIDTH + x[i]];
		u32 new = frame[y[i] * MS912X_MAX_WIDTH + x[i]];

		if ((old & 0xff000000) != (new & 0xff000000))
			return false;
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
					const struct drm_rect *b)
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
	return cost * 2 <= area;
}

static void ms912x_damage_scan_add_rect(struct drm_rect *rects,
					unsigned int *count,
					const struct drm_rect *rect,
					struct ms912x_device *ms912x,
					unsigned int width,
					unsigned int height)
{
	struct drm_rect merged = *rect;
	struct drm_rect bounds = DRM_RECT_INIT(0, 0, width, height);
	unsigned int i;

	if (*count == 1 && drm_rect_equals(&rects[0], &bounds))
		return;
	for (i = 0; i < *count;) {
		struct drm_rect *old = &rects[i];

		if (merged.x1 > old->x2 || old->x1 > merged.x2 ||
		    merged.y1 > old->y2 || old->y1 > merged.y2 ||
		    !ms912x_damage_rect_merge_within_stale_limit(old,
								 &merged)) {
			i++;
			continue;
		}
		merged.x1 = min(merged.x1, old->x1);
		merged.y1 = min(merged.y1, old->y1);
		merged.x2 = max(merged.x2, old->x2);
		merged.y2 = max(merged.y2, old->y2);
		memmove(old, old + 1, (*count - i - 1) * sizeof(*old));
		(*count)--;
		i = 0;
	}
	if (*count == MS912X_MAX_DAMAGE_RECTS) {
		u64 best_cost = U64_MAX;
		unsigned int first = 0, second = 0;

		/* Merge the least-cost pair instead of bounding every changed area. */
		for (i = 0; i <= *count; i++) {
			const struct drm_rect *a = i == *count ? rect : &rects[i];
			unsigned int j;

			for (j = i + 1; j <= *count; j++) {
				const struct drm_rect *b =
					j == *count ? rect : &rects[j];
				u64 cost;

				if (!ms912x_damage_rect_merge_within_stale_limit(a, b))
					continue;
				cost = ms912x_damage_rect_merge_cost(a, b);

				if (cost < best_cost) {
					best_cost = cost;
					first = i;
					second = j;
				}
			}
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
	rects[(*count)++] = merged;
}

int ms912x_damage_scan_find_snapshot_changes(
				   struct ms912x_device *ms912x,
				   const u32 *frame,
				   u64 frame_sequence,
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
	unsigned int y, x, stride, phase, rows_scanned = 0, i;
	u64 damage_area = 0, frame_area = (u64)width * height;
	unsigned int fast_mode;
	bool full_update, use_memcmp = false, memcmp_enabled;
	bool stats_enabled = ms912x_stats_enabled();
	int fast_generation;
	int ret = 1;

	if (width > MS912X_MAX_WIDTH || height > MS912X_MAX_HEIGHT)
		return -EINVAL;
	if (stats_enabled)
		start_ns = ktime_get_ns();
	MS912X_STAT_INC(ms912x, software_scan_frames);
	*rect_count = 0;
	mutex_lock(&ms912x->damage_scan_lock);

	fast_generation = atomic_read(&ms912x_parameters.damage_scan_fast_generation);
	if (fast_generation != READ_ONCE(ms912x->damage_scan_fast_generation)) {
		WRITE_ONCE(ms912x->damage_scan_fast_generation, fast_generation);
		atomic_set(&ms912x->damage_scan_invalid, 1);
	}
	fast_mode = clamp_t(unsigned int,
			    READ_ONCE(ms912x_parameters.damage_scan_fast), 0, 2);
	full_update = force_full || atomic_read(&ms912x->damage_scan_invalid) ||
		!ms912x->damage_scan_valid ||
		ms912x->damage_scan_width != width ||
		ms912x->damage_scan_height != height;
	if (full_update) {
		rects[0] = bounds;
		*rect_count = 1;
		changed_pixels = (u64)width * height;
		MS912X_STAT_ADD(ms912x, software_scan_pixels, changed_pixels);
		ms912x->damage_scan_x_probe_frames = 0;
		ms912x->damage_scan_x_stable = false;
		for (y = 0; y < height; y++)
			memcpy(next_snapshot +
			       y * MS912X_MAX_WIDTH,
			       frame + y * MS912X_MAX_WIDTH,
			       width * sizeof(*frame));
		goto out_damage;
	}

	memcmp_enabled = fast_mode != 0;
	if (!memcmp_enabled) {
		ms912x->damage_scan_x_probe_frames = 0;
		ms912x->damage_scan_x_stable = false;
	} else if (ms912x_damage_scan_probe_snapshot_stability(
			  baseline, frame, width, height)) {
		if (ms912x->damage_scan_x_probe_frames < MS912X_X_STABLE_PROBES)
			ms912x->damage_scan_x_probe_frames++;
		if (ms912x->damage_scan_x_probe_frames >=
		    MS912X_X_STABLE_PROBES)
			ms912x->damage_scan_x_stable = true;
	} else {
		ms912x->damage_scan_x_probe_frames = 0;
		ms912x->damage_scan_x_stable = false;
	}
	use_memcmp = memcmp_enabled && ms912x->damage_scan_x_stable;
	if (use_memcmp)
		MS912X_STAT_INC(ms912x, software_scan_memcmp_frames);
	for (y = 0; y < height; y++)
		memcpy(next_snapshot + y * MS912X_MAX_WIDTH,
		       baseline + y * MS912X_MAX_WIDTH,
		       width * sizeof(*frame));
	stride = fast_mode == 2 ? 2 : 1;
	phase = fast_mode == 2 ? frame_sequence & 1 : 0;

	for (y = phase; y < height; y += stride) {
		const u32 *old = baseline +
			y * MS912X_MAX_WIDTH;
		const u32 *new = frame + y * MS912X_MAX_WIDTH;

		rows_scanned++;
		if (use_memcmp) {
			MS912X_STAT_INC(ms912x, software_scan_memcmp_rows);
			if (!memcmp(old, new, width * sizeof(*new))) {
				MS912X_STAT_INC(ms912x,
					software_scan_memcmp_equal_rows);
				continue;
			}
		}

		x = 0;
		while (x < width) {
			unsigned int start;

			if (use_memcmp) {
				unsigned int block_pixels =
					MS912X_DAMAGE_SCAN_MEMCMP_BLOCK_BYTES /
					sizeof(*new);
				unsigned int block_end = min(width, x + block_pixels);

				MS912X_STAT_INC(ms912x,
					software_scan_memcmp_blocks);
				if (!memcmp(&old[x], &new[x],
					    (block_end - x) * sizeof(*new))) {
					MS912X_STAT_INC(ms912x,
						software_scan_memcmp_equal_blocks);
					x = block_end;
					continue;
				}
			}
			while (x < width && ((old[x] ^ new[x]) & 0x00ffffff) == 0)
				x++;
			if (x == width)
				break;
			start = x++;
			changed_pixels++;
			while (x < width && ((old[x] ^ new[x]) & 0x00ffffff)) {
				changed_pixels++;
				x++;
			}
			{
				struct drm_rect changed =
					DRM_RECT_INIT(start, y, x - start, 1);

				ms912x_damage_scan_add_rect(rects, rect_count,
							   &changed,
							   ms912x,
							   width, height);
			}
		}
		memcpy(next_snapshot + y * MS912X_MAX_WIDTH, new,
		       width * sizeof(*new));
	}
	MS912X_STAT_ADD(ms912x, software_scan_pixels,
			(u64)width * rows_scanned);
	for (i = 0; i < *rect_count; i++)
		damage_area += (u64)drm_rect_width(&rects[i]) *
			drm_rect_height(&rects[i]);
	if (changed_pixels &&
	    damage_area * 100 >=
	    frame_area * MS912X_FULL_UPDATE_THRESHOLD_PERCENT) {
		rects[0] = bounds;
		*rect_count = 1;
		for (y = 0; y < height; y++)
			memcpy(next_snapshot + y * MS912X_MAX_WIDTH,
			       frame + y * MS912X_MAX_WIDTH,
			       width * sizeof(*frame));
	}
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
	MS912X_STAT_ADD(ms912x, software_scan_changed_pixels, changed_pixels);
	for (i = 0; i < *rect_count; i++)
		MS912X_STAT_ADD(ms912x, software_scan_bbox_pixels,
				(u64)drm_rect_width(&rects[i]) *
				drm_rect_height(&rects[i]));
out_unlock:
	mutex_unlock(&ms912x->damage_scan_lock);
	if (stats_enabled)
		ms912x_stats_record_duration(&ms912x->stats.software_scan_ns,
					       &ms912x->stats.software_scan_last_ns,
					       &ms912x->stats.software_scan_max_ns,
					       ktime_get_ns() - start_ns);
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
