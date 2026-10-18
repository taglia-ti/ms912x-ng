// SPDX-License-Identifier: GPL-2.0-only

#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/math64.h>
#include <linux/mutex.h>
#include <linux/seq_file.h>

#include <drm/drm_debugfs.h>
#include <drm/drm_device.h>

#include "ms912x.h"

static int ms912x_debugfs_stats_show(struct seq_file *m, void *unused)
{
	struct drm_debugfs_entry *entry = m->private;
	struct ms912x_device *ms912x = to_ms912x(entry->dev);
	struct ms912x_stats *stats = &ms912x->stats;
	u64 usb_speed_mean_bps, usb_speed_samples, usb_mean_payload_bytes;
	u64 usb_successful_transfers, usb_first_success_ns, usb_last_success_ns;
	u64 present_queue_pressure_ns, present_queue_low_watermark_ns;
	u64 present_queue_high_watermark_ns;
	u64 usb_mean_fps_milli = 0;
	u64 frame_sequence, published_frame_sequence;
	enum ms912x_damage_scan_method scan_method;
	unsigned int pending_rect_count, queued_scan_frames = 0;
	unsigned int running_scan_frames = 0, i;
	bool transfer_stopping, scan_stopping, scan_work_scheduled;
	bool present_backpressured;

	seq_printf(m, "updates: %lld\n",
		   (long long)atomic64_read(&stats->updates));
	seq_printf(m, "no_damage_updates: %lld\n",
		   (long long)atomic64_read(&stats->no_damage_updates));
	seq_printf(m, "damage_updates: %lld\n",
		   (long long)atomic64_read(&stats->damage_updates));
	seq_printf(m, "damage_pixels: %lld\n",
		   (long long)atomic64_read(&stats->damage_pixels));
	seq_printf(m, "damage_full_updates: %lld\n",
		   (long long)atomic64_read(&stats->damage_full_updates));
	seq_printf(m, "forced_full_updates: %lld\n",
		   (long long)atomic64_read(&stats->forced_full_updates));
	seq_printf(m, "modeset_updates: %lld\n",
		   (long long)atomic64_read(&stats->modeset_updates));
	seq_printf(m, "framebuffer_swaps: %lld\n",
		   (long long)atomic64_read(&stats->framebuffer_swaps));
	seq_printf(m, "fb_swaps_with_clips: %lld\n",
		   (long long)atomic64_read(&stats->fb_swaps_with_clips));
	seq_printf(m, "fb_swaps_without_clips: %lld\n",
		   (long long)atomic64_read(&stats->fb_swaps_without_clips));
	seq_printf(m, "damage_clip_rects: %lld\n",
		   (long long)atomic64_read(&stats->damage_clip_rects));
	seq_printf(m, "display_refresh_hz: %u\n",
		   READ_ONCE(ms912x->display_refresh_hz));
	seq_printf(m, "display_status_raw: %d\n",
		   READ_ONCE(ms912x->detect_status_raw));
	seq_printf(m, "display_status_confirm: %d\n",
		   READ_ONCE(ms912x->detect_status_confirm));
	seq_printf(m, "display_edid_probe: %d\n",
		   READ_ONCE(ms912x->detect_edid_probe));
	seq_printf(m, "fast_chroma: %u\n",
		   READ_ONCE(ms912x_parameters.fast_chroma));
	mutex_lock(&ms912x->transfer_lock);
	transfer_stopping = ms912x->transfer_stopping;
	pending_rect_count = ms912x->pending_rect_count;
	frame_sequence = ms912x->frame_sequence;
	published_frame_sequence = ms912x->published_frame_sequence;
	usb_speed_mean_bps = ms912x->usb_speed_mean_bps;
	usb_speed_samples = ms912x->usb_speed_samples;
	usb_mean_payload_bytes = ms912x->usb_mean_payload_bytes;
	usb_successful_transfers = ms912x->usb_successful_transfers;
	usb_first_success_ns = ms912x->usb_first_success_ns;
	usb_last_success_ns = ms912x->usb_last_success_ns;
	present_queue_pressure_ns = ms912x->present_queue_pressure_ns;
	present_queue_low_watermark_ns =
		ms912x->present_queue_low_watermark_ns;
	present_queue_high_watermark_ns =
		ms912x->present_queue_high_watermark_ns;
	present_backpressured = ms912x->present_backpressured;
	mutex_lock(&ms912x->scan_queue_lock);
	scan_stopping = ms912x->scan_stopping;
	scan_work_scheduled = ms912x->scan_work_scheduled;
	for (i = 0; i < ARRAY_SIZE(ms912x->scan_frames); i++) {
		queued_scan_frames += ms912x->scan_frames[i].queued;
		running_scan_frames += ms912x->scan_frames[i].running;
	}
	mutex_unlock(&ms912x->scan_queue_lock);
	mutex_unlock(&ms912x->transfer_lock);
	seq_printf(m, "frame_sequence: %llu\n",
		   (unsigned long long)frame_sequence);
	seq_printf(m, "published_frame_sequence: %llu\n",
		   (unsigned long long)published_frame_sequence);
	seq_printf(m, "scratch_generation: %llu\n",
		   (unsigned long long)READ_ONCE(ms912x->scratch_generation));
	seq_printf(m, "frame_epoch: %u\n", READ_ONCE(ms912x->frame_generation));
	seq_printf(m, "pending_rect_count: %u\n", pending_rect_count);
	seq_printf(m, "scan_queue_queued: %u\n", queued_scan_frames);
	seq_printf(m, "scan_queue_running: %u\n", running_scan_frames);
	seq_printf(m, "scan_work_scheduled: %u\n", scan_work_scheduled);
	seq_printf(m, "scan_stopping: %u\n", scan_stopping);
	seq_printf(m, "transfer_stopping: %u\n", transfer_stopping);
	seq_printf(m, "scan_frames_enqueued: %lld\n",
		   (long long)atomic64_read(&stats->scan_frames_enqueued));
	seq_printf(m, "scan_frames_replaced: %lld\n",
		   (long long)atomic64_read(&stats->scan_frames_replaced));
	seq_printf(m, "scan_queue_full: %lld\n",
		   (long long)atomic64_read(&stats->scan_queue_full));
	seq_printf(m, "scan_work_queue_false: %lld\n",
		   (long long)atomic64_read(&stats->scan_work_queue_false));
	seq_printf(m, "scan_worker_frames: %lld\n",
		   (long long)atomic64_read(&stats->scan_worker_frames));
	seq_printf(m, "scan_frames_published: %lld\n",
		   (long long)atomic64_read(&stats->scan_frames_published));
	seq_printf(m, "scan_epoch_dropped: %lld\n",
		   (long long)atomic64_read(&stats->scan_epoch_dropped));
	seq_printf(m, "scan_publish_rejected: %lld\n",
		   (long long)atomic64_read(&stats->scan_publish_rejected));
	seq_printf(m, "scan_stop_rejected: %lld\n",
		   (long long)atomic64_read(&stats->scan_stop_rejected));
	seq_printf(m, "submit_stopping: %lld\n",
		   (long long)atomic64_read(&stats->submit_stopping));
	seq_printf(m, "usb_stale_payload_refreshed: %lld\n",
		   (long long)atomic64_read(&stats->usb_stale_payload_refreshed));
	ms912x_stats_show_event_rate(m, "plane_update",
				       &stats->plane_update_rate);
	ms912x_stats_show_event_rate(m, "damage_clip",
				       &stats->damage_clip_rate);
	seq_printf(m, "damage_coalesce_batches: %lld\n",
		   (long long)atomic64_read(
			   &stats->damage_coalesce_batches));
	seq_printf(m, "damage_coalesce_calls: %lld\n",
		   (long long)atomic64_read(&stats->damage_coalesce_calls));
	seq_printf(m, "damage_coalesced_updates: %lld\n",
		   (long long)atomic64_read(
			   &stats->damage_coalesced_updates));
	seq_printf(m, "damage_queue_overflow_full_updates: %lld\n",
		   (long long)atomic64_read(
			   &stats->damage_queue_overflow_full_updates));
	seq_printf(m, "damage_coalesce_exec_ns: %lld\n",
		   (long long)atomic64_read(&stats->damage_coalesce_exec_ns));
	seq_printf(m, "damage_coalesce_exec_last_ns: %lld\n",
		   (long long)atomic64_read(
			   &stats->damage_coalesce_exec_last_ns));
	seq_printf(m, "damage_coalesce_exec_max_ns: %lld\n",
		   (long long)atomic64_read(
			   &stats->damage_coalesce_exec_max_ns));
	seq_printf(m, "damage_scan_mode: %s\n",
			   ms912x_param_damage_scan_mode_name(
			   READ_ONCE(ms912x_parameters.damage_scan)));
	scan_method = READ_ONCE(ms912x_parameters.damage_scan_method);
	seq_printf(m, "damage_scan_method: %s\n",
		   scan_method == MS912X_DAMAGE_SCAN_AVX2 ? "avx2" :
		   scan_method == MS912X_DAMAGE_SCAN_MEMCMP ? "memcmp" : "scalar");
	seq_printf(m, "damage_scan_stride: %u\n",
		   READ_ONCE(ms912x_parameters.damage_scan_stride));
	seq_printf(m, "damage_scan_x_probe_done: %s\n",
		   READ_ONCE(ms912x->damage_scan_x_probe_done) ? "yes" : "no");
	seq_printf(m, "damage_scan_x_stable: %s\n",
		   READ_ONCE(ms912x->damage_scan_x_stable) ? "yes" : "no");
	seq_printf(m, "conversion_attempts: %lld\n",
		   (long long)atomic64_read(&stats->conversion_attempts));
	seq_printf(m, "conversion_frames: %lld\n",
		   (long long)atomic64_read(&stats->conversion_frames));
	seq_printf(m, "conversion_last_pixels: %lld\n",
		   (long long)atomic64_read(&stats->conversion_last_pixels));
	seq_printf(m, "conversion_last_ns: %lld\n",
		   (long long)atomic64_read(&stats->conversion_last_ns));
	seq_printf(m, "conversion_max_ns: %lld\n",
		   (long long)atomic64_read(&stats->conversion_max_ns));
	seq_printf(m, "conversion_profile_frames: %lld\n",
		   (long long)atomic64_read(
			   &stats->conversion_profile_frames));
	seq_printf(m, "conversion_color_ns: %lld\n",
		   (long long)atomic64_read(&stats->conversion_color_ns));
	seq_printf(m, "async_snapshot_copy_ns: %lld\n",
		   (long long)atomic64_read(&stats->async_snapshot_copy_ns));
	seq_printf(m, "software_scan_frames: %lld\n",
		   (long long)atomic64_read(&stats->software_scan_frames));
	seq_printf(m, "software_scan_no_change: %lld\n",
		   (long long)atomic64_read(&stats->software_scan_no_change));
	seq_printf(m, "software_scan_memcmp_frames: %lld\n",
		   (long long)atomic64_read(&stats->software_scan_memcmp_frames));
	seq_printf(m, "software_scan_memcmp_rows: %lld\n",
		   (long long)atomic64_read(&stats->software_scan_memcmp_rows));
	seq_printf(m, "software_scan_memcmp_equal_rows: %lld\n",
		   (long long)atomic64_read(&stats->software_scan_memcmp_equal_rows));
	seq_printf(m, "software_scan_memcmp_blocks: %lld\n",
		   (long long)atomic64_read(&stats->software_scan_memcmp_blocks));
	seq_printf(m, "software_scan_memcmp_equal_blocks: %lld\n",
		   (long long)atomic64_read(&stats->software_scan_memcmp_equal_blocks));
	seq_printf(m, "software_scan_avx2_frames: %lld\n",
		   (long long)atomic64_read(&stats->software_scan_avx2_frames));
	seq_printf(m, "software_scan_avx2_rows: %lld\n",
		   (long long)atomic64_read(&stats->software_scan_avx2_rows));
	seq_printf(m, "software_scan_avx2_blocks: %lld\n",
		   (long long)atomic64_read(&stats->software_scan_avx2_blocks));
	seq_printf(m, "software_scan_avx2_equal_blocks: %lld\n",
		   (long long)atomic64_read(&stats->software_scan_avx2_equal_blocks));
	seq_printf(m, "software_scan_pixels: %lld\n",
		   (long long)atomic64_read(&stats->software_scan_pixels));
	seq_printf(m, "software_scan_changed_pixels: %lld\n",
		   (long long)atomic64_read(&stats->software_scan_changed_pixels));
	seq_printf(m, "software_scan_bbox_pixels: %lld\n",
		   (long long)atomic64_read(&stats->software_scan_bbox_pixels));
	seq_printf(m, "software_scan_ns: %lld\n",
		   (long long)atomic64_read(&stats->software_scan_ns));
	seq_printf(m, "software_scan_last_ns: %lld\n",
		   (long long)atomic64_read(&stats->software_scan_last_ns));
	seq_printf(m, "software_scan_max_ns: %lld\n",
		   (long long)atomic64_read(&stats->software_scan_max_ns));
	seq_printf(m, "software_scan_copy_ns: %lld\n",
		   (long long)atomic64_read(&stats->software_scan_copy_ns));
	seq_printf(m, "software_scan_copy_last_ns: %lld\n",
		   (long long)atomic64_read(&stats->software_scan_copy_last_ns));
	seq_printf(m, "software_scan_copy_max_ns: %lld\n",
		   (long long)atomic64_read(&stats->software_scan_copy_max_ns));
	seq_printf(m, "software_scan_compare_ns: %lld\n",
		   (long long)atomic64_read(&stats->software_scan_compare_ns));
	seq_printf(m, "software_scan_compare_last_ns: %lld\n",
		   (long long)atomic64_read(&stats->software_scan_compare_last_ns));
	seq_printf(m, "software_scan_compare_max_ns: %lld\n",
		   (long long)atomic64_read(&stats->software_scan_compare_max_ns));
	seq_printf(m, "software_scan_rect_merge_ns: %lld\n",
		   (long long)atomic64_read(&stats->software_scan_rect_merge_ns));
	seq_printf(m, "software_scan_rect_merge_last_ns: %lld\n",
		   (long long)atomic64_read(
			   &stats->software_scan_rect_merge_last_ns));
	seq_printf(m, "software_scan_rect_merge_max_ns: %lld\n",
		   (long long)atomic64_read(
			   &stats->software_scan_rect_merge_max_ns));
	seq_printf(m, "software_scan_rect_add_calls: %lld\n",
		   (long long)atomic64_read(&stats->software_scan_rect_add_calls));
	seq_printf(m, "scan_rect_overflow_full_updates: %lld\n",
		   (long long)atomic64_read(
			   &stats->scan_rect_overflow_full_updates));
	seq_printf(m, "merged_full_updates: %lld\n",
		   (long long)atomic64_read(&stats->merged_full_updates));
	seq_printf(m, "converted_pixels: %lld\n",
		   (long long)atomic64_read(&stats->converted_pixels));
	seq_printf(m, "conversion_ns: %lld\n",
		   (long long)atomic64_read(&stats->conversion_ns));
	seq_printf(m, "usb_requests: %lld\n",
		   (long long)atomic64_read(&stats->usb_requests));
	seq_printf(m, "queued_bytes: %lld\n",
		   (long long)atomic64_read(&stats->queued_bytes));
	seq_printf(m, "usb_bytes: %lld\n",
		   (long long)atomic64_read(&stats->usb_bytes));
	seq_printf(m, "usb_ns: %lld\n",
		   (long long)atomic64_read(&stats->usb_ns));
	seq_printf(m, "usb_last_ns: %lld\n",
		   (long long)atomic64_read(&stats->usb_last_ns));
	seq_printf(m, "usb_max_ns: %lld\n",
		   (long long)atomic64_read(&stats->usb_max_ns));
	seq_printf(m, "usb_last_bytes: %lld\n",
		   (long long)atomic64_read(&stats->usb_last_bytes));
	seq_printf(m, "workqueue_wait_ns: %lld\n",
		   (long long)atomic64_read(&stats->workqueue_wait_ns));
	seq_printf(m, "workqueue_wait_last_ns: %lld\n",
		   (long long)atomic64_read(&stats->workqueue_wait_last_ns));
	seq_printf(m, "workqueue_wait_max_ns: %lld\n",
		   (long long)atomic64_read(&stats->workqueue_wait_max_ns));
	seq_printf(m, "usb_errors: %lld\n",
		   (long long)atomic64_read(&stats->usb_errors));
	seq_printf(m, "usb_timeouts: %lld\n",
		   (long long)atomic64_read(&stats->usb_timeouts));
	seq_printf(m, "usb_short_transfers: %lld\n",
		   (long long)atomic64_read(&stats->usb_short_transfers));
	seq_printf(m, "usb_last_error: %lld\n",
		   (long long)atomic64_read(&stats->usb_last_error));
	seq_printf(m, "usb_control_retries: %lld\n",
		   (long long)atomic64_read(&stats->usb_control_retries));
	seq_printf(m, "settle_replays: %lld\n",
		   (long long)atomic64_read(&stats->settle_replays));
	seq_printf(m, "drm_present_wait_frames: %lld\n",
		   (long long)atomic64_read(&stats->drm_present_wait_frames));
	seq_printf(m, "drm_present_wait_ns: %lld\n",
		   (long long)atomic64_read(&stats->drm_present_wait_ns));
	seq_printf(m, "drm_present_wait_last_ns: %lld\n",
		   (long long)atomic64_read(&stats->drm_present_wait_last_ns));
	seq_printf(m, "drm_present_wait_max_ns: %lld\n",
		   (long long)atomic64_read(&stats->drm_present_wait_max_ns));
	seq_printf(m, "drm_present_wait_timeouts: %lld\n",
		   (long long)atomic64_read(&stats->drm_present_wait_timeouts));
	if (usb_successful_transfers > 1 &&
	    usb_last_success_ns > usb_first_success_ns)
		usb_mean_fps_milli = mul_u64_u64_div_u64(
			usb_successful_transfers - 1, NSEC_PER_SEC * 1000ULL,
			usb_last_success_ns - usb_first_success_ns);
	seq_printf(m, "usb_speed_mean_bps: %llu\n",
		   (unsigned long long)usb_speed_mean_bps);
	seq_printf(m, "usb_speed_samples: %llu\n",
		   (unsigned long long)usb_speed_samples);
	seq_printf(m, "usb_mean_payload_bytes: %llu\n",
		   (unsigned long long)usb_mean_payload_bytes);
	seq_printf(m, "present_queue_pressure_ns: %llu\n",
		   (unsigned long long)present_queue_pressure_ns);
	seq_printf(m, "present_queue_low_watermark_ns: %llu\n",
		   (unsigned long long)present_queue_low_watermark_ns);
	seq_printf(m, "present_queue_high_watermark_ns: %llu\n",
		   (unsigned long long)present_queue_high_watermark_ns);
	seq_printf(m, "present_backpressured: %s\n",
		   present_backpressured ? "yes" : "no");
	seq_printf(m, "usb_successful_transfers: %llu\n",
		   (unsigned long long)usb_successful_transfers);
	seq_printf(m, "usb_mean_fps: %llu.%03llu\n",
		   (unsigned long long)(usb_mean_fps_milli / 1000),
		   (unsigned long long)(usb_mean_fps_milli % 1000));
	return 0;
}

static const struct drm_debugfs_info ms912x_debugfs_files[] = {
	{
		.name = "ms912x_stats",
		.show = ms912x_debugfs_stats_show,
	},
};

void ms912x_debugfs_register(struct drm_device *drm)
{
	if (!READ_ONCE(ms912x_parameters.debugfs_enabled))
		return;

	drm_debugfs_add_files(drm, ms912x_debugfs_files,
			      ARRAY_SIZE(ms912x_debugfs_files));
}
