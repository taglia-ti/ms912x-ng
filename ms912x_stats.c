// SPDX-License-Identifier: GPL-2.0-only

#include <linux/atomic.h>
#include <linux/types.h>
#include <linux/ktime.h>
#include <linux/seq_file.h>

#include "ms912x.h"

DEFINE_STATIC_KEY_FALSE(ms912x_stats_enabled_key);

#define MS912X_RESET_STAT(name) atomic64_set(&stats->name, 0)

static void ms912x_stats_reset_event_rate(struct ms912x_event_rate_stats *stats)
{
	unsigned int i;

	atomic64_set(&stats->events, 0);
	atomic64_set(&stats->intervals, 0);
	atomic64_set(&stats->interval_ns, 0);
	atomic64_set(&stats->last_event_ns, 0);
	for (i = 0; i < ARRAY_SIZE(stats->interval_buckets); i++)
		atomic64_set(&stats->interval_buckets[i], 0);
}

void ms912x_stats_reset(struct ms912x_device *ms912x)
{
	struct ms912x_stats *stats = &ms912x->stats;

	ms912x_stats_reset_event_rate(&stats->plane_update_rate);
	ms912x_stats_reset_event_rate(&stats->damage_clip_rate);
	MS912X_RESET_STAT(damage_coalesce_batches);
	MS912X_RESET_STAT(damage_coalesced_updates);
	MS912X_RESET_STAT(damage_queue_overflow_full_updates);
	MS912X_RESET_STAT(damage_coalesce_ns);
	MS912X_RESET_STAT(damage_coalesce_last_ns);
	MS912X_RESET_STAT(damage_coalesce_max_ns);
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
	MS912X_RESET_STAT(conversion_frames);
	MS912X_RESET_STAT(conversion_last_ns);
	MS912X_RESET_STAT(conversion_max_ns);
	MS912X_RESET_STAT(conversion_last_pixels);
	MS912X_RESET_STAT(conversion_profile_frames);
	MS912X_RESET_STAT(conversion_color_ns);
	MS912X_RESET_STAT(async_snapshot_copy_ns);
	MS912X_RESET_STAT(merged_full_updates);
	MS912X_RESET_STAT(converted_pixels);
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
	MS912X_RESET_STAT(software_scan_frames);
	MS912X_RESET_STAT(software_scan_no_change);
	MS912X_RESET_STAT(software_scan_memcmp_frames);
	MS912X_RESET_STAT(software_scan_memcmp_rows);
	MS912X_RESET_STAT(software_scan_memcmp_equal_rows);
	MS912X_RESET_STAT(software_scan_memcmp_blocks);
	MS912X_RESET_STAT(software_scan_memcmp_equal_blocks);
	MS912X_RESET_STAT(software_scan_pixels);
	MS912X_RESET_STAT(software_scan_changed_pixels);
	MS912X_RESET_STAT(software_scan_bbox_pixels);
	MS912X_RESET_STAT(software_scan_ns);
	MS912X_RESET_STAT(software_scan_last_ns);
	MS912X_RESET_STAT(software_scan_max_ns);
	MS912X_RESET_STAT(scan_rect_overflow_full_updates);
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


static void ms912x_atomic64_max(atomic64_t *counter, u64 value)
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
	ms912x_atomic64_max(maximum, duration);
}

void ms912x_stats_record_event_rate(struct ms912x_event_rate_stats *stats,
				    u64 timestamp_ns)
{
	static const u64 interval_limits_ns[] = {
		8 * NSEC_PER_MSEC,
		17 * NSEC_PER_MSEC,
		25 * NSEC_PER_MSEC,
		40 * NSEC_PER_MSEC,
	};
	s64 previous_ns, observed_ns;
	u64 interval_ns;
	unsigned int bucket;

	atomic64_inc(&stats->events);
	previous_ns = atomic64_read(&stats->last_event_ns);
	for (;;) {
		if (timestamp_ns <= previous_ns)
			return;
		observed_ns = atomic64_cmpxchg(&stats->last_event_ns,
					       previous_ns,
					       (s64)timestamp_ns);
		if (observed_ns == previous_ns)
			break;
		previous_ns = observed_ns;
	}

	if (!previous_ns)
		return;

	interval_ns = timestamp_ns - previous_ns;
	atomic64_inc(&stats->intervals);
	atomic64_add(interval_ns, &stats->interval_ns);
	for (bucket = 0; bucket < ARRAY_SIZE(interval_limits_ns); bucket++) {
		if (interval_ns < interval_limits_ns[bucket])
			break;
	}
	atomic64_inc(&stats->interval_buckets[bucket]);
}

void ms912x_stats_show_event_rate(
	struct seq_file *m, const char *name,
	const struct ms912x_event_rate_stats *stats)
{
	static const char * const bucket_names[] = {
		[MS912X_UPDATE_INTERVAL_LT_8MS] = "lt_8ms",
		[MS912X_UPDATE_INTERVAL_8_17MS] = "8_17ms",
		[MS912X_UPDATE_INTERVAL_17_25MS] = "17_25ms",
		[MS912X_UPDATE_INTERVAL_25_40MS] = "25_40ms",
		[MS912X_UPDATE_INTERVAL_GE_40MS] = "ge_40ms",
	};
	unsigned int i;

	seq_printf(m, "%s_events: %lld\n", name,
		   (long long)atomic64_read(&stats->events));
	seq_printf(m, "%s_interval_samples: %lld\n", name,
		   (long long)atomic64_read(&stats->intervals));
	seq_printf(m, "%s_interval_ns: %lld\n", name,
		   (long long)atomic64_read(&stats->interval_ns));
	for (i = 0; i < ARRAY_SIZE(bucket_names); i++)
		seq_printf(m, "%s_interval_%s: %lld\n", name, bucket_names[i],
			   (long long)atomic64_read(
				   &stats->interval_buckets[i]));
}
