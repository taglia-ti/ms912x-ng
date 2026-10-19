// SPDX-License-Identifier: GPL-2.0-only

#include <linux/atomic.h>
#include <linux/types.h>
#include <linux/ktime.h>

#include "ms912x.h"

DEFINE_STATIC_KEY_FALSE(ms912x_stats_enabled_key);

#define MS912X_RESET_STAT(name) atomic64_set(&stats->name, 0)

void ms912x_stats_reset(struct ms912x_device *ms912x)
{
	struct ms912x_stats *stats = &ms912x->stats;

	atomic64_set(&stats->session_start_ns, ktime_get_ns());
	MS912X_RESET_STAT(damage_batch_delays);
	MS912X_RESET_STAT(damage_batched_updates);
	MS912X_RESET_STAT(damage_queue_overflow_merged_updates);
	MS912X_RESET_STAT(updates);
	MS912X_RESET_STAT(no_damage_updates);
	MS912X_RESET_STAT(damage_updates);
	MS912X_RESET_STAT(damage_pixels);
	MS912X_RESET_STAT(damage_full_updates);
	MS912X_RESET_STAT(forced_full_updates);
	MS912X_RESET_STAT(modeset_updates);
	MS912X_RESET_STAT(framebuffer_swaps);
	MS912X_RESET_STAT(fb_swaps_with_clips);
	MS912X_RESET_STAT(fb_swaps_without_clips);
	MS912X_RESET_STAT(damage_clip_rects);
	MS912X_RESET_STAT(conversion_attempts);
	MS912X_RESET_STAT(conversion_successes);
	MS912X_RESET_STAT(conversion_last_ns);
	MS912X_RESET_STAT(conversion_max_ns);
	MS912X_RESET_STAT(conversion_last_pixels);
	MS912X_RESET_STAT(conversion_profile_frames);
	MS912X_RESET_STAT(conversion_color_ns);
	MS912X_RESET_STAT(async_snapshot_copy_ns);
	MS912X_RESET_STAT(conversion_pixels);
	MS912X_RESET_STAT(conversion_ns);
	MS912X_RESET_STAT(usb_requests);
	MS912X_RESET_STAT(queued_bytes);
	MS912X_RESET_STAT(usb_bytes);
	MS912X_RESET_STAT(usb_ns);
	MS912X_RESET_STAT(usb_last_ns);
	MS912X_RESET_STAT(usb_max_ns);
	MS912X_RESET_STAT(usb_last_bytes);
	MS912X_RESET_STAT(workqueue_wait_ns);
	MS912X_RESET_STAT(workqueue_wait_last_ns);
	MS912X_RESET_STAT(workqueue_wait_max_ns);
	MS912X_RESET_STAT(usb_errors);
	MS912X_RESET_STAT(usb_timeouts);
	MS912X_RESET_STAT(usb_short_transfers);
	MS912X_RESET_STAT(usb_last_error);
	MS912X_RESET_STAT(usb_control_retries);
	MS912X_RESET_STAT(drm_present_wait_frames);
	MS912X_RESET_STAT(drm_present_wait_ns);
	MS912X_RESET_STAT(drm_present_wait_last_ns);
	MS912X_RESET_STAT(drm_present_wait_max_ns);
	MS912X_RESET_STAT(drm_present_wait_timeouts);
	MS912X_RESET_STAT(cursor_plane_updates);
	MS912X_RESET_STAT(cursor_composite_packets);
	MS912X_RESET_STAT(cursor_composite_pixels);
	MS912X_RESET_STAT(cursor_nonintersecting_packets);
	MS912X_RESET_STAT(software_scan_frames);
	MS912X_RESET_STAT(software_scan_no_change);
	MS912X_RESET_STAT(software_scan_memcmp_frames);
	MS912X_RESET_STAT(software_scan_memcmp_rows);
	MS912X_RESET_STAT(software_scan_memcmp_equal_rows);
	MS912X_RESET_STAT(software_scan_memcmp_blocks);
	MS912X_RESET_STAT(software_scan_memcmp_equal_blocks);
	MS912X_RESET_STAT(software_scan_avx2_frames);
	MS912X_RESET_STAT(software_scan_avx2_rows);
	MS912X_RESET_STAT(software_scan_avx2_blocks);
	MS912X_RESET_STAT(software_scan_avx2_equal_blocks);
	MS912X_RESET_STAT(software_scan_avx2_relaxed_frames);
	MS912X_RESET_STAT(software_scan_avx2_relaxed_rows);
	MS912X_RESET_STAT(software_scan_frame_pixels);
	MS912X_RESET_STAT(software_scan_compared_pixels);
	MS912X_RESET_STAT(software_scan_bbox_pixels);
	MS912X_RESET_STAT(software_scan_ns);
	MS912X_RESET_STAT(software_scan_last_ns);
	MS912X_RESET_STAT(software_scan_max_ns);
	MS912X_RESET_STAT(software_scan_copy_ns);
	MS912X_RESET_STAT(software_scan_copy_last_ns);
	MS912X_RESET_STAT(software_scan_copy_max_ns);
	MS912X_RESET_STAT(software_scan_compare_ns);
	MS912X_RESET_STAT(software_scan_compare_last_ns);
	MS912X_RESET_STAT(software_scan_compare_max_ns);
	MS912X_RESET_STAT(software_scan_rect_add_ns);
	MS912X_RESET_STAT(software_scan_rect_add_last_ns);
	MS912X_RESET_STAT(software_scan_rect_add_max_ns);
	MS912X_RESET_STAT(software_scan_rect_add_calls);
	MS912X_RESET_STAT(software_scan_rect_add_max_calls);
	MS912X_RESET_STAT(software_scan_rectangles);
	MS912X_RESET_STAT(software_scan_rectangles_max);
	MS912X_RESET_STAT(scan_rect_overflow_merged_updates);
	MS912X_RESET_STAT(scan_frames_enqueued);
	MS912X_RESET_STAT(scan_frames_replaced);
	MS912X_RESET_STAT(scan_queue_full);
	MS912X_RESET_STAT(scan_work_queue_false);
	MS912X_RESET_STAT(scan_worker_frames);
	MS912X_RESET_STAT(scan_frames_published);
	MS912X_RESET_STAT(scan_epoch_dropped);
	MS912X_RESET_STAT(scan_publish_rejected);
	MS912X_RESET_STAT(scan_stop_rejected);
	MS912X_RESET_STAT(submit_stopping);
	MS912X_RESET_STAT(usb_stale_payload_refreshed);
}

#undef MS912X_RESET_STAT


void ms912x_stats_record_max(atomic64_t *counter, u64 value)
{
	s64 previous = atomic64_read(counter);

	while (previous < value) {
		s64 observed = atomic64_cmpxchg(counter, previous, value);

		if (observed == previous)
			break;

		previous = observed;
	}
}

void ms912x_stats_record_duration(atomic64_t *total, atomic64_t *last,
				  atomic64_t *maximum, u64 duration)
{
	atomic64_add(duration, total);
	atomic64_set(last, duration);
	ms912x_stats_record_max(maximum, duration);
}
