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
	u64 usb_speed_mean_bps, usb_speed_samples;
	u64 usb_successful_transfers, usb_first_success_ns, usb_last_success_ns;
	u64 usb_mean_fps_milli = 0;

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
	ms912x_stats_show_event_rate(m, "plane_update",
				       &stats->plane_update_rate);
	ms912x_stats_show_event_rate(m, "damage_clip",
				       &stats->damage_clip_rate);
	seq_printf(m, "damage_coalesce_batches: %lld\n",
		   (long long)atomic64_read(
			   &stats->damage_coalesce_batches));
	seq_printf(m, "damage_coalesced_updates: %lld\n",
		   (long long)atomic64_read(
			   &stats->damage_coalesced_updates));
	seq_printf(m, "damage_coalesce_ns: %lld\n",
		   (long long)atomic64_read(&stats->damage_coalesce_ns));
	seq_printf(m, "damage_coalesce_last_ns: %lld\n",
		   (long long)atomic64_read(
			   &stats->damage_coalesce_last_ns));
	seq_printf(m, "damage_coalesce_max_ns: %lld\n",
		   (long long)atomic64_read(
			   &stats->damage_coalesce_max_ns));
	seq_printf(m, "damage_scan_mode: %s\n",
			   ms912x_param_damage_scan_mode_name(
			   READ_ONCE(ms912x_parameters.damage_scan)));
	seq_printf(m, "damage_scan_memcmp: %s\n",
		   READ_ONCE(ms912x_parameters.damage_scan_memcmp) ? "on" : "off");
	seq_printf(m, "damage_scan_x_probe_frames: %u\n",
		   READ_ONCE(ms912x->damage_scan_x_probe_frames));
	seq_printf(m, "damage_scan_x_stable: %s\n",
		   READ_ONCE(ms912x->damage_scan_x_stable) ? "yes" : "no");
	seq_printf(m, "damage_scan_stride: %u\n",
		   READ_ONCE(ms912x_parameters.damage_scan_stride));
	seq_printf(m, "damage_scan_stride_phase: %u\n",
		   READ_ONCE(ms912x->damage_scan_stride_phase));
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
	mutex_lock(&ms912x->transfer_lock);
	usb_speed_mean_bps = ms912x->usb_speed_mean_bps;
	usb_speed_samples = ms912x->usb_speed_samples;
	usb_successful_transfers = ms912x->usb_successful_transfers;
	usb_first_success_ns = ms912x->usb_first_success_ns;
	usb_last_success_ns = ms912x->usb_last_success_ns;
	mutex_unlock(&ms912x->transfer_lock);
	if (usb_successful_transfers > 1 &&
	    usb_last_success_ns > usb_first_success_ns)
		usb_mean_fps_milli = mul_u64_u64_div_u64(
			usb_successful_transfers - 1, NSEC_PER_SEC * 1000ULL,
			usb_last_success_ns - usb_first_success_ns);
	seq_printf(m, "usb_speed_mean_bps: %llu\n",
		   (unsigned long long)usb_speed_mean_bps);
	seq_printf(m, "usb_speed_samples: %llu\n",
		   (unsigned long long)usb_speed_samples);
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
