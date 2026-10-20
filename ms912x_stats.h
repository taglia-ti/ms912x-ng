/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MS912X_STATS_H
#define MS912X_STATS_H

#include <linux/atomic.h>
#include <linux/jump_label.h>
#include <linux/types.h>

struct ms912x_device;

struct ms912x_stats {
	atomic64_t session_start_ns;
	atomic64_t damage_batched_updates;
	atomic64_t damage_queue_overflow_merged_updates;
	atomic64_t updates;
	atomic64_t conversion_attempts;
	atomic64_t conversion_successes;
	atomic64_t conversion_pixels;
	atomic64_t conversion_ns;
	atomic64_t usb_requests;
	atomic64_t usb_bytes;
	atomic64_t usb_ns;
	atomic64_t usb_errors;
	atomic64_t usb_timeouts;
	atomic64_t usb_short_transfers;
	atomic64_t damage_replay_batches_queued;
	atomic64_t damage_replay_batches_sent;
	atomic64_t usb_requests_requeued_before_send;
	atomic64_t drm_present_wait_frames;
	atomic64_t drm_present_wait_ns;
	atomic64_t drm_present_wait_timeouts;
	atomic64_t cursor_plane_updates;
	atomic64_t cursor_composite_packets;
	atomic64_t cursor_composite_pixels;
	atomic64_t software_scan_frames;
	atomic64_t software_scan_frame_pixels;
	atomic64_t software_scan_compared_pixels;
	atomic64_t software_scan_bbox_pixels;
	atomic64_t software_scan_ns;
	atomic64_t software_scan_rect_add_calls;
	atomic64_t software_scan_rect_add_max_calls;
	atomic64_t software_scan_rectangles;
	atomic64_t software_scan_rectangles_max;
	atomic64_t scan_rect_overflow_merged_updates;
};

DECLARE_STATIC_KEY_FALSE(ms912x_stats_enabled_key);

static inline bool ms912x_stats_enabled(void)
{
	return static_branch_unlikely(&ms912x_stats_enabled_key);
}

#define MS912X_STAT_INC(ms912x, counter) \
	do { \
		if (ms912x_stats_enabled()) \
			atomic64_inc(&(ms912x)->stats.counter); \
	} while (0)

#define MS912X_STAT_ADD(ms912x, counter, value) \
	do { \
		if (ms912x_stats_enabled()) \
			atomic64_add((value), &(ms912x)->stats.counter); \
	} while (0)

void ms912x_stats_record_max(atomic64_t *max, u64 value);
void ms912x_stats_reset(struct ms912x_device *ms912x);

#endif /* MS912X_STATS_H */
