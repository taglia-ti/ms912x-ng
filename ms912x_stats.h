/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MS912X_STATS_H
#define MS912X_STATS_H

#include <linux/atomic.h>
#include <linux/jump_label.h>
#include <linux/types.h>

struct ms912x_device;

struct ms912x_stats {
	atomic64_t session_start_ns;
	atomic64_t damage_batch_delays;
	atomic64_t damage_batched_updates;
	atomic64_t damage_queue_overflow_merged_updates;
	atomic64_t updates;
	atomic64_t no_damage_updates;
	atomic64_t damage_updates;
	atomic64_t damage_pixels;
	atomic64_t damage_full_updates;
	atomic64_t forced_full_updates;
	atomic64_t modeset_updates;
	atomic64_t framebuffer_swaps;
	atomic64_t fb_swaps_with_clips;
	atomic64_t fb_swaps_without_clips;
	atomic64_t damage_clip_rects;
	atomic64_t conversion_attempts;
	atomic64_t conversion_successes;
	atomic64_t conversion_last_ns;
	atomic64_t conversion_max_ns;
	atomic64_t conversion_last_pixels;
	atomic64_t conversion_profile_frames;
	atomic64_t conversion_color_ns;
	atomic64_t async_snapshot_copy_ns;
	atomic64_t conversion_pixels;
	atomic64_t conversion_ns;
	atomic64_t usb_requests;
	atomic64_t queued_bytes;
	atomic64_t usb_bytes;
	atomic64_t usb_ns;
	atomic64_t usb_last_ns;
	atomic64_t usb_max_ns;
	atomic64_t usb_last_bytes;
	atomic64_t workqueue_wait_ns;
	atomic64_t workqueue_wait_last_ns;
	atomic64_t workqueue_wait_max_ns;
	atomic64_t usb_errors;
	atomic64_t usb_timeouts;
	atomic64_t usb_short_transfers;
	atomic64_t damage_replay_batches_queued;
	atomic64_t damage_replay_batches_sent;
	atomic64_t usb_requests_requeued_before_send;
	atomic64_t usb_last_error;
	atomic64_t usb_control_retries;
	atomic64_t drm_present_wait_frames;
	atomic64_t drm_present_wait_ns;
	atomic64_t drm_present_wait_last_ns;
	atomic64_t drm_present_wait_max_ns;
	atomic64_t drm_present_wait_timeouts;
	atomic64_t cursor_plane_updates;
	atomic64_t cursor_composite_packets;
	atomic64_t cursor_composite_pixels;
	atomic64_t cursor_nonintersecting_packets;
	atomic64_t software_scan_frames;
	atomic64_t software_scan_no_change;
	atomic64_t software_scan_memcmp_frames;
	atomic64_t software_scan_memcmp_rows;
	atomic64_t software_scan_memcmp_equal_rows;
	atomic64_t software_scan_memcmp_blocks;
	atomic64_t software_scan_memcmp_equal_blocks;
	atomic64_t software_scan_avx2_frames;
	atomic64_t software_scan_avx2_rows;
	atomic64_t software_scan_avx2_blocks;
	atomic64_t software_scan_avx2_equal_blocks;
	atomic64_t software_scan_avx2_relaxed_frames;
	atomic64_t software_scan_avx2_relaxed_rows;
	atomic64_t software_scan_frame_pixels;
	atomic64_t software_scan_compared_pixels;
	atomic64_t software_scan_bbox_pixels;
	atomic64_t software_scan_ns;
	atomic64_t software_scan_last_ns;
	atomic64_t software_scan_max_ns;
	atomic64_t software_scan_copy_ns;
	atomic64_t software_scan_copy_last_ns;
	atomic64_t software_scan_copy_max_ns;
	atomic64_t software_scan_compare_ns;
	atomic64_t software_scan_compare_last_ns;
	atomic64_t software_scan_compare_max_ns;
	atomic64_t software_scan_rect_add_ns;
	atomic64_t software_scan_rect_add_last_ns;
	atomic64_t software_scan_rect_add_max_ns;
	atomic64_t software_scan_rect_add_calls;
	atomic64_t software_scan_rect_add_max_calls;
	atomic64_t software_scan_rectangles;
	atomic64_t software_scan_rectangles_max;
	atomic64_t scan_rect_overflow_merged_updates;
	atomic64_t scan_frames_enqueued;
	atomic64_t scan_frames_replaced;
	atomic64_t scan_queue_full;
	atomic64_t scan_work_queue_false;
	atomic64_t scan_worker_frames;
	atomic64_t scan_frames_published;
	atomic64_t scan_epoch_dropped;
	atomic64_t scan_publish_rejected;
	atomic64_t scan_stop_rejected;
	atomic64_t submit_stopping;
	atomic64_t usb_stale_payload_refreshed;
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

#define MS912X_STAT_SET(ms912x, counter, value) \
	do { \
		if (ms912x_stats_enabled()) \
			atomic64_set(&(ms912x)->stats.counter, (value)); \
	} while (0)

void ms912x_stats_record_duration(atomic64_t *total, atomic64_t *last,
				  atomic64_t *max, u64 duration);
void ms912x_stats_record_max(atomic64_t *max, u64 value);
void ms912x_stats_reset(struct ms912x_device *ms912x);

#endif /* MS912X_STATS_H */
