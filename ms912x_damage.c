// SPDX-License-Identifier: GPL-2.0-only

#include <linux/iosys-map.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/string.h>

#include <drm/drm_framebuffer.h>
#include <drm/drm_format_helper.h>
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

static bool ms912x_damage_scan_probe_padding_stability(struct ms912x_device *ms912x,
					       struct drm_framebuffer *fb,
					       const struct iosys_map *map)
{
	const unsigned int x[4] = {
		fb->width / 4, (fb->width * 3) / 4,
		fb->width / 4, (fb->width * 3) / 4,
	};
	const unsigned int y[4] = {
		fb->height / 4, fb->height / 4,
		(fb->height * 3) / 4, (fb->height * 3) / 4,
	};
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(x); i++) {
		__le32 sample;
		u32 previous = ms912x->damage_scan_snapshot[
			y[i] * MS912X_MAX_WIDTH + x[i]];
		size_t offset = (size_t)y[i] * fb->pitches[0] + x[i] * 4;

		iosys_map_memcpy_from(&sample, map, offset, sizeof(sample));
		if ((le32_to_cpu(sample) & 0xff000000) !=
		    (le32_to_cpu((__force __le32)previous) & 0xff000000))
			return false;
	}

	return true;
}

/* Compare the current XRGB image against the last queued image. */
int ms912x_damage_scan_find_changes(struct ms912x_device *ms912x,
				   struct drm_framebuffer *fb,
				   const struct iosys_map *map,
				   struct drm_format_conv_state *fmtcnv_state,
				   bool force_full, struct drm_rect *damage)
{
	struct drm_rect bounds = DRM_RECT_INIT(0, 0, fb->width, fb->height);
	__le32 *scanline;
	u64 start_ns = 0;
	u64 changed_pixels = 0;
	unsigned int y, stride, phase, rows_scanned = 0;
	int min_x = fb->width, min_y = fb->height;
	int max_x = -1, max_y = -1;
	bool full_update, use_memcmp = false;
	bool memcmp_enabled;
	bool stats_enabled = ms912x_stats_enabled();
	int memcmp_generation, stride_generation;
	int ret = 1;

	if (stats_enabled)
		start_ns = ktime_get_ns();
	MS912X_STAT_INC(ms912x, software_scan_frames);

	scanline = drm_format_conv_state_reserve(fmtcnv_state,
						 fb->width * sizeof(*scanline),
						 GFP_KERNEL);
	if (!scanline) {
		ret = -ENOMEM;
		goto out_time;
	}

	stride_generation = atomic_read(&ms912x_parameters.damage_scan_stride_generation);
	if (stride_generation != READ_ONCE(ms912x->damage_scan_stride_generation)) {
		WRITE_ONCE(ms912x->damage_scan_stride_generation,
			   stride_generation);
		WRITE_ONCE(ms912x->damage_scan_stride_phase, 0);
		atomic_set(&ms912x->damage_scan_invalid, 1);
	}

	full_update = force_full || atomic_read(&ms912x->damage_scan_invalid) ||
		!ms912x->damage_scan_valid ||
		ms912x->damage_scan_width != fb->width ||
		ms912x->damage_scan_height != fb->height;

	if (full_update) {
		*damage = bounds;
		changed_pixels = (u64)fb->width * fb->height;
		MS912X_STAT_ADD(ms912x, software_scan_pixels, changed_pixels);
		ms912x->damage_scan_x_probe_frames = 0;
		ms912x->damage_scan_x_stable = false;
		WRITE_ONCE(ms912x->damage_scan_stride_phase, 0);
		for (y = 0; y < fb->height; y++) {
			u32 *snapshot = ms912x->damage_scan_snapshot +
				y * MS912X_MAX_WIDTH;

			iosys_map_memcpy_from(snapshot, map,
					     y * fb->pitches[0],
					     fb->width * sizeof(*snapshot));
		}
		goto out_damage;
	}

	memcmp_enabled = READ_ONCE(ms912x_parameters.damage_scan_memcmp);
	memcmp_generation = atomic_read(&ms912x_parameters.damage_scan_memcmp_generation);
	if (memcmp_generation != READ_ONCE(ms912x->damage_scan_memcmp_generation)) {
		WRITE_ONCE(ms912x->damage_scan_memcmp_generation,
			   memcmp_generation);
		ms912x->damage_scan_x_probe_frames = 0;
		ms912x->damage_scan_x_stable = false;
	}
	if (!memcmp_enabled) {
		ms912x->damage_scan_x_probe_frames = 0;
		ms912x->damage_scan_x_stable = false;
	} else if (ms912x_damage_scan_probe_padding_stability(ms912x, fb, map)) {
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
	stride = clamp_t(unsigned int, READ_ONCE(ms912x_parameters.damage_scan_stride), 1, 16);
	phase = READ_ONCE(ms912x->damage_scan_stride_phase);
	if (phase >= stride)
		phase = 0;

	for (y = phase; y < fb->height; y += stride) {
		u32 *snapshot = ms912x->damage_scan_snapshot +
			y * MS912X_MAX_WIDTH;
		unsigned int x = 0;
		unsigned int block_end;
		int row_min_x = fb->width;
		int row_max_x = -1;

		rows_scanned++;

		iosys_map_memcpy_from(scanline, map, y * fb->pitches[0],
				     fb->width * sizeof(*scanline));
		if (use_memcmp) {
			MS912X_STAT_INC(ms912x, software_scan_memcmp_rows);
			/* Only byte-identical rows are skipped; others get RGB checks. */
			if (!memcmp(scanline, snapshot,
				    fb->width * sizeof(*scanline))) {
				MS912X_STAT_INC(ms912x,
					software_scan_memcmp_equal_rows);
				continue;
			}
		}

		while (x < fb->width) {
			if (use_memcmp) {
				unsigned int block_pixels =
					MS912X_DAMAGE_SCAN_MEMCMP_BLOCK_BYTES /
					sizeof(*scanline);
				size_t block_bytes;

				block_end = min_t(unsigned int, fb->width,
						  x + block_pixels);
				block_bytes = (block_end - x) * sizeof(*scanline);
				MS912X_STAT_INC(ms912x,
					software_scan_memcmp_blocks);
				if (!memcmp(&scanline[x], &snapshot[x],
					    block_bytes)) {
					MS912X_STAT_INC(ms912x,
						software_scan_memcmp_equal_blocks);
					x = block_end;
					continue;
				}
			} else {
				block_end = fb->width;
			}

			for (; x < block_end; x++) {
				u32 pixel = le32_to_cpu(scanline[x]) & 0x00ffffff;
				u32 previous =
					le32_to_cpu((__force __le32)snapshot[x]) &
					0x00ffffff;

				if (pixel == previous)
					continue;

				changed_pixels++;
				row_min_x = min_t(unsigned int, row_min_x, x);
				row_max_x = x;
			}
		}

		if (row_max_x >= 0) {
			min_x = min(min_x, row_min_x);
			max_x = max(max_x, row_max_x);
			min_y = min_t(int, min_y, y);
			max_y = y;
		}
		memcpy(snapshot, scanline, fb->width * sizeof(*scanline));
	}
	MS912X_STAT_ADD(ms912x, software_scan_pixels,
			(u64)fb->width * rows_scanned);
	WRITE_ONCE(ms912x->damage_scan_stride_phase, (phase + 1) % stride);

	if (!changed_pixels) {
		MS912X_STAT_INC(ms912x, software_scan_no_change);
		ret = 0;
		goto out_time;
	}

	damage->x1 = min_x;
	damage->y1 = min_y;
	damage->x2 = max_x + 1;
	damage->y2 = max_y + 1;

	out_damage:
	ms912x->damage_scan_width = fb->width;
	ms912x->damage_scan_height = fb->height;
	MS912X_STAT_ADD(ms912x, software_scan_changed_pixels, changed_pixels);
	MS912X_STAT_ADD(ms912x, software_scan_bbox_pixels,
			(u64)drm_rect_width(damage) * drm_rect_height(damage));
out_time:
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
