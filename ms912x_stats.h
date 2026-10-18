/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MS912X_STATS_H
#define MS912X_STATS_H

#include <linux/atomic.h>
#include <linux/jump_label.h>
#include <linux/types.h>

struct seq_file;
struct ms912x_device;

enum ms912x_update_interval_bucket {
	MS912X_UPDATE_INTERVAL_LT_8MS,
	MS912X_UPDATE_INTERVAL_8_17MS,
	MS912X_UPDATE_INTERVAL_17_25MS,
	MS912X_UPDATE_INTERVAL_25_40MS,
	MS912X_UPDATE_INTERVAL_GE_40MS,
	MS912X_UPDATE_INTERVAL_BUCKET_COUNT,
};

struct ms912x_event_rate_stats {
	atomic64_t events;
	atomic64_t intervals;
	atomic64_t interval_ns;
	atomic64_t last_event_ns;
	atomic64_t interval_buckets[MS912X_UPDATE_INTERVAL_BUCKET_COUNT];
};

struct ms912x_stats {
	struct ms912x_event_rate_stats plane_update_rate;
	struct ms912x_event_rate_stats damage_clip_rate;
	atomic64_t damage_coalesce_batches;
	atomic64_t damage_coalesced_updates;
	atomic64_t damage_queue_overflow_full_updates;
	atomic64_t damage_coalesce_ns;
	atomic64_t damage_coalesce_last_ns;
	atomic64_t damage_coalesce_max_ns;
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
	atomic64_t conversion_frames;
	atomic64_t conversion_last_ns;
	atomic64_t conversion_max_ns;
	atomic64_t conversion_last_pixels;
	atomic64_t conversion_profile_frames;
	atomic64_t conversion_color_ns;
	atomic64_t async_snapshot_copy_ns;
	atomic64_t merged_full_updates;
	atomic64_t converted_pixels;
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
	atomic64_t usb_last_error;
	atomic64_t usb_control_retries;
	atomic64_t software_scan_frames;
	atomic64_t software_scan_no_change;
	atomic64_t software_scan_memcmp_frames;
	atomic64_t software_scan_memcmp_rows;
	atomic64_t software_scan_memcmp_equal_rows;
	atomic64_t software_scan_memcmp_blocks;
	atomic64_t software_scan_memcmp_equal_blocks;
	atomic64_t software_scan_pixels;
	atomic64_t software_scan_changed_pixels;
	atomic64_t software_scan_bbox_pixels;
	atomic64_t software_scan_ns;
	atomic64_t software_scan_last_ns;
	atomic64_t software_scan_max_ns;
	atomic64_t scan_rect_overflow_full_updates;
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
void ms912x_stats_reset(struct ms912x_device *ms912x);
void ms912x_stats_record_event_rate(struct ms912x_event_rate_stats *stats,
				    u64 timestamp_ns);
void ms912x_stats_show_event_rate(
			struct seq_file *m, const char *name,
			const struct ms912x_event_rate_stats *stats);

#endif /* MS912X_STATS_H */
