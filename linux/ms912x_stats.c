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
	MS912X_RESET_STAT(damage_batched_updates);
	MS912X_RESET_STAT(damage_queue_overflow_merged_updates);
	MS912X_RESET_STAT(updates);
	MS912X_RESET_STAT(conversion_attempts);
	MS912X_RESET_STAT(conversion_successes);
	MS912X_RESET_STAT(conversion_pixels);
	MS912X_RESET_STAT(conversion_ns);
	MS912X_RESET_STAT(usb_requests);
	MS912X_RESET_STAT(usb_bytes);
	MS912X_RESET_STAT(usb_ns);
	MS912X_RESET_STAT(usb_errors);
	MS912X_RESET_STAT(usb_timeouts);
	MS912X_RESET_STAT(usb_short_transfers);
	MS912X_RESET_STAT(damage_replay_batches_queued);
	MS912X_RESET_STAT(damage_replay_batches_sent);
	MS912X_RESET_STAT(usb_requests_requeued_before_send);
	MS912X_RESET_STAT(drm_present_wait_frames);
	MS912X_RESET_STAT(drm_present_wait_ns);
	MS912X_RESET_STAT(drm_present_wait_timeouts);
	MS912X_RESET_STAT(cursor_plane_updates);
	MS912X_RESET_STAT(cursor_composite_packets);
	MS912X_RESET_STAT(cursor_composite_pixels);
	MS912X_RESET_STAT(software_scan_frames);
	MS912X_RESET_STAT(software_scan_frame_pixels);
	MS912X_RESET_STAT(software_scan_compared_pixels);
	MS912X_RESET_STAT(software_scan_bbox_pixels);
	MS912X_RESET_STAT(software_scan_ns);
	MS912X_RESET_STAT(software_scan_rect_add_calls);
	MS912X_RESET_STAT(software_scan_rect_add_max_calls);
	MS912X_RESET_STAT(software_scan_rectangles);
	MS912X_RESET_STAT(software_scan_rectangles_max);
	MS912X_RESET_STAT(scan_rect_overflow_merged_updates);
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
