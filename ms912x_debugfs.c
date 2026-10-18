// SPDX-License-Identifier: GPL-2.0-only

#include <linux/kernel.h>
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
	u64 usb_speed, usb_payload, queue_pressure, low_watermark;
	u64 high_watermark, scan_frames, scan_ns, scanned_pixels;
	u64 changed_pixels, bbox_pixels, conversion_pixels, conversion_ns;
	u64 usb_bytes, usb_ns, usb_transfers, wait_frames, wait_ns;
	u64 changed_percent = 0, bbox_percent = 0;
	unsigned int pending_rects;
	bool backpressured;
	enum ms912x_damage_scan_method method;

	mutex_lock(&ms912x->transfer_lock);
	usb_speed = ms912x->usb_speed_mean_bps;
	usb_payload = ms912x->usb_mean_payload_bytes;
	queue_pressure = ms912x->present_queue_pressure_ns;
	low_watermark = ms912x->present_queue_low_watermark_ns;
	high_watermark = ms912x->present_queue_high_watermark_ns;
	backpressured = ms912x->present_backpressured;
	pending_rects = ms912x->pending_rect_count;
	usb_transfers = ms912x->usb_successful_transfers;
	mutex_unlock(&ms912x->transfer_lock);

	scan_frames = atomic64_read(&stats->software_scan_frames);
	scan_ns = atomic64_read(&stats->software_scan_ns);
	scanned_pixels = atomic64_read(&stats->software_scan_pixels);
	changed_pixels = atomic64_read(&stats->software_scan_changed_pixels);
	bbox_pixels = atomic64_read(&stats->software_scan_bbox_pixels);
	if (scanned_pixels) {
		changed_percent = mul_u64_u64_div_u64(changed_pixels, 10000,
						      scanned_pixels);
		bbox_percent = mul_u64_u64_div_u64(bbox_pixels, 10000,
						   scanned_pixels);
	}
	conversion_pixels = atomic64_read(&stats->converted_pixels);
	conversion_ns = atomic64_read(&stats->conversion_ns);
	usb_bytes = atomic64_read(&stats->usb_bytes);
	usb_ns = atomic64_read(&stats->usb_ns);
	wait_frames = atomic64_read(&stats->drm_present_wait_frames);
	wait_ns = atomic64_read(&stats->drm_present_wait_ns);
	method = READ_ONCE(ms912x_parameters.damage_scan_method);

	seq_printf(m, "updates: %lld\n",
		   (long long)atomic64_read(&stats->updates));
	seq_printf(m, "display_refresh_hz: %u\n",
		   READ_ONCE(ms912x->display_refresh_hz));
	seq_printf(m, "damage_scan_mode: %s\n",
		   ms912x_param_damage_scan_mode_name(
			   READ_ONCE(ms912x_parameters.damage_scan)));
	seq_printf(m, "damage_scan_method: %s\n",
		   method == MS912X_DAMAGE_SCAN_AVX2 ? "avx2" :
		   method == MS912X_DAMAGE_SCAN_MEMCMP ? "memcmp" : "scalar");
	seq_printf(m, "damage_scan_tolerance: %u\n",
		   READ_ONCE(ms912x_parameters.damage_scan_tolerance_override) ?
		   READ_ONCE(ms912x_parameters.damage_scan_tolerance) :
		   READ_ONCE(ms912x->damage_scan_tolerance_default));
	seq_printf(m, "damage_scan_stride: %u\n",
		   READ_ONCE(ms912x_parameters.damage_scan_stride_override) ?
		   READ_ONCE(ms912x_parameters.damage_scan_stride) :
		   READ_ONCE(ms912x->damage_scan_stride_default));
	seq_printf(m, "scan_frames: %llu\n", (unsigned long long)scan_frames);
	seq_printf(m, "scan_changed_percent: %llu.%02llu\n",
		   (unsigned long long)(changed_percent / 100),
		   (unsigned long long)(changed_percent % 100));
	seq_printf(m, "scan_bbox_percent: %llu.%02llu\n",
		   (unsigned long long)(bbox_percent / 100),
		   (unsigned long long)(bbox_percent % 100));
	seq_printf(m, "scan_avg_us: %llu\n",
		   (unsigned long long)(scan_frames ?
			div64_u64(scan_ns, scan_frames * NSEC_PER_USEC) : 0));
	seq_printf(m, "scan_rect_overflow_full_updates: %lld\n",
		   (long long)atomic64_read(
			   &stats->scan_rect_overflow_full_updates));
	seq_printf(m, "damage_coalesced_updates: %lld\n",
		   (long long)atomic64_read(&stats->damage_coalesced_updates));
	seq_printf(m, "damage_queue_overflow_full_updates: %lld\n",
		   (long long)atomic64_read(
			   &stats->damage_queue_overflow_full_updates));
	seq_printf(m, "merged_full_updates: %lld\n",
		   (long long)atomic64_read(&stats->merged_full_updates));
	seq_printf(m, "pending_rect_count: %u\n", pending_rects);
	seq_printf(m, "conversion_pixels: %llu\n",
		   (unsigned long long)conversion_pixels);
	seq_printf(m, "conversion_avg_ns_per_pixel: %llu\n",
		   (unsigned long long)(conversion_pixels ?
			div64_u64(conversion_ns, conversion_pixels) : 0));
	seq_printf(m, "usb_transfers: %llu\n", (unsigned long long)usb_transfers);
	seq_printf(m, "usb_bytes: %llu\n", (unsigned long long)usb_bytes);
	seq_printf(m, "usb_avg_payload_bytes: %llu\n",
		   (unsigned long long)(usb_transfers ?
			div64_u64(usb_bytes, usb_transfers) : 0));
	seq_printf(m, "usb_throughput_mbps: %llu.%02llu\n",
		   (unsigned long long)(usb_speed / 1000000),
		   (unsigned long long)((usb_speed % 1000000) / 10000));
	seq_printf(m, "usb_avg_service_ms: %llu.%02llu\n",
		   (unsigned long long)(usb_transfers ?
			div64_u64(usb_ns, usb_transfers * NSEC_PER_MSEC) : 0),
		   (unsigned long long)(usb_transfers ?
			div64_u64(usb_ns, usb_transfers * 10000) % 100 : 0));
	seq_printf(m, "usb_mean_payload_estimate_bytes: %llu\n",
		   (unsigned long long)usb_payload);
	seq_printf(m, "usb_errors: %lld\n",
		   (long long)atomic64_read(&stats->usb_errors));
	seq_printf(m, "usb_timeouts: %lld\n",
		   (long long)atomic64_read(&stats->usb_timeouts));
	seq_printf(m, "usb_short_transfers: %lld\n",
		   (long long)atomic64_read(&stats->usb_short_transfers));
	seq_printf(m, "present_queue_pressure_ms: %llu\n",
		   (unsigned long long)div64_u64(queue_pressure, NSEC_PER_MSEC));
	seq_printf(m, "present_queue_low_watermark_ms: %llu\n",
		   (unsigned long long)div64_u64(low_watermark, NSEC_PER_MSEC));
	seq_printf(m, "present_queue_high_watermark_ms: %llu\n",
		   (unsigned long long)div64_u64(high_watermark, NSEC_PER_MSEC));
	seq_printf(m, "present_backpressured: %s\n",
		   backpressured ? "yes" : "no");
	seq_printf(m, "present_wait_frames: %llu\n",
		   (unsigned long long)wait_frames);
	seq_printf(m, "present_wait_avg_ms: %llu\n",
		   (unsigned long long)(wait_frames ?
			div64_u64(wait_ns, wait_frames * NSEC_PER_MSEC) : 0));
	seq_printf(m, "present_wait_timeouts: %lld\n",
		   (long long)atomic64_read(&stats->drm_present_wait_timeouts));
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
