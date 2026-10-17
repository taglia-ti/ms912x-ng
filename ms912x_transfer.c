// SPDX-License-Identifier: GPL-2.0-only

#include <linux/align.h>
#include <linux/container_of.h>
#include <linux/dma-direction.h>
#include <linux/iosys-map.h>
#include <linux/jiffies.h>
#include <linux/ktime.h>
#include <linux/math64.h>
#include <linux/minmax.h>
#include <linux/string.h>
#include <linux/workqueue.h>

#include <drm/drm_drv.h>
#include <drm/drm_format_helper.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_print.h>

#include "ms912x.h"

static void ms912x_rect_union(struct drm_rect *dest,
			      const struct drm_rect *r1,
			      const struct drm_rect *r2)
{
	dest->x1 = min(r1->x1, r2->x1);
	dest->y1 = min(r1->y1, r2->y1);
	dest->x2 = max(r1->x2, r2->x2);
	dest->y2 = max(r1->y2, r2->y2);
}

static void ms912x_rect_align_uyvy(struct drm_rect *rect,
				     unsigned int fb_width)
{
	int x = ALIGN_DOWN(rect->x1, 2);
	int x2 = min_t(int, ALIGN(rect->x2, 2), fb_width);

	rect->x1 = x;
	rect->x2 = x2;
}

#define MS912X_CONVERSION_ESTIMATED_PIXELS_PER_SEC (100ULL * 1000 * 1000)
#define MS912X_USB_COALESCE_RATE_PERCENT 85

static unsigned long
ms912x_transfer_coalesce_delay(struct ms912x_device *ms912x,
			     const struct drm_rect *rect)
{
	u64 window_ns = U64_MAX, now_ns;
	u64 conversion_ns, pixels;
	unsigned int i;
	bool active_request_found = false;

	now_ns = ktime_get_ns();

	for (i = 0; i < ARRAY_SIZE(ms912x->requests); i++) {
		const struct ms912x_usb_request *active =
			&ms912x->requests[i];
		u64 expected_ns, elapsed_ns, remaining_ns;
		u64 rate_bps = ms912x->usb_speed_mean_bps ?
			ms912x->usb_speed_mean_bps :
			MS912X_USB_INITIAL_RATE_BPS;

		if (active->state != MS912X_REQUEST_USB_ACTIVE)
			continue;

		active_request_found = true;
		/* Leave headroom for transfer-rate variance and submit overhead. */
		rate_bps = div64_u64(rate_bps *
				     MS912X_USB_COALESCE_RATE_PERCENT, 100);
		expected_ns = DIV_ROUND_UP_ULL(
			(u64)active->transfer_len * NSEC_PER_SEC,
			rate_bps);
		elapsed_ns = now_ns - active->usb_started_ns;
		remaining_ns = expected_ns > elapsed_ns ?
			expected_ns - elapsed_ns : 0;
		window_ns = min(window_ns, remaining_ns);
	}

	if (!active_request_found)
		return 0;

	pixels = (u64)drm_rect_width(rect) * drm_rect_height(rect);
	conversion_ns = DIV_ROUND_UP_ULL(
		pixels * NSEC_PER_SEC,
		MS912X_CONVERSION_ESTIMATED_PIXELS_PER_SEC);
	window_ns = window_ns > conversion_ns ?
		window_ns - conversion_ns : 0;

	/* Leave a conversion-time margin so conversion overlaps USB completion. */
	return window_ns * HZ / NSEC_PER_SEC;
}

static bool ms912x_transfer_has_idle_request(struct ms912x_device *ms912x)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(ms912x->requests); i++) {
		if (ms912x->requests[i].state == MS912X_REQUEST_FREE)
			return true;
	}

	return false;
}

static unsigned long
ms912x_transfer_pending_delay(struct ms912x_device *ms912x)
{
	if (ms912x->pending_force_full || !ms912x->pending_deadline ||
	    time_after_eq(jiffies, ms912x->pending_deadline))
		return 0;

	return ms912x->pending_deadline - jiffies;
}

/* Caller holds transfer_lock. */
void ms912x_schedule_conversion_locked(struct ms912x_device *ms912x)
{
	if (!ms912x->transfer_stopping && ms912x->pending_valid &&
	    !ms912x->conversion_busy &&
	    ms912x_transfer_has_idle_request(ms912x))
		mod_delayed_work(ms912x->conversion_workqueue,
				 &ms912x->conversion_work,
				 ms912x_transfer_pending_delay(ms912x));
}

static void ms912x_snapshot_capture_damage(struct ms912x_device *ms912x,
				      struct drm_framebuffer *fb,
				      const struct iosys_map *map,
				      const struct drm_rect *rect)
{
	unsigned int y;
	size_t width = drm_rect_width(rect);

	for (y = rect->y1; y < rect->y2; y++) {
		u32 *dest = ms912x->latest_xrgb +
			y * MS912X_MAX_WIDTH + rect->x1;
		size_t offset = y * fb->pitches[0] + rect->x1 * sizeof(*dest);

		iosys_map_memcpy_from(dest, map, offset, width * sizeof(*dest));
	}
}

static void ms912x_snapshot_stage_conversion(struct ms912x_device *ms912x,
					     const struct drm_rect *rect)
{
	unsigned int y;
	size_t width = drm_rect_width(rect);

	for (y = rect->y1; y < rect->y2; y++) {
		u32 *src = ms912x->latest_xrgb +
			y * MS912X_MAX_WIDTH + rect->x1;
		u32 *dest = ms912x->conversion_xrgb +
			y * MS912X_MAX_WIDTH + rect->x1;

		memcpy(dest, src, width * sizeof(*dest));
	}
}

static void ms912x_conversion_worker(struct work_struct *work)
{
	struct delayed_work *delayed_work = to_delayed_work(work);
	struct ms912x_device *ms912x =
		container_of(delayed_work, struct ms912x_device, conversion_work);
	struct ms912x_usb_request *request = NULL;
	struct drm_rect damage_rect, rect;
	u64 conversion_start_ns = 0, conversion_ns, coalesce_wait_ns;
	u64 snapshot_copy_start_ns = 0;
	u64 queued_at_ns;
	u32 generation, idle_replay_generation;
	unsigned int width, height, i;
	unsigned int idle_replay_tile;
	bool idle_replay;
	bool stats_enabled;
	int idx, ret;

	if (!drm_dev_enter(&ms912x->drm, &idx))
		return;

	mutex_lock(&ms912x->transfer_lock);
	if (ms912x->transfer_stopping || !ms912x->pending_valid) {
		mutex_unlock(&ms912x->transfer_lock);
		drm_dev_exit(idx);
		return;
	}
	for (i = 0; i < ARRAY_SIZE(ms912x->requests); i++) {
		if (ms912x->requests[i].state == MS912X_REQUEST_FREE) {
			request = &ms912x->requests[i];
			break;
		}
	}
	if (!request) {
		mutex_unlock(&ms912x->transfer_lock);
		drm_dev_exit(idx);
		return;
	}

	stats_enabled = ms912x_stats_enabled();
	ms912x->conversion_busy = true;
	request->state = MS912X_REQUEST_CONVERTING;
	damage_rect = ms912x->pending_damage_rect;
	rect = ms912x->pending_rect;
	width = ms912x->pending_width;
	height = ms912x->pending_height;
	generation = ms912x->frame_generation;
	idle_replay = ms912x->pending_idle_replay;
	idle_replay_tile = ms912x->pending_idle_replay_tile;
	idle_replay_generation = ms912x->idle_replay_generation;
	queued_at_ns = ms912x->pending_started_ns;
	if (ms912x->last_transfer_valid &&
	    ms912x->last_transfer_generation == generation)
		ms912x_rect_union(&rect, &rect,
				  &ms912x->last_transfer_rect);
	ms912x_rect_align_uyvy(&rect, width);
	if (stats_enabled)
		snapshot_copy_start_ns = ktime_get_ns();
	ms912x_snapshot_stage_conversion(ms912x, &rect);
	if (stats_enabled)
		atomic64_add(ktime_get_ns() - snapshot_copy_start_ns,
			     &ms912x->stats.async_snapshot_copy_ns);
	ms912x->pending_valid = false;
	ms912x->pending_force_full = false;
	ms912x->pending_idle_replay = false;
	ms912x->pending_deadline = 0;
	ms912x->pending_started_ns = 0;
	mutex_unlock(&ms912x->transfer_lock);

	if (stats_enabled && queued_at_ns) {
		coalesce_wait_ns = ktime_get_ns() - queued_at_ns;
		ms912x_stats_record_duration(&ms912x->stats.damage_coalesce_ns,
				       &ms912x->stats.damage_coalesce_last_ns,
				       &ms912x->stats.damage_coalesce_max_ns,
				       coalesce_wait_ns);
	}
	if (stats_enabled)
		conversion_start_ns = ktime_get_ns();
	MS912X_STAT_INC(ms912x, conversion_attempts);
	ret = ms912x_convert_xrgb8888_to_uyvy(
		request->transfer_buffer,
		(const __le32 *)ms912x->conversion_xrgb, ms912x, width, height,
		&rect);
	if (stats_enabled) {
		conversion_ns = ktime_get_ns() - conversion_start_ns;
		ms912x_stats_record_duration(&ms912x->stats.conversion_ns,
				       &ms912x->stats.conversion_last_ns,
				       &ms912x->stats.conversion_max_ns,
				       conversion_ns);
	}

	mutex_lock(&ms912x->transfer_lock);
	ms912x->conversion_busy = false;
	if (ret || ms912x->transfer_stopping ||
	    generation != ms912x->frame_generation ||
	    (idle_replay && idle_replay_generation !=
		    ms912x->idle_replay_generation)) {
		request->state = MS912X_REQUEST_FREE;
		ms912x_schedule_conversion_locked(ms912x);
		mutex_unlock(&ms912x->transfer_lock);
		if (ret) {
			ms912x_damage_scan_invalidate(ms912x);
			drm_err_ratelimited(&ms912x->drm,
					    "frame conversion failed: %d\n", ret);
		}
		drm_dev_exit(idx);
		return;
	}

	request->transfer_len = drm_rect_width(&rect) * 2 *
		drm_rect_height(&rect) + MS912X_FRAME_OVERHEAD;
	request->queued_at_ns = ktime_get_ns();
	request->generation = generation;
	request->idle_replay = idle_replay;
	request->idle_replay_tile = idle_replay_tile;
	request->idle_replay_generation = idle_replay_generation;
	request->state = MS912X_REQUEST_USB_QUEUED;
	MS912X_STAT_INC(ms912x, conversion_frames);
	MS912X_STAT_SET(ms912x, conversion_last_pixels,
			drm_rect_width(&rect) * drm_rect_height(&rect));
	MS912X_STAT_ADD(ms912x, converted_pixels,
			drm_rect_width(&rect) * drm_rect_height(&rect));
	MS912X_STAT_ADD(ms912x, queued_bytes, request->transfer_len);
	if (WARN_ON_ONCE(!queue_work(ms912x->workqueue, &request->work))) {
		request->state = MS912X_REQUEST_FREE;
	} else {
		ms912x->last_transfer_rect = damage_rect;
		ms912x_rect_align_uyvy(&ms912x->last_transfer_rect, width);
		ms912x->last_transfer_generation = generation;
		ms912x->last_transfer_valid = true;
	}
	ms912x_schedule_conversion_locked(ms912x);
	mutex_unlock(&ms912x->transfer_lock);
	drm_dev_exit(idx);
}

void ms912x_transfer_work_init(struct ms912x_device *ms912x)
{
	INIT_DELAYED_WORK(&ms912x->conversion_work, ms912x_conversion_worker);
	INIT_DELAYED_WORK(&ms912x->idle_work, ms912x_idle_work);
}

static bool ms912x_transfer_requests_busy(struct ms912x_device *ms912x)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(ms912x->requests); i++) {
		if (ms912x->requests[i].state != MS912X_REQUEST_FREE)
			return true;
	}

	return false;
}

void ms912x_idle_work(struct work_struct *work)
{
	struct delayed_work *delayed_work = to_delayed_work(work);
	struct ms912x_device *ms912x =
		container_of(delayed_work, struct ms912x_device, idle_work);
	unsigned long refresh_delay = msecs_to_jiffies(MS912X_IDLE_REFRESH_MS);
	unsigned long idle_deadline;
	unsigned int tile, row, column;
	int idx;

	if (!drm_dev_enter(&ms912x->drm, &idx))
		return;

	mutex_lock(&ms912x->transfer_lock);
	if (ms912x->transfer_stopping || !ms912x->frame_snapshot_valid)
		goto unlock;

	idle_deadline = ms912x->last_frame_activity + refresh_delay;
	if (time_before(jiffies, idle_deadline)) {
		refresh_delay = idle_deadline - jiffies;
	} else if (!ms912x->pending_valid && !ms912x->conversion_busy &&
		   !ms912x_transfer_requests_busy(ms912x)) {
		/* Refresh one tile; eight ticks cover the cached image. */
		tile = ms912x->idle_replay_tile;
		row = tile / MS912X_IDLE_REPLAY_COLUMNS;
		column = tile < MS912X_IDLE_REPLAY_COLUMNS ?
			tile : MS912X_IDLE_REPLAY_TILES - 1 - tile;
		ms912x->pending_rect.x1 =
			(ms912x->frame_snapshot_width * column /
			 MS912X_IDLE_REPLAY_COLUMNS) & ~1U;
		ms912x->pending_rect.x2 = column + 1 ==
			MS912X_IDLE_REPLAY_COLUMNS ?
			ms912x->frame_snapshot_width :
			(ms912x->frame_snapshot_width * (column + 1) /
			 MS912X_IDLE_REPLAY_COLUMNS) & ~1U;
		ms912x->pending_rect.y1 =
			ms912x->frame_snapshot_height * row / 2;
		ms912x->pending_rect.y2 = row ?
			ms912x->frame_snapshot_height :
			ms912x->frame_snapshot_height / 2;
		ms912x->pending_damage_rect = ms912x->pending_rect;
		ms912x->pending_width = ms912x->frame_snapshot_width;
		ms912x->pending_height = ms912x->frame_snapshot_height;
		ms912x->pending_started_ns = ktime_get_ns();
		ms912x->pending_deadline = 0;
		ms912x->pending_force_full = true;
		ms912x->pending_idle_replay = true;
		ms912x->pending_idle_replay_tile = tile;
		ms912x->pending_valid = true;
		ms912x->last_frame_activity = jiffies;
		ms912x_schedule_conversion_locked(ms912x);
	}

	if (!ms912x->transfer_stopping && ms912x->frame_snapshot_valid)
		mod_delayed_work(system_percpu_wq, &ms912x->idle_work,
				 refresh_delay);
unlock:
	mutex_unlock(&ms912x->transfer_lock);
	drm_dev_exit(idx);
}

int ms912x_framebuffer_submit(struct drm_framebuffer *fb, const struct iosys_map *map,
			struct drm_format_conv_state *fmtcnv_state,
			struct drm_rect *rect, struct drm_rect *damage_rect,
			bool software_scan, bool force_full_scan)
{
	int ret = 0, idx, scan_ret;
	struct ms912x_device *ms912x = to_ms912x(fb->dev);
	struct drm_device *drm = &ms912x->drm;
	bool had_pending;
	bool cpu_access = false, scan_lock_held = false;
	bool snapshot_enabled = ms912x->damage_scan_snapshot;
	bool full_update;
	bool stats_enabled;
	u64 snapshot_copy_start_ns;

	if (!drm_dev_enter(drm, &idx))
		return -ENODEV;

	MS912X_STAT_INC(ms912x, updates);
	stats_enabled = ms912x_stats_enabled();

	mutex_lock(&ms912x->transfer_lock);
	if (ms912x->transfer_stopping) {
		ret = -ENODEV;
		goto dev_exit;
	}

	ret = drm_gem_fb_begin_cpu_access(fb, DMA_FROM_DEVICE);
	if (ret < 0)
		goto invalidate_scan;
	cpu_access = true;

	if (snapshot_enabled) {
		mutex_lock(&ms912x->damage_scan_lock);
		scan_lock_held = true;
	}

	if (software_scan) {
		scan_ret = ms912x_damage_scan_find_changes(
			ms912x, fb, map, fmtcnv_state, force_full_scan,
			damage_rect);
		if (scan_ret < 0) {
			ret = scan_ret;
			goto invalidate_scan;
		}
		if (!scan_ret) {
			ret = -ENODATA;
			goto no_damage;
		}
		ms912x_rect_union(rect, damage_rect, &ms912x->update_rect);
	}

	/* UYVY stores pixels in pairs. Expand damage to a complete pair. */
	ms912x_rect_align_uyvy(rect, fb->width);
	full_update = rect->x1 == 0 && rect->y1 == 0 &&
		rect->x2 == fb->width && rect->y2 == fb->height;
	if (full_update)
		MS912X_STAT_INC(ms912x, merged_full_updates);

	if (snapshot_enabled) {
		if (!software_scan) {
			ms912x_damage_scan_update_snapshot(ms912x, fb, map, rect);
		} else {
			ms912x->damage_scan_width = fb->width;
			ms912x->damage_scan_height = fb->height;
			ms912x->damage_scan_valid = true;
			atomic_set(&ms912x->damage_scan_invalid, 0);
		}
	}

	/* Preserve pixels before the DRM shadow-map lifetime ends. */
	if (force_full_scan ||
	    (ms912x->pending_valid &&
	     (ms912x->pending_width != fb->width ||
	      ms912x->pending_height != fb->height))) {
		ms912x->frame_generation++;
		ms912x->last_transfer_valid = false;
		ms912x->pending_valid = false;
		ms912x->pending_force_full = false;
		ms912x->pending_idle_replay = false;
		ms912x->pending_deadline = 0;
		ms912x->pending_started_ns = 0;
		ms912x->frame_snapshot_valid = false;
	}
	/* Real display activity restarts the idle tile cycle. */
	ms912x->idle_replay_generation++;
	ms912x->idle_replay_tile = 0;
	if (ms912x->pending_valid && ms912x->pending_idle_replay) {
		ms912x->pending_valid = false;
		ms912x->pending_force_full = false;
		ms912x->pending_idle_replay = false;
	}
	if (stats_enabled)
		snapshot_copy_start_ns = ktime_get_ns();
	ms912x_snapshot_capture_damage(ms912x, fb, map, rect);
	ms912x->frame_snapshot_width = fb->width;
	ms912x->frame_snapshot_height = fb->height;
	ms912x->last_frame_activity = jiffies;
	if (stats_enabled)
		atomic64_add(ktime_get_ns() - snapshot_copy_start_ns,
			     &ms912x->stats.async_snapshot_copy_ns);

	had_pending = ms912x->pending_valid;
	if (had_pending) {
		ms912x_rect_union(&ms912x->pending_rect,
				  &ms912x->pending_rect, rect);
		ms912x_rect_union(&ms912x->pending_damage_rect,
				  &ms912x->pending_damage_rect, damage_rect);
		MS912X_STAT_INC(ms912x, damage_coalesced_updates);
	} else {
		ms912x->pending_rect = *rect;
		ms912x->pending_damage_rect = *damage_rect;
		ms912x->pending_valid = true;
		ms912x->pending_width = fb->width;
		ms912x->pending_height = fb->height;
		ms912x->pending_started_ns = 0;
		ms912x->pending_deadline = 0;
	}
	ms912x->pending_force_full |= force_full_scan || full_update;

	if (force_full_scan || full_update) {
		ms912x->pending_deadline = 0;
	} else if (!had_pending) {
		unsigned long coalesce_delay =
			ms912x_transfer_coalesce_delay(ms912x, rect);

		if (coalesce_delay) {
			ms912x->pending_deadline = jiffies + coalesce_delay;
			ms912x->pending_started_ns = ktime_get_ns();
			MS912X_STAT_INC(ms912x, damage_coalesce_batches);
		}
	}
	ms912x_schedule_conversion_locked(ms912x);

	drm_gem_fb_end_cpu_access(fb, DMA_FROM_DEVICE);
	cpu_access = false;
	if (scan_lock_held) {
		mutex_unlock(&ms912x->damage_scan_lock);
		scan_lock_held = false;
	}
	goto dev_exit;

no_damage:
invalidate_scan:
	if (ret < 0 && ret != -ENODATA && snapshot_enabled) {
		if (!scan_lock_held) {
			mutex_lock(&ms912x->damage_scan_lock);
			scan_lock_held = true;
		}
		ms912x->damage_scan_valid = false;
		atomic_set(&ms912x->damage_scan_invalid, 1);
	}
	if (cpu_access)
		drm_gem_fb_end_cpu_access(fb, DMA_FROM_DEVICE);
	if (scan_lock_held)
		mutex_unlock(&ms912x->damage_scan_lock);
dev_exit:
	mutex_unlock(&ms912x->transfer_lock);
	drm_dev_exit(idx);
	return ret;
}
