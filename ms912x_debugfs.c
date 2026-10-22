// SPDX-License-Identifier: GPL-2.0-only

#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/math64.h>
#include <linux/mutex.h>
#include <linux/seq_file.h>

#include <drm/drm_debugfs.h>
#include <drm/drm_device.h>

#include "ms912x.h"

static u64 ms912x_stats_rate_x100(u64 count, u64 duration_ns)
{
	return duration_ns ?
		mul_u64_u64_div_u64(count, 100ULL * NSEC_PER_SEC,
				    duration_ns) : 0;
}

static u64 ms912x_stats_mpix_rate_x100(u64 pixels, u64 duration_ns)
{
	return duration_ns ?
		mul_u64_u64_div_u64(pixels, 100000, duration_ns) : 0;
}

static int ms912x_debugfs_stats_show(struct seq_file *m, void *unused)
{
	struct drm_debugfs_entry *entry = m->private;
	struct ms912x_device *ms912x = to_ms912x(entry->dev);
	struct usb_device *usbdev = interface_to_usbdev(ms912x->intf);
	struct ms912x_stats *stats = &ms912x->stats;
	u64 session_start_ns, session_ns = 0, input_updates;
	u64 usb_speed, queue_pressure, low_watermark, high_watermark;
	u64 scan_frames, scan_ns, scanned_pixels, frame_pixels;
	u64 bbox_pixels, conversion_attempts;
	u64 rect_candidates, rect_candidates_max, rects_output, rects_output_max;
	u64 rect_candidates_avg_x100, rects_output_avg_x100;
	u64 conversion_successes, conversion_pixels, conversion_ns;
	u64 usb_bytes, usb_ns, usb_requests, usb_transfers, wait_frames, wait_ns;
	u64 input_rate_x100, input_period_ms_x100, usb_rate_x100;
	u64 usb_period_ms_x100, scan_mpix_rate_x100;
	u64 conversion_mpix_rate_x100, usb_mbps_x100, usb_session_mbps_x100;
	u64 bbox_percent = 0;
	unsigned int pending_rects, scan_rect_vector_capacity;
	unsigned int cursor_width, cursor_height;
	int cursor_x, cursor_y;
	bool cursor_enabled, primary_active, primary_rgb565;
	unsigned int scale_setting, requested_scale;
	char requested_scale_name[8];
	bool backpressured;
	enum ms912x_damage_scan_method method;
	u64 now_ns = ktime_get_ns();
	u16 vendor = le16_to_cpu(usbdev->descriptor.idVendor);
	u16 product = le16_to_cpu(usbdev->descriptor.idProduct);

	session_start_ns = atomic64_read(&stats->session_start_ns);
	if (session_start_ns && now_ns >= session_start_ns)
		session_ns = now_ns - session_start_ns;
	input_updates = atomic64_read(&stats->updates);
	scan_frames = atomic64_read(&stats->software_scan_frames);
	scan_ns = atomic64_read(&stats->software_scan_ns);
	scanned_pixels = atomic64_read(&stats->software_scan_compared_pixels);
	frame_pixels = atomic64_read(&stats->software_scan_frame_pixels);
	bbox_pixels = atomic64_read(&stats->software_scan_bbox_pixels);
	rect_candidates = atomic64_read(&stats->software_scan_rect_add_calls);
	rect_candidates_max = atomic64_read(
		&stats->software_scan_rect_add_max_calls);
	rects_output = atomic64_read(&stats->software_scan_rectangles);
	rects_output_max = atomic64_read(&stats->software_scan_rectangles_max);
	conversion_attempts = atomic64_read(&stats->conversion_attempts);
	conversion_successes = atomic64_read(&stats->conversion_successes);
	conversion_pixels = atomic64_read(&stats->conversion_pixels);
	conversion_ns = atomic64_read(&stats->conversion_ns);
	usb_requests = atomic64_read(&stats->usb_requests);
	usb_bytes = atomic64_read(&stats->usb_bytes);
	usb_ns = atomic64_read(&stats->usb_ns);
	wait_frames = atomic64_read(&stats->drm_present_wait_frames);
	wait_ns = atomic64_read(&stats->drm_present_wait_ns);

	mutex_lock(&ms912x->transfer_lock);
	usb_speed = ms912x->usb_speed_mean_bytes_per_sec;
	queue_pressure = ms912x->present_queue_pressure_ns;
	low_watermark = ms912x->present_queue_low_watermark_ns;
	high_watermark = ms912x->present_queue_high_watermark_ns;
	backpressured = ms912x->present_backpressured;
	scale_setting = ms912x_scale_setting_for_mode(ms912x->scale_mode);
	requested_scale = READ_ONCE(ms912x->scale_is_overridden) ?
		READ_ONCE(ms912x->scale_override_setting) :
		READ_ONCE(ms912x_parameters.scale);
	if (requested_scale == MS912X_SCALE_AUTO)
		strscpy(requested_scale_name, "auto",
			sizeof(requested_scale_name));
	else
		scnprintf(requested_scale_name, sizeof(requested_scale_name), "%u",
			  requested_scale);
	pending_rects = ms912x->pending_rect_count;
	cursor_enabled = ms912x->cursor_enabled;
	cursor_x = ms912x->cursor_x;
	cursor_y = ms912x->cursor_y;
	cursor_width = ms912x->cursor_width;
	cursor_height = ms912x->cursor_height;
	primary_active = READ_ONCE(ms912x->primary_plane_active);
	primary_rgb565 = READ_ONCE(ms912x->video_in_rgb565);
	usb_transfers = ms912x->usb_successful_transfers;
	mutex_unlock(&ms912x->transfer_lock);
	mutex_lock(&ms912x->damage_scan_lock);
	scan_rect_vector_capacity = ms912x->scan_damage_rect_capacity;
	mutex_unlock(&ms912x->damage_scan_lock);

	if (frame_pixels) {
		bbox_percent = mul_u64_u64_div_u64(bbox_pixels, 10000,
						   frame_pixels);
	}
	input_rate_x100 = ms912x_stats_rate_x100(input_updates, session_ns);
	input_period_ms_x100 = input_updates ?
		div64_u64(div64_u64(session_ns, input_updates), 10000) : 0;
	usb_rate_x100 = ms912x_stats_rate_x100(usb_transfers, session_ns);
	usb_period_ms_x100 = usb_transfers ?
		div64_u64(div64_u64(session_ns, usb_transfers), 10000) : 0;
	scan_mpix_rate_x100 =
		ms912x_stats_mpix_rate_x100(scanned_pixels, scan_ns);
	conversion_mpix_rate_x100 =
		ms912x_stats_mpix_rate_x100(conversion_pixels, conversion_ns);
	rect_candidates_avg_x100 = scan_frames ?
		mul_u64_u64_div_u64(rect_candidates, 100, scan_frames) : 0;
	rects_output_avg_x100 = scan_frames ?
		mul_u64_u64_div_u64(rects_output, 100, scan_frames) : 0;
	usb_mbps_x100 = mul_u64_u64_div_u64(usb_speed, 800, 1000000);
	usb_session_mbps_x100 = session_ns ?
		mul_u64_u64_div_u64(usb_bytes, 800000, session_ns) : 0;
	method = READ_ONCE(ms912x_parameters.damage_scan_method);

	seq_printf(m, "usb_device_path: /sys/bus/usb/devices/%s\n",
		   dev_name(&usbdev->dev));
	seq_printf(m, "usb_interface_path: /sys/bus/usb/devices/%s\n",
		   dev_name(&ms912x->intf->dev));
	seq_printf(m, "usb_id: %04x:%04x\n", vendor, product);
	seq_printf(m, "usb_link_speed: %s\n",
		   ms912x_usb_speed_name(usbdev->speed));
	if (usbdev->serial && usbdev->serial[0])
		seq_printf(m, "usb_serial: %s\n", usbdev->serial);
	seq_printf(m, "session_duration_s: %llu.%03llu\n",
		   (unsigned long long)div64_u64(session_ns, NSEC_PER_SEC),
		   (unsigned long long)(div64_u64(session_ns, NSEC_PER_MSEC) % 1000));
	seq_printf(m, "input_updates: %llu\n",
		   (unsigned long long)input_updates);
	seq_printf(m, "input_updates_per_s: %llu.%02llu\n",
		   (unsigned long long)(input_rate_x100 / 100),
		   (unsigned long long)(input_rate_x100 % 100));
	seq_printf(m, "input_update_period_ms: %llu.%02llu\n",
		   (unsigned long long)(input_period_ms_x100 / 100),
		   (unsigned long long)(input_period_ms_x100 % 100));
	seq_printf(m, "display_refresh_hz: %u\n",
		   READ_ONCE(ms912x->display_refresh_hz));
	seq_printf(m, "primary_plane_format: %s\n",
		   !primary_active ? "inactive" :
		   primary_rgb565 ? "RGB565" : "XRGB8888");
	seq_printf(m, "pixel_payload_format: %s\n",
		   !primary_active ? "inactive" :
		   primary_rgb565 ? "RGB565" : "UYVY");
	seq_printf(m, "damage_scan_mode: %s\n",
		   ms912x_param_damage_scan_mode_name(
			   READ_ONCE(ms912x->damage_scan_mode_is_overridden) ?
			   READ_ONCE(ms912x->damage_scan_mode_override) :
			   READ_ONCE(ms912x_parameters.damage_scan_override) ?
			   READ_ONCE(ms912x_parameters.damage_scan) :
			   READ_ONCE(ms912x->damage_scan_mode_default)));
	seq_printf(m, "effective_profile: %s\n",
		   ms912x_profile_name(
			   READ_ONCE(ms912x->effective_profile)));
	seq_printf(m, "profile_source: %s\n",
		   READ_ONCE(ms912x->profile_override) ? "adapter" : "module");
	seq_printf(m, "fake_edid_policy: %s (%s)\n",
		   ms912x_fake_edid_mode_name(
			   ms912x_fake_edid_mode_for_device(ms912x)),
		   READ_ONCE(ms912x->fake_edid_override) ? "adapter" : "module");
	seq_printf(m, "force_connected_policy: %s (%s)\n",
		   ms912x_force_connected_mode_name(
			   ms912x_force_connected_mode_for_device(ms912x)),
		   READ_ONCE(ms912x->force_connected_override) ?
		   "adapter" : "module");
	seq_printf(m, "damage_scan_method: %s\n",
		   ms912x_param_damage_scan_method_name(method));
	seq_printf(m, "damage_scan_backend: %s\n",
		   ms912x_param_damage_scan_method_name(
			   READ_ONCE(ms912x->damage_scan_backend)));
	seq_printf(m, "fast_chroma_backend: %s\n",
		   ms912x_fast_chroma_selected_backend_name());
	seq_printf(m, "damage_scan_tolerance: %u\n",
		   ms912x_damage_scan_effective_tolerance(ms912x));
	seq_printf(m, "damage_scan_stride: %u\n",
		   READ_ONCE(ms912x->damage_scan_stride_is_overridden) ?
		   READ_ONCE(ms912x->damage_scan_stride_override_value) :
		   READ_ONCE(ms912x_parameters.damage_scan_stride_override) ?
		   READ_ONCE(ms912x_parameters.damage_scan_stride) :
		   READ_ONCE(ms912x->damage_scan_stride_default));
	seq_printf(m, "scan_frames: %llu\n", (unsigned long long)scan_frames);
	seq_printf(m, "scan_compared_pixels: %llu\n",
		   (unsigned long long)scanned_pixels);
	seq_printf(m, "scan_compared_mpixels_per_s: %llu.%02llu\n",
		   (unsigned long long)(scan_mpix_rate_x100 / 100),
		   (unsigned long long)(scan_mpix_rate_x100 % 100));
	seq_printf(m, "scan_damage_bbox_of_frame_percent: %llu.%02llu\n",
		   (unsigned long long)(bbox_percent / 100),
		   (unsigned long long)(bbox_percent % 100));
	seq_printf(m, "scan_avg_duration_us: %llu\n",
		   (unsigned long long)(scan_frames ?
			div64_u64(scan_ns, scan_frames * NSEC_PER_USEC) : 0));
	seq_printf(m, "scan_rect_candidates_total: %llu\n",
		   (unsigned long long)rect_candidates);
	seq_printf(m, "scan_rect_candidates_avg_per_frame: %llu.%02llu\n",
		   (unsigned long long)(rect_candidates_avg_x100 / 100),
		   (unsigned long long)(rect_candidates_avg_x100 % 100));
	seq_printf(m, "scan_rect_candidates_max_per_frame: %llu\n",
		   (unsigned long long)rect_candidates_max);
	seq_printf(m, "scan_rectangles_output_total: %llu\n",
		   (unsigned long long)rects_output);
	seq_printf(m, "scan_rectangles_output_avg_per_frame: %llu.%02llu\n",
		   (unsigned long long)(rects_output_avg_x100 / 100),
		   (unsigned long long)(rects_output_avg_x100 % 100));
	seq_printf(m, "scan_rectangles_output_max_per_frame: %llu\n",
		   (unsigned long long)rects_output_max);
	seq_printf(m, "scan_rect_vector_capacity: %u\n",
		   scan_rect_vector_capacity);
	seq_printf(m, "scan_rect_vector_limit: %u\n",
		   MS912X_MAX_DAMAGE_RECTS);
	seq_printf(m, "scan_rect_overflow_merged_updates: %llu\n",
		   (unsigned long long)atomic64_read(
			   &stats->scan_rect_overflow_merged_updates));
	seq_putc(m, '\n');
	seq_printf(m, "damage_batched_updates: %llu\n",
		   (unsigned long long)atomic64_read(
			   &stats->damage_batched_updates));
	seq_printf(m, "damage_queue_overflow_merged_updates: %llu\n",
		   (unsigned long long)atomic64_read(
			   &stats->damage_queue_overflow_merged_updates));
	seq_printf(m, "pending_rect_count: %u\n", pending_rects);
	seq_putc(m, '\n');
	seq_printf(m, "conversion_attempts: %llu\n",
		   (unsigned long long)conversion_attempts);
	seq_printf(m, "conversion_successes: %llu\n",
		   (unsigned long long)conversion_successes);
	seq_printf(m, "conversion_pixels: %llu\n",
		   (unsigned long long)conversion_pixels);
	seq_printf(m, "conversion_mpixels_per_s: %llu.%02llu\n",
		   (unsigned long long)(conversion_mpix_rate_x100 / 100),
		   (unsigned long long)(conversion_mpix_rate_x100 % 100));
	seq_printf(m, "conversion_avg_us: %llu\n",
		   (unsigned long long)(conversion_attempts ?
			div64_u64(conversion_ns,
				  conversion_attempts * NSEC_PER_USEC) : 0));
	seq_putc(m, '\n');
	seq_printf(m, "usb_requests: %llu\n",
		   (unsigned long long)usb_requests);
	seq_printf(m, "usb_successful_transfers: %llu\n",
		   (unsigned long long)usb_transfers);
	seq_printf(m, "usb_transfers_per_s: %llu.%02llu\n",
		   (unsigned long long)(usb_rate_x100 / 100),
		   (unsigned long long)(usb_rate_x100 % 100));
	seq_printf(m, "usb_transfer_period_ms: %llu.%02llu\n",
		   (unsigned long long)(usb_period_ms_x100 / 100),
		   (unsigned long long)(usb_period_ms_x100 % 100));
	seq_printf(m, "usb_bytes: %llu\n", (unsigned long long)usb_bytes);
	seq_printf(m, "usb_active_payload_mbps: %llu.%02llu\n",
		   (unsigned long long)(usb_mbps_x100 / 100),
		   (unsigned long long)(usb_mbps_x100 % 100));
	seq_printf(m, "usb_session_payload_mbps: %llu.%02llu\n",
		   (unsigned long long)(usb_session_mbps_x100 / 100),
		   (unsigned long long)(usb_session_mbps_x100 % 100));
	seq_printf(m, "usb_request_avg_ms: %llu.%02llu\n",
		   (unsigned long long)(usb_requests ?
			div64_u64(usb_ns, usb_requests * 10000) / 100 : 0),
		   (unsigned long long)(usb_requests ?
			div64_u64(usb_ns, usb_requests * 10000) % 100 : 0));
	seq_printf(m, "usb_errors: %llu\n",
		   (unsigned long long)atomic64_read(&stats->usb_errors));
	seq_printf(m, "usb_timeouts: %llu\n",
		   (unsigned long long)atomic64_read(&stats->usb_timeouts));
	seq_printf(m, "usb_short_transfers: %llu\n",
		   (unsigned long long)atomic64_read(&stats->usb_short_transfers));
	seq_printf(m, "damage_replay_batches_queued: %llu\n",
		   (unsigned long long)atomic64_read(
			   &stats->damage_replay_batches_queued));
	seq_printf(m, "damage_replay_batches_sent: %llu\n",
		   (unsigned long long)atomic64_read(
			   &stats->damage_replay_batches_sent));
	seq_printf(m, "usb_requests_requeued_before_send: %llu\n",
		   (unsigned long long)atomic64_read(
			   &stats->usb_requests_requeued_before_send));
	seq_putc(m, '\n');
	seq_printf(m, "present_queue_pressure_ms: %llu\n",
		   (unsigned long long)div64_u64(queue_pressure, NSEC_PER_MSEC));
	seq_printf(m, "present_queue_low_watermark_ms: %llu\n",
		   (unsigned long long)div64_u64(low_watermark, NSEC_PER_MSEC));
	seq_printf(m, "present_queue_high_watermark_ms: %llu\n",
		   (unsigned long long)div64_u64(high_watermark, NSEC_PER_MSEC));
	seq_printf(m, "present_backpressured: %s\n",
		   backpressured ? "yes" : "no");
	/* Keep the existing key/value for readers that consume this debugfs file. */
	seq_printf(m, "scale_percent: %u\n", scale_setting);
	seq_printf(m, "scale_request: %s (%s)\n", requested_scale_name,
		   READ_ONCE(ms912x->scale_is_overridden) ? "adapter" : "module");
	seq_printf(m, "present_wait_frames: %llu\n",
		   (unsigned long long)wait_frames);
	seq_printf(m, "present_wait_avg_ms: %llu\n",
		   (unsigned long long)(wait_frames ?
			div64_u64(wait_ns, wait_frames * NSEC_PER_MSEC) : 0));
	seq_printf(m, "present_wait_timeouts: %llu\n",
		   (unsigned long long)atomic64_read(
			   &stats->drm_present_wait_timeouts));
	seq_printf(m, "cursor_plane_updates: %llu\n",
		   (unsigned long long)atomic64_read(&stats->cursor_plane_updates));
	seq_printf(m, "cursor_state: %s %ux%u at %d,%d\n",
		   cursor_enabled ? "enabled" : "disabled", cursor_width,
		   cursor_height, cursor_x, cursor_y);
	seq_printf(m, "cursor_composite_packets: %llu\n",
		   (unsigned long long)atomic64_read(
			   &stats->cursor_composite_packets));
	seq_printf(m, "cursor_composite_pixels: %llu\n",
		   (unsigned long long)atomic64_read(
			   &stats->cursor_composite_pixels));
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
