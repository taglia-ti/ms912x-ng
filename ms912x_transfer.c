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

static bool ms912x_rect_valid_for_frame(const struct drm_rect *rect,
					unsigned int width,
					unsigned int height)
{
	return width && height && width <= MS912X_MAX_WIDTH &&
		height <= MS912X_MAX_HEIGHT && rect->x1 >= 0 && rect->y1 >= 0 &&
		rect->x1 < rect->x2 && rect->y1 < rect->y2 &&
		rect->x2 <= width && rect->y2 <= height;
}

static u64 ms912x_rects_payload_bytes(const struct drm_rect *rects,
				     unsigned int rect_count)
{
	u64 bytes = 0;
	unsigned int i;

	for (i = 0; i < rect_count; i++)
		bytes += (u64)drm_rect_width(&rects[i]) *
			drm_rect_height(&rects[i]) * 2 + MS912X_FRAME_OVERHEAD;

	return bytes;
}

static u64 ms912x_rect_merge_cost(const struct drm_rect *a,
				 const struct drm_rect *b)
{
	struct drm_rect merged;
	u64 area_a = (u64)drm_rect_width(a) * drm_rect_height(a);
	u64 area_b = (u64)drm_rect_width(b) * drm_rect_height(b);
	u64 intersection;
	u64 area_merged;
	int overlap_width = min(a->x2, b->x2) - max(a->x1, b->x1);
	int overlap_height = min(a->y2, b->y2) - max(a->y1, b->y1);

	ms912x_rect_union(&merged, a, b);
	area_merged = (u64)drm_rect_width(&merged) *
		drm_rect_height(&merged);
	intersection = overlap_width > 0 && overlap_height > 0 ?
		(u64)overlap_width * overlap_height : 0;
	return area_merged > area_a + area_b - intersection ?
		area_merged - area_a - area_b + intersection : 0;
}

static bool ms912x_rect_merge_within_stale_limit(
					const struct drm_rect *a,
					const struct drm_rect *b,
					unsigned int stale_percent)
{
	struct drm_rect merged;
	u64 area;
	u64 cost = ms912x_rect_merge_cost(a, b);

	ms912x_rect_union(&merged, a, b);
	area = (u64)drm_rect_width(&merged) * drm_rect_height(&merged);
	if (area * 2 + MS912X_FRAME_OVERHEAD <
	    MS912X_USB_COALESCE_MIN_BYTES)
		return true;
	return !stale_percent || cost * 100 <= area * stale_percent;
}

/* Caller holds transfer_lock. Promote accumulated damage once it is dominant. */
static void ms912x_pending_promote_full_locked(struct ms912x_device *ms912x,
					       unsigned int width,
					       unsigned int height)
{
	struct drm_rect bounds = DRM_RECT_INIT(0, 0, width, height);
	u64 damage_area = 0, frame_area = (u64)width * height;
	unsigned int i;

	if (!ms912x->pending_rect_count ||
	    (ms912x->pending_rect_count == 1 &&
	     drm_rect_equals(&ms912x->pending_rects[0].rect, &bounds)))
		return;
	for (i = 0; i < ms912x->pending_rect_count; i++)
		damage_area += (u64)drm_rect_width(
			&ms912x->pending_rects[i].rect) *
			drm_rect_height(&ms912x->pending_rects[i].rect);
	if (damage_area * 100 <
	    frame_area * MS912X_FULL_UPDATE_THRESHOLD_PERCENT)
		return;

	ms912x->pending_rects[0].rect = bounds;
	ms912x->pending_rect_count = 1;
	ms912x->pending_valid = true;
	ms912x->pending_force_full = true;
	ms912x->pending_deadline = 0;
	MS912X_STAT_INC(ms912x, merged_full_updates);
}

static bool ms912x_rects_touch(const struct drm_rect *a,
			       const struct drm_rect *b)
{
	return a->x1 <= b->x2 && b->x1 <= a->x2 &&
	       a->y1 <= b->y2 && b->y1 <= a->y2;
}

static void ms912x_rect_list_add(struct drm_rect *rects,
				 unsigned int *count,
				 unsigned int capacity,
				 const struct drm_rect *rect,
				 unsigned int width,
				 unsigned int height)
{
	struct drm_rect merged = *rect;
	struct drm_rect bounds = DRM_RECT_INIT(0, 0, width, height);
	unsigned int i;

	if (*count == 1 && drm_rect_equals(&rects[0], &bounds))
		return;
	for (i = 0; i < *count;) {
		if (!ms912x_rects_touch(&rects[i], &merged) ||
		    !ms912x_rect_merge_within_stale_limit(&rects[i], &merged,
							  MS912X_DEFAULT_STALE_AREA_PERCENT)) {
			i++;
			continue;
		}
		ms912x_rect_union(&merged, &merged, &rects[i]);
		memmove(&rects[i], &rects[i + 1],
			(*count - i - 1) * sizeof(*rects));
		(*count)--;
		i = 0;
	}
	if (*count == capacity) {
		u64 best_cost = U64_MAX;
		unsigned int first = 0, second = 0;

		/* Include the incoming rectangle; never collapse the entire frame. */
		for (i = 0; i <= *count; i++) {
			const struct drm_rect *a = i == *count ? rect : &rects[i];
			unsigned int j;

			for (j = i + 1; j <= *count; j++) {
				const struct drm_rect *b =
					j == *count ? rect : &rects[j];
				u64 cost;

				if (!ms912x_rect_merge_within_stale_limit(
					    a, b, MS912X_DEFAULT_STALE_AREA_PERCENT))
					continue;
				cost = ms912x_rect_merge_cost(a, b);

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
			return;
		}
		if (second == *count) {
			ms912x_rect_union(&rects[first], &rects[first], rect);
			return;
		}
		ms912x_rect_union(&rects[first], &rects[first], &rects[second]);
		memmove(&rects[second], &rects[second + 1],
			(*count - second - 1) * sizeof(*rects));
		(*count)--;
	}
	rects[(*count)++] = merged;
}

/* Caller holds transfer_lock. */
static void ms912x_pending_add_rect_locked(struct ms912x_device *ms912x,
					   const struct drm_rect *rect)
{
	struct drm_rect merged = *rect;
	unsigned int i, insert_at = UINT_MAX;

	if (WARN_ON_ONCE(!ms912x_rect_valid_for_frame(
			 &merged, ms912x->pending_width, ms912x->pending_height))) {
		merged = DRM_RECT_INIT(0, 0, ms912x->pending_width,
				       ms912x->pending_height);
	}

	for (i = 0; i < ms912x->pending_rect_count;) {
		struct ms912x_pending_rect *pending =
			&ms912x->pending_rects[i];

		if (!ms912x_rects_touch(&pending->rect, &merged)) {
			i++;
			continue;
		}
		/* Touching clips may merge only within the stale-area budget. */
		if (!ms912x_rect_merge_within_stale_limit(
			    &pending->rect, &merged,
			    MS912X_DEFAULT_STALE_AREA_PERCENT)) {
			i++;
			continue;
		}
		ms912x_rect_union(&merged, &merged, &pending->rect);
		MS912X_STAT_INC(ms912x, damage_coalesced_rects);
		insert_at = min(insert_at, i);
		memmove(pending, pending + 1,
			(ms912x->pending_rect_count - i - 1) *
			sizeof(*pending));
		ms912x->pending_rect_count--;
		i = 0;
	}

	if (ms912x->pending_rect_count == MS912X_MAX_DAMAGE_RECTS) {
		u64 best_cost = U64_MAX;
		unsigned int merge_at = 0;

		/* Free one queue slot with the least costly neighboring merge. */
		for (i = 0; i + 1 < ms912x->pending_rect_count; i++) {
			u64 cost;

			if (!ms912x_rect_merge_within_stale_limit(
				    &ms912x->pending_rects[i].rect,
				    &ms912x->pending_rects[i + 1].rect,
				    MS912X_DEFAULT_STALE_AREA_PERCENT))
				continue;
			cost = ms912x_rect_merge_cost(
				&ms912x->pending_rects[i].rect,
				&ms912x->pending_rects[i + 1].rect);
			if (cost < best_cost) {
				best_cost = cost;
				merge_at = i;
			}
		}
		if (best_cost == U64_MAX) {
			/* Preserve coverage without an over-50% lossy bbox merge. */
			ms912x->pending_rect_count = 0;
			ms912x->pending_valid = false;
			merged = DRM_RECT_INIT(0, 0, ms912x->pending_width,
					ms912x->pending_height);
			insert_at = 0;
			ms912x->pending_force_full = true;
			ms912x->pending_deadline = 0;
			MS912X_STAT_INC(ms912x,
				damage_queue_overflow_full_updates);
		} else {
			ms912x_rect_union(&ms912x->pending_rects[merge_at].rect,
					  &ms912x->pending_rects[merge_at].rect,
					  &ms912x->pending_rects[merge_at + 1].rect);
			MS912X_STAT_INC(ms912x, damage_coalesced_rects);
			memmove(&ms912x->pending_rects[merge_at + 1],
				&ms912x->pending_rects[merge_at + 2],
				(ms912x->pending_rect_count - merge_at - 2) *
				sizeof(*ms912x->pending_rects));
			ms912x->pending_rect_count--;
		}
	}
	if (insert_at == UINT_MAX)
		insert_at = ms912x->pending_rect_count;
	else
		memmove(&ms912x->pending_rects[insert_at + 1],
			&ms912x->pending_rects[insert_at],
			(ms912x->pending_rect_count - insert_at) *
			sizeof(*ms912x->pending_rects));
	ms912x->pending_rects[insert_at].rect = merged;
	ms912x->pending_rect_count++;
	ms912x->pending_valid = true;
}

/* Caller holds transfer_lock. */
static void ms912x_pending_pop_locked(struct ms912x_device *ms912x)
{
	unsigned int i;

	for (i = 1; i < ms912x->pending_rect_count; i++)
		ms912x->pending_rects[i - 1] = ms912x->pending_rects[i];
	ms912x->pending_rect_count--;
	ms912x->pending_valid = ms912x->pending_rect_count != 0;
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
	if (pixels * 2 + MS912X_FRAME_OVERHEAD <
	    MS912X_USB_COALESCE_MIN_BYTES)
		pixels = DIV_ROUND_UP_ULL(
			MS912X_USB_COALESCE_MIN_BYTES - MS912X_FRAME_OVERHEAD, 2);
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

static void ms912x_snapshot_capture_frame(u32 *dest_frame,
					  const u32 *frame,
					  unsigned int width,
					  unsigned int height)
{
	unsigned int y;

	/* Keep the conversion source as one coherent, complete scanned frame. */
	for (y = 0; y < height; y++) {
		const u32 *src = frame + y * MS912X_MAX_WIDTH;
		u32 *dest = dest_frame + y * MS912X_MAX_WIDTH;

		memcpy(dest, src, width * sizeof(*dest));
	}
}

static void ms912x_scan_frame_worker(struct work_struct *work)
{
	struct ms912x_device *ms912x =
		container_of(work, struct ms912x_device, scan_work);

	for (;;) {
		struct ms912x_scan_frame *frame = NULL;
		struct drm_rect rects[MS912X_MAX_DAMAGE_RECTS];
		unsigned int rect_count = 0, i, slot;
		unsigned int width, height;
		u32 generation;
		u64 sequence;
		bool force_full, published = false;
		int ret, idx;

		mutex_lock(&ms912x->scan_queue_lock);
		if (ms912x->scan_stopping) {
			for (slot = 0; slot < ARRAY_SIZE(ms912x->scan_frames); slot++)
				ms912x->scan_frames[slot].queued = false;
			ms912x->scan_work_scheduled = false;
			mutex_unlock(&ms912x->scan_queue_lock);
			return;
		}
		for (slot = 0; slot < ARRAY_SIZE(ms912x->scan_frames); slot++) {
			struct ms912x_scan_frame *candidate =
				&ms912x->scan_frames[slot];

			if (!candidate->queued)
				continue;
			if (!frame || candidate->sequence < frame->sequence)
				frame = candidate;
		}
		if (!frame) {
			ms912x->scan_work_scheduled = false;
			mutex_unlock(&ms912x->scan_queue_lock);
			return;
		}
		frame->queued = false;
		frame->running = true;
		width = frame->width;
		height = frame->height;
		generation = frame->generation;
		sequence = frame->sequence;
		force_full = frame->force_full;
		MS912X_STAT_INC(ms912x, scan_worker_frames);
		mutex_unlock(&ms912x->scan_queue_lock);

		/* Drop only snapshots from an obsolete modeset/size epoch. */
		if (generation != READ_ONCE(ms912x->frame_generation)) {
			MS912X_STAT_INC(ms912x, scan_epoch_dropped);
			goto frame_done;
		}
		if (!drm_dev_enter(&ms912x->drm, &idx))
			goto frame_done;
		ret = ms912x_damage_scan_find_snapshot_changes(
			ms912x, frame->pixels, sequence, generation,
			ms912x->damage_scan_snapshot,
			ms912x->damage_scan_snapshot_back, width, height, force_full,
			rects, &rect_count);
		if (ret >= 0 && generation == READ_ONCE(ms912x->frame_generation)) {
			mutex_lock(&ms912x->transfer_lock);
			mutex_lock(&ms912x->damage_scan_lock);
			if (!ms912x->transfer_stopping &&
			    generation == ms912x->frame_generation &&
			    !atomic_read(&ms912x->damage_scan_invalid)) {
				bool full_update = force_full ||
					(rect_count == 1 && rects[0].x1 == 0 &&
					 rects[0].y1 == 0 && rects[0].x2 == width &&
					 rects[0].y2 == height);
				bool had_pending = ms912x->pending_rect_count != 0;
				u32 *buffer;

				/* Replace pixels and baseline atomically to readers. */
				ms912x_snapshot_capture_frame(ms912x->latest_xrgb,
							     frame->pixels,
							     width, height);
				buffer = ms912x->damage_scan_snapshot;
				ms912x->damage_scan_snapshot =
					ms912x->damage_scan_snapshot_back;
				ms912x->damage_scan_snapshot_back = buffer;
				ms912x->published_frame_sequence = sequence;
				if (rect_count)
					ms912x->scratch_generation++;
				published = true;
				MS912X_STAT_INC(ms912x, scan_frames_published);
				if (full_update) {
					ms912x->pending_rect_count = 0;
					ms912x->pending_valid = false;
					ms912x->pending_force_full = false;
					ms912x->pending_deadline = 0;
					had_pending = false;
				}
				if (rect_count && had_pending)
					MS912X_STAT_INC(ms912x,
							damage_coalesced_updates);
				ms912x->pending_width = width;
				ms912x->pending_height = height;
				for (i = 0; i < rect_count; i++) {
					ms912x_pending_add_rect_locked(ms912x,
								      &rects[i]);
				}
				ms912x_pending_promote_full_locked(ms912x,
								   width, height);
				ms912x->pending_width = width;
				ms912x->pending_height = height;
				ms912x->pending_force_full |= full_update;
				if (rect_count && !full_update && !had_pending &&
				    !ms912x->pending_force_full) {
					unsigned long delay =
						ms912x_transfer_coalesce_delay(
							ms912x, &rects[0]);

					if (delay) {
						ms912x->pending_deadline =
							jiffies + delay;
						MS912X_STAT_INC(ms912x,
							damage_coalesce_batches);
					}
				}
				if (rect_count)
					ms912x_schedule_conversion_locked(ms912x);
				if (ms912x_rects_payload_bytes(rects, rect_count) <
				    MS912X_SMALL_DAMAGE_THRESHOLD_BYTES) {
					WRITE_ONCE(ms912x->small_damage_sequence,
						   sequence);
					wake_up_all(&ms912x->frame_present_wait);
				}
				ms912x_transfer_update_pacing_locked(ms912x);
			}
			mutex_unlock(&ms912x->damage_scan_lock);
			mutex_unlock(&ms912x->transfer_lock);
			if (!published)
				MS912X_STAT_INC(ms912x, scan_publish_rejected);
			if (!published)
				ms912x_damage_scan_invalidate(ms912x);
		} else if (ret < 0) {
			ms912x_damage_scan_invalidate(ms912x);
			mutex_lock(&ms912x->transfer_lock);
			ms912x->failed_present_sequence = max(
				ms912x->failed_present_sequence, sequence);
			wake_up_all(&ms912x->frame_present_wait);
			mutex_unlock(&ms912x->transfer_lock);
			drm_err_ratelimited(&ms912x->drm,
					    "async damage scan failed: %d\n", ret);
		} else {
			/* A newer modeset superseded this queued snapshot. */
			ms912x_damage_scan_invalidate(ms912x);
		}
		drm_dev_exit(idx);

frame_done:
		mutex_lock(&ms912x->scan_queue_lock);
		frame->running = false;
		mutex_unlock(&ms912x->scan_queue_lock);
		mutex_lock(&ms912x->transfer_lock);
		ms912x_transfer_update_pacing_locked(ms912x);
		mutex_unlock(&ms912x->transfer_lock);
	}
}

static int ms912x_scan_enqueue_frame_locked(struct ms912x_device *ms912x,
					    struct drm_framebuffer *fb,
					    const struct iosys_map *map,
					    bool force_full,
					    u64 sequence)
{
	struct ms912x_scan_frame *slot = NULL;
	unsigned int i, y;
	bool queue_scan = false;

	mutex_lock(&ms912x->scan_queue_lock);
	if (ms912x->scan_stopping) {
		MS912X_STAT_INC(ms912x, scan_stop_rejected);
		mutex_unlock(&ms912x->scan_queue_lock);
		return -ESHUTDOWN;
	}
	for (i = 0; i < ARRAY_SIZE(ms912x->scan_frames); i++) {
		struct ms912x_scan_frame *candidate = &ms912x->scan_frames[i];

		if (!candidate->queued && !candidate->running) {
			slot = candidate;
			break;
		}
	}
	if (!slot) {
		/* Keep the running scan; replace the newest waiting snapshot. */
		for (i = 0; i < ARRAY_SIZE(ms912x->scan_frames); i++) {
			struct ms912x_scan_frame *candidate =
				&ms912x->scan_frames[i];

			if (candidate->queued &&
			    (!slot || candidate->sequence > slot->sequence))
				slot = candidate;
		}
	}
	if (!slot) {
		MS912X_STAT_INC(ms912x, scan_queue_full);
		mutex_unlock(&ms912x->scan_queue_lock);
		return -EBUSY;
	}

	force_full |= slot->queued && slot->force_full;
	if (slot->queued)
		MS912X_STAT_INC(ms912x, scan_frames_replaced);
	for (y = 0; y < fb->height; y++)
		iosys_map_memcpy_from(slot->pixels + y * MS912X_MAX_WIDTH, map,
				     y * fb->pitches[0],
				     fb->width * sizeof(*slot->pixels));
	slot->width = fb->width;
	slot->height = fb->height;
	slot->generation = ms912x->frame_generation;
	slot->force_full = force_full;
	slot->sequence = sequence;
	MS912X_STAT_INC(ms912x, scan_frames_enqueued);
	if (!slot->queued) {
		slot->queued = true;
		if (!ms912x->scan_work_scheduled) {
			ms912x->scan_work_scheduled = true;
			queue_scan = true;
		}
	}
	mutex_unlock(&ms912x->scan_queue_lock);
	if (queue_scan && !queue_work(system_dfl_wq, &ms912x->scan_work))
		MS912X_STAT_INC(ms912x, scan_work_queue_false);
	return 0;
}

static bool ms912x_scan_is_pending(struct ms912x_device *ms912x)
{
	unsigned int i;
	bool pending = false;

	mutex_lock(&ms912x->scan_queue_lock);
	for (i = 0; i < ARRAY_SIZE(ms912x->scan_frames); i++)
		pending |= ms912x->scan_frames[i].queued ||
			ms912x->scan_frames[i].running;
	mutex_unlock(&ms912x->scan_queue_lock);
	return pending;
}

/* Caller holds transfer_lock. Exclude the USB packet currently in flight. */
static u64 ms912x_present_queue_bytes_locked(struct ms912x_device *ms912x)
{
	u64 bytes = 0;
	unsigned int i;

	for (i = 0; i < ms912x->pending_rect_count; i++)
		bytes += (u64)drm_rect_width(
			&ms912x->pending_rects[i].rect) *
			drm_rect_height(&ms912x->pending_rects[i].rect) * 2 +
			MS912X_FRAME_OVERHEAD;
	if (ms912x->failed_rect_valid)
		bytes += (u64)drm_rect_width(&ms912x->failed_rect) *
			drm_rect_height(&ms912x->failed_rect) * 2 +
			MS912X_FRAME_OVERHEAD;

	for (i = 0; i < ARRAY_SIZE(ms912x->requests); i++)
		if (ms912x->requests[i].state != MS912X_REQUEST_FREE &&
		    ms912x->requests[i].state != MS912X_REQUEST_USB_ACTIVE)
			bytes += ms912x->requests[i].transfer_len;

	return bytes;
}

/* Caller holds transfer_lock. Pace against queued-byte drain time. */
void ms912x_transfer_update_pacing_locked(struct ms912x_device *ms912x)
{
	u64 bytes = ms912x_present_queue_bytes_locked(ms912x);
	u64 usb_rate = max_t(u64, ms912x->usb_speed_mean_bps, 1);
	unsigned int refresh_hz = READ_ONCE(ms912x->display_refresh_hz);
	u64 frame_period_ns;
	u64 low_watermark_ns, high_watermark_ns, pressure_ns;

	if (!refresh_hz)
		refresh_hz = 60;
	frame_period_ns = div64_u64(NSEC_PER_SEC, refresh_hz);
	low_watermark_ns = div64_u64(frame_period_ns * 3, 2);
	high_watermark_ns = frame_period_ns * 2;

	pressure_ns = div64_u64(bytes * NSEC_PER_SEC, usb_rate);
	WRITE_ONCE(ms912x->present_queue_pressure_ns, pressure_ns);
	WRITE_ONCE(ms912x->present_queue_low_watermark_ns,
		   low_watermark_ns);
	WRITE_ONCE(ms912x->present_queue_high_watermark_ns,
		   high_watermark_ns);

	if (!ms912x->present_backpressured) {
		if (pressure_ns >= high_watermark_ns)
			ms912x->present_backpressured = true;
	} else if (pressure_ns <= low_watermark_ns) {
		ms912x->present_backpressured = false;
	}

	if (!ms912x->present_backpressured &&
	    ms912x->failed_present_sequence <
		ms912x->published_frame_sequence &&
	    ms912x->published_frame_sequence > ms912x->drm_released_sequence) {
		ms912x->drm_released_sequence =
			ms912x->published_frame_sequence;
		wake_up_all(&ms912x->frame_present_wait);
	}
}

static unsigned long ms912x_small_damage_wait_jiffies(
						struct ms912x_device *ms912x)
{
	u64 usb_rate = max_t(u64, READ_ONCE(ms912x->usb_speed_mean_bps), 1);
	u64 wait_ns = div64_u64(
		(u64)MS912X_SMALL_DAMAGE_THRESHOLD_BYTES * NSEC_PER_SEC,
		usb_rate);

	return max_t(unsigned long, 1, nsecs_to_jiffies(wait_ns));
}

static int ms912x_wait_for_pacing_release(struct ms912x_device *ms912x,
					  u64 sequence, u32 generation)
{
	bool stats_enabled = ms912x_stats_enabled();
	u64 start_ns = stats_enabled ? ktime_get_ns() : 0;
	unsigned long wait_start = jiffies;
	unsigned long small_damage_cap =
		ms912x_small_damage_wait_jiffies(ms912x);
	unsigned long elapsed, remaining;
	long waited;

	if (stats_enabled)
		MS912X_STAT_INC(ms912x, drm_present_wait_frames);
	waited = wait_event_timeout(ms912x->frame_present_wait,
		READ_ONCE(ms912x->transfer_stopping) ||
		READ_ONCE(ms912x->frame_generation) != generation ||
		READ_ONCE(ms912x->failed_present_sequence) >= sequence ||
		READ_ONCE(ms912x->drm_released_sequence) >= sequence ||
		READ_ONCE(ms912x->small_damage_sequence) == sequence,
		msecs_to_jiffies(5000));
	if (READ_ONCE(ms912x->small_damage_sequence) == sequence &&
	    READ_ONCE(ms912x->drm_released_sequence) < sequence &&
	    !READ_ONCE(ms912x->transfer_stopping) &&
	    READ_ONCE(ms912x->frame_generation) == generation &&
	    READ_ONCE(ms912x->failed_present_sequence) < sequence) {
		elapsed = jiffies - wait_start;
		remaining = elapsed < small_damage_cap ?
			small_damage_cap - elapsed : 0;
		if (remaining)
			wait_event_timeout(ms912x->frame_present_wait,
				READ_ONCE(ms912x->transfer_stopping) ||
				READ_ONCE(ms912x->frame_generation) != generation ||
				READ_ONCE(ms912x->failed_present_sequence) >= sequence ||
				READ_ONCE(ms912x->drm_released_sequence) >= sequence,
				remaining);

		/* Small damage must not wait longer than its capped USB service time. */
		mutex_lock(&ms912x->transfer_lock);
		if (ms912x->small_damage_sequence == sequence &&
		    ms912x->frame_generation == generation &&
		    ms912x->failed_present_sequence < sequence &&
		    ms912x->drm_released_sequence < sequence) {
			ms912x->drm_released_sequence = sequence;
			wake_up_all(&ms912x->frame_present_wait);
		}
		mutex_unlock(&ms912x->transfer_lock);
	}
	if (stats_enabled)
		ms912x_stats_record_duration(
			&ms912x->stats.drm_present_wait_ns,
			&ms912x->stats.drm_present_wait_last_ns,
			&ms912x->stats.drm_present_wait_max_ns,
			ktime_get_ns() - start_ns);
	if (!waited && READ_ONCE(ms912x->small_damage_sequence) != sequence) {
		if (stats_enabled)
			MS912X_STAT_INC(ms912x, drm_present_wait_timeouts);
		drm_warn(&ms912x->drm,
			 "timed out waiting for USB queue pacing on frame %llu\n",
			 (unsigned long long)sequence);
		return -ETIMEDOUT;
	}
	if (READ_ONCE(ms912x->transfer_stopping))
		return -ENODEV;
	if (READ_ONCE(ms912x->drm_released_sequence) >= sequence)
		return 0;
	if (READ_ONCE(ms912x->failed_present_sequence) >= sequence)
		return -EIO;
	return 0;
}

static bool ms912x_snapshot_stage_conversion(struct ms912x_device *ms912x,
					     const struct drm_rect *rect,
					     unsigned int width,
					     unsigned int height)
{
	unsigned int y;
	size_t rect_width;

	if (WARN_ON_ONCE(!ms912x_rect_valid_for_frame(rect, width, height)))
		return false;
	rect_width = drm_rect_width(rect);

	for (y = rect->y1; y < rect->y2; y++) {
		u32 *src = ms912x->latest_xrgb +
			y * MS912X_MAX_WIDTH + rect->x1;
		u32 *dest = ms912x->conversion_xrgb +
			y * MS912X_MAX_WIDTH + rect->x1;

		memcpy(dest, src, rect_width * sizeof(*dest));
	}
	return true;
}

/* Refresh a queued YUV payload if its source image changed while waiting. */
int ms912x_usb_request_refresh(struct ms912x_usb_request *request)
{
	struct ms912x_device *ms912x = request->ms912x;
	struct drm_rect rect = request->rect;
	unsigned int width = request->width, height = request->height;
	u64 source_generation, source_sequence;
	u64 start_ns = 0, conversion_ns;
	bool stats_enabled = ms912x_stats_enabled();
	int ret;

	for (;;) {
		mutex_lock(&ms912x->transfer_lock);
		if (ms912x->transfer_stopping ||
		    request->generation != ms912x->frame_generation) {
			mutex_unlock(&ms912x->transfer_lock);
			return -ECANCELED;
		}
		if (request->source_generation == ms912x->scratch_generation) {
			mutex_unlock(&ms912x->transfer_lock);
			return 0;
		}
		if (ms912x->conversion_busy) {
			mutex_unlock(&ms912x->transfer_lock);
			wait_event(ms912x->conversion_wait,
				   !READ_ONCE(ms912x->conversion_busy) ||
				   READ_ONCE(ms912x->transfer_stopping));
			continue;
		}
		if (WARN_ON_ONCE(!ms912x_rect_valid_for_frame(&rect, width,
							      height)))
			rect = DRM_RECT_INIT(0, 0, width, height);

		ms912x->conversion_busy = true;
		source_generation = ms912x->scratch_generation;
		source_sequence = ms912x->published_frame_sequence;
		if (!ms912x_snapshot_stage_conversion(ms912x, &rect, width,
						      height)) {
			ms912x->conversion_busy = false;
			wake_up_all(&ms912x->conversion_wait);
			mutex_unlock(&ms912x->transfer_lock);
			return -EINVAL;
		}
		mutex_unlock(&ms912x->transfer_lock);

		if (stats_enabled)
			start_ns = ktime_get_ns();
		ret = ms912x_convert_xrgb8888_to_uyvy(
			request->transfer_buffer,
			(const __le32 *)ms912x->conversion_xrgb,
			ms912x, width, height, &rect);
		if (stats_enabled) {
			conversion_ns = ktime_get_ns() - start_ns;
			ms912x_stats_record_duration(&ms912x->stats.conversion_ns,
					       &ms912x->stats.conversion_last_ns,
					       &ms912x->stats.conversion_max_ns,
					       conversion_ns);
		}

		mutex_lock(&ms912x->transfer_lock);
		ms912x->conversion_busy = false;
		wake_up_all(&ms912x->conversion_wait);
		if (!ret && !ms912x->transfer_stopping &&
		    request->generation == ms912x->frame_generation) {
			request->rect = rect;
			request->source_generation = source_generation;
			request->source_sequence = source_sequence;
			request->transfer_len = drm_rect_width(&rect) * 2 *
				drm_rect_height(&rect) + MS912X_FRAME_OVERHEAD;
			MS912X_STAT_INC(ms912x, usb_stale_payload_refreshed);
		}
		ms912x_schedule_conversion_locked(ms912x);
		ms912x_transfer_update_pacing_locked(ms912x);
		mutex_unlock(&ms912x->transfer_lock);
		if (ret) {
			ms912x_damage_scan_invalidate(ms912x);
			return ret;
		}
		if (READ_ONCE(ms912x->transfer_stopping))
			return -ECANCELED;
		/* A newer scratch may have been published during conversion. */
	}
}

/* Caller holds transfer_lock. */
static bool ms912x_transfer_usb_busy_locked(struct ms912x_device *ms912x)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(ms912x->requests); i++) {
		enum ms912x_request_state state = ms912x->requests[i].state;

		if (state == MS912X_REQUEST_USB_ACTIVE ||
		    state == MS912X_REQUEST_USB_QUEUED)
			return true;
	}
	return false;
}

/* Caller holds transfer_lock. Keep FIFO order and bound coalesced stale area. */
static void ms912x_pending_coalesce_for_usb_locked(
					struct ms912x_device *ms912x)
{
	struct ms912x_pending_rect *first;
	u64 min_bytes = MS912X_USB_COALESCE_MIN_BYTES;
	unsigned int i;

	if (!ms912x_transfer_usb_busy_locked(ms912x) ||
	    !ms912x->pending_rect_count)
		return;

	first = &ms912x->pending_rects[0];

	while (ms912x->pending_rect_count > 1) {
		u64 first_area = (u64)drm_rect_width(&first->rect) *
			drm_rect_height(&first->rect);
		u64 best_cost = U64_MAX;
		unsigned int best = UINT_MAX;

		if (first_area * 2 + MS912X_FRAME_OVERHEAD >= min_bytes)
			break;

		for (i = 1; i < ms912x->pending_rect_count; i++) {
			struct ms912x_pending_rect *candidate =
				&ms912x->pending_rects[i];
			u64 cost;

			if (!ms912x_rect_merge_within_stale_limit(
				    &first->rect, &candidate->rect,
				    MS912X_DEFAULT_STALE_AREA_PERCENT))
				continue;
			cost = ms912x_rect_merge_cost(&first->rect,
						     &candidate->rect);
			if (cost < best_cost) {
				best_cost = cost;
				best = i;
			}
		}
		if (best == UINT_MAX)
			break;

		ms912x_rect_union(&first->rect, &first->rect,
				  &ms912x->pending_rects[best].rect);
		memmove(&ms912x->pending_rects[best],
			&ms912x->pending_rects[best + 1],
			(ms912x->pending_rect_count - best - 1) *
			sizeof(*ms912x->pending_rects));
		ms912x->pending_rect_count--;
		MS912X_STAT_INC(ms912x, damage_coalesced_rects);
	}
}

static void ms912x_conversion_worker(struct work_struct *work)
{
	struct delayed_work *delayed_work = to_delayed_work(work);
	struct ms912x_device *ms912x =
		container_of(delayed_work, struct ms912x_device, conversion_work);
	struct ms912x_usb_request *request = NULL;
	struct ms912x_pending_rect pending;
	struct drm_rect rect, base_rect;
	u64 conversion_start_ns = 0, conversion_ns, coalesce_start_ns;
	u64 snapshot_copy_start_ns = 0;
	u64 source_sequence, scratch_generation;
	u32 generation;
	unsigned int width, height, i;
	bool stats_enabled;
	int idx, ret;

	if (!drm_dev_enter(&ms912x->drm, &idx))
		return;

	mutex_lock(&ms912x->transfer_lock);
	/* A USB stale-payload refresh may own the shared conversion scratch. */
	if (ms912x->transfer_stopping || ms912x->conversion_busy ||
	    !ms912x->pending_valid ||
	    !ms912x->pending_rect_count) {
		mutex_unlock(&ms912x->transfer_lock);
		drm_dev_exit(idx);
		return;
	}
	stats_enabled = ms912x_stats_enabled();
	if (stats_enabled) {
		atomic64_inc(&ms912x->stats.damage_coalesce_calls);
		coalesce_start_ns = ktime_get_ns();
	}
	ms912x_pending_coalesce_for_usb_locked(ms912x);
	if (stats_enabled)
		ms912x_stats_record_duration(
			&ms912x->stats.damage_coalesce_exec_ns,
			&ms912x->stats.damage_coalesce_exec_last_ns,
			&ms912x->stats.damage_coalesce_exec_max_ns,
			ktime_get_ns() - coalesce_start_ns);
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

	ms912x->conversion_busy = true;
	request->state = MS912X_REQUEST_CONVERTING;
	pending = ms912x->pending_rects[0];
	rect = pending.rect;
	width = ms912x->pending_width;
	height = ms912x->pending_height;
	generation = ms912x->frame_generation;
	source_sequence = ms912x->published_frame_sequence;
	scratch_generation = ms912x->scratch_generation;
	ms912x_rect_align_uyvy(&rect, width);
	base_rect = rect;
	if (ms912x->last_queued_rect_valid &&
	    ms912x->last_queued_generation == generation &&
	    ms912x->last_queued_width == width &&
	    ms912x->last_queued_height == height)
		ms912x_rect_union(&rect, &ms912x->last_queued_rect, &base_rect);
	if (ms912x->failed_rect_valid &&
	    ms912x->failed_generation == generation &&
	    ms912x->failed_width == width && ms912x->failed_height == height)
		ms912x_rect_union(&rect, &rect, &ms912x->failed_rect);
	if (WARN_ON_ONCE(!ms912x_rect_valid_for_frame(&rect, width, height))) {
		rect = DRM_RECT_INIT(0, 0, width, height);
		base_rect = rect;
	}
	request->rect = rect;
	request->width = width;
	request->height = height;
	request->transfer_len = drm_rect_width(&rect) * 2 *
		drm_rect_height(&rect) + MS912X_FRAME_OVERHEAD;
	if (stats_enabled)
		snapshot_copy_start_ns = ktime_get_ns();
	if (WARN_ON_ONCE(!ms912x_snapshot_stage_conversion(ms912x, &rect,
							  width, height))) {
		ms912x->conversion_busy = false;
		request->state = MS912X_REQUEST_FREE;
		ms912x_pending_add_rect_locked(ms912x,
					       &(struct drm_rect) {
							.x1 = 0, .y1 = 0,
							.x2 = width, .y2 = height,
						       });
		ms912x_schedule_conversion_locked(ms912x);
		mutex_unlock(&ms912x->transfer_lock);
		drm_dev_exit(idx);
		return;
	}
	if (stats_enabled)
		atomic64_add(ktime_get_ns() - snapshot_copy_start_ns,
			     &ms912x->stats.async_snapshot_copy_ns);
	ms912x_pending_pop_locked(ms912x);
	if (!ms912x->pending_valid)
		ms912x->pending_force_full = false;
	ms912x->pending_deadline = 0;
	ms912x_transfer_update_pacing_locked(ms912x);
	mutex_unlock(&ms912x->transfer_lock);

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
	    generation != ms912x->frame_generation) {
		request->state = MS912X_REQUEST_FREE;
		if (ret)
			ms912x->failed_present_sequence = max(
				ms912x->failed_present_sequence, source_sequence);
		ms912x_schedule_conversion_locked(ms912x);
		ms912x_transfer_update_pacing_locked(ms912x);
		wake_up_all(&ms912x->conversion_wait);
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
	request->rect = rect;
	request->base_rect = base_rect;
	request->width = width;
	request->height = height;
	request->source_sequence = source_sequence;
	request->queued_at_ns = ktime_get_ns();
	request->generation = generation;
	request->source_generation = scratch_generation;
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
		ms912x->last_queued_rect = base_rect;
		ms912x->last_queued_width = width;
		ms912x->last_queued_height = height;
		ms912x->last_queued_generation = generation;
		ms912x->last_queued_rect_valid = true;
	}
	ms912x_transfer_update_pacing_locked(ms912x);
	wake_up_all(&ms912x->conversion_wait);
	ms912x_schedule_conversion_locked(ms912x);
	mutex_unlock(&ms912x->transfer_lock);
	drm_dev_exit(idx);
}

void ms912x_transfer_work_init(struct ms912x_device *ms912x)
{
	init_waitqueue_head(&ms912x->conversion_wait);
	init_waitqueue_head(&ms912x->frame_present_wait);
	INIT_DELAYED_WORK(&ms912x->conversion_work, ms912x_conversion_worker);
	INIT_WORK(&ms912x->scan_work, ms912x_scan_frame_worker);
}

int ms912x_framebuffer_submit(struct drm_framebuffer *fb, const struct iosys_map *map,
			struct drm_rect *rects, unsigned int *rect_count,
			bool software_scan, bool force_full_scan,
			bool new_epoch)
{
	int ret = 0, idx;
	struct ms912x_device *ms912x = to_ms912x(fb->dev);
	struct drm_device *drm = &ms912x->drm;
	struct drm_rect bounds = DRM_RECT_INIT(0, 0, fb->width, fb->height);
	struct drm_rect current_rects[MS912X_MAX_DAMAGE_RECTS];
	bool had_pending, full_update = false;
	bool cpu_access = false, scan_lock_held = false;
	bool wait_for_present = false;
	bool snapshot_enabled = ms912x->damage_scan_snapshot;
	bool stats_enabled;
	unsigned int current_count = 0, i;
	u32 wait_generation = 0;
	u64 snapshot_copy_start_ns, sequence;

	if (!drm_dev_enter(drm, &idx))
		return -ENODEV;
	MS912X_STAT_INC(ms912x, updates);
	stats_enabled = ms912x_stats_enabled();

	mutex_lock(&ms912x->transfer_lock);
	if (ms912x->transfer_stopping) {
		MS912X_STAT_INC(ms912x, submit_stopping);
		ret = -ENODEV;
		goto dev_exit;
	}
	ret = drm_gem_fb_begin_cpu_access(fb, DMA_FROM_DEVICE);
	if (ret < 0)
		goto invalidate_scan;
	cpu_access = true;

	new_epoch |= ms912x->frame_snapshot_width &&
		(ms912x->frame_snapshot_width != fb->width ||
		 ms912x->frame_snapshot_height != fb->height);
	force_full_scan |= new_epoch ||
		atomic_read(&ms912x->damage_scan_invalid);
	sequence = ++ms912x->frame_sequence;
	if (!software_scan && ms912x_scan_is_pending(ms912x))
		software_scan = true;
	if (new_epoch) {
		ms912x->frame_generation++;
		ms912x->last_queued_rect_valid = false;
		ms912x->failed_rect_valid = false;
		ms912x->pending_rect_count = 0;
		ms912x->pending_valid = false;
		ms912x->pending_force_full = false;
		ms912x->pending_deadline = 0;
		atomic_set(&ms912x->damage_scan_invalid, 1);
	}
	wait_generation = ms912x->frame_generation;
	ms912x->frame_snapshot_width = fb->width;
	ms912x->frame_snapshot_height = fb->height;

	if (software_scan) {
		if (stats_enabled)
			snapshot_copy_start_ns = ktime_get_ns();
		ret = ms912x_scan_enqueue_frame_locked(ms912x, fb, map,
						       force_full_scan, sequence);
		if (stats_enabled)
			atomic64_add(ktime_get_ns() - snapshot_copy_start_ns,
				     &ms912x->stats.async_snapshot_copy_ns);
		if (ret < 0)
			goto invalidate_scan;
		wait_for_present = true;
		goto end_cpu_access;
	}
	if (snapshot_enabled) {
		mutex_lock(&ms912x->damage_scan_lock);
		scan_lock_held = true;
	}
	if (force_full_scan) {
		rects[0] = bounds;
		*rect_count = 1;
	}
	if (!*rect_count) {
		ret = -ENODATA;
		goto no_damage;
	}
	if (*rect_count > MS912X_MAX_DAMAGE_RECTS) {
		struct drm_rect merged = rects[0];

		for (i = 1; i < *rect_count; i++)
			ms912x_rect_union(&merged, &merged, &rects[i]);
		rects[0] = merged;
		*rect_count = 1;
	}
	for (i = 0; i < *rect_count; i++) {
		struct drm_rect rect = rects[i];

		rect.x1 = clamp_t(int, rect.x1, 0, fb->width);
		rect.y1 = clamp_t(int, rect.y1, 0, fb->height);
		rect.x2 = clamp_t(int, rect.x2, 0, fb->width);
		rect.y2 = clamp_t(int, rect.y2, 0, fb->height);
		if (rect.x1 >= rect.x2 || rect.y1 >= rect.y2)
			continue;
		ms912x_rect_align_uyvy(&rect, fb->width);
		ms912x_rect_list_add(current_rects, &current_count,
				     MS912X_MAX_DAMAGE_RECTS, &rect,
				     fb->width, fb->height);
		if (current_count == 1 &&
		    drm_rect_equals(&current_rects[0], &bounds)) {
			full_update = true;
			break;
		}
		full_update |= rect.x1 == 0 && rect.y1 == 0 &&
			rect.x2 == fb->width && rect.y2 == fb->height;
	}
	if (!current_count) {
		ret = -ENODATA;
		goto no_damage;
	}
	full_update |= current_count == 1 &&
		drm_rect_equals(&current_rects[0], &bounds);
	if (full_update)
		MS912X_STAT_INC(ms912x, merged_full_updates);
	if (ms912x_rects_payload_bytes(current_rects, current_count) <
	    MS912X_SMALL_DAMAGE_THRESHOLD_BYTES) {
		WRITE_ONCE(ms912x->small_damage_sequence, sequence);
		wake_up_all(&ms912x->frame_present_wait);
	}

	if (snapshot_enabled) {
		for (i = 0; i < current_count; i++)
			ms912x_damage_scan_update_snapshot(ms912x, fb, map,
							   &current_rects[i]);
	}
	if (stats_enabled)
		snapshot_copy_start_ns = ktime_get_ns();
	for (i = 0; i < current_count; i++)
		ms912x_snapshot_capture_damage(ms912x, fb, map,
					       &current_rects[i]);
	ms912x->published_frame_sequence = sequence;
	ms912x->scratch_generation++;
	if (stats_enabled)
		atomic64_add(ktime_get_ns() - snapshot_copy_start_ns,
			     &ms912x->stats.async_snapshot_copy_ns);

	if (full_update) {
		ms912x->pending_rect_count = 0;
		ms912x->pending_valid = false;
		ms912x->pending_force_full = false;
		ms912x->pending_deadline = 0;
	}
	had_pending = ms912x->pending_rect_count != 0;
	if (had_pending) {
		MS912X_STAT_INC(ms912x, damage_coalesced_updates);
	} else {
		ms912x->pending_width = fb->width;
		ms912x->pending_height = fb->height;
		ms912x->pending_deadline = 0;
	}
	ms912x->pending_force_full |= force_full_scan || full_update;
	for (i = 0; i < current_count; i++)
		ms912x_pending_add_rect_locked(ms912x, &current_rects[i]);
	ms912x_pending_promote_full_locked(ms912x, fb->width, fb->height);
	if (force_full_scan || full_update) {
		ms912x->pending_deadline = 0;
	} else if (!had_pending) {
		unsigned long delay = ms912x_transfer_coalesce_delay(
			ms912x, &current_rects[0]);

		if (delay) {
			ms912x->pending_deadline = jiffies + delay;
			MS912X_STAT_INC(ms912x, damage_coalesce_batches);
		}
	}
	ms912x_schedule_conversion_locked(ms912x);
	ms912x_transfer_update_pacing_locked(ms912x);
	wait_for_present = true;
	goto end_cpu_access;

no_damage:
	if (ret == -ENODATA)
		goto end_cpu_access;
invalidate_scan:
	if (snapshot_enabled) {
		if (!scan_lock_held) {
			mutex_lock(&ms912x->damage_scan_lock);
			scan_lock_held = true;
		}
		ms912x->damage_scan_valid = false;
		atomic_set(&ms912x->damage_scan_invalid, 1);
	}
end_cpu_access:
	if (cpu_access)
		drm_gem_fb_end_cpu_access(fb, DMA_FROM_DEVICE);
	if (scan_lock_held)
		mutex_unlock(&ms912x->damage_scan_lock);
dev_exit:
	mutex_unlock(&ms912x->transfer_lock);
	if (wait_for_present && !ret)
		ret = ms912x_wait_for_pacing_release(ms912x, sequence,
						     wait_generation);
	drm_dev_exit(idx);
	return ret;
}
