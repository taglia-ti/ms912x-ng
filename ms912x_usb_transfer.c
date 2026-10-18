// SPDX-License-Identifier: GPL-2.0-only

#include <linux/container_of.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/math64.h>
#include <linux/mm.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/usb.h>
#include <linux/vmalloc.h>
#include <linux/workqueue.h>

#include <drm/drm_drv.h>
#include <drm/drm_print.h>

#include "ms912x.h"

static void ms912x_usb_request_timeout(struct timer_list *t)
{
	struct ms912x_usb_request *request = timer_container_of(request, t, timer);

	usb_sg_cancel(&request->sgr);
}

/* Caller holds transfer_lock. The initial estimate is one seed sample. */
static void ms912x_usb_rate_update_locked(struct ms912x_device *ms912x,
						   u64 measured_bps)
{
	u64 mean = ms912x->usb_speed_mean_bps;
	u64 samples = ms912x->usb_speed_samples;
	u64 delta;

	if (!samples) {
		ms912x->usb_speed_mean_bps = measured_bps;
		ms912x->usb_speed_samples = 1;
		return;
	}

	if (measured_bps >= mean) {
		delta = div64_u64(measured_bps - mean, samples + 1);
		mean += delta;
	} else {
		delta = div64_u64(mean - measured_bps, samples + 1);
		mean -= delta;
	}

	ms912x->usb_speed_mean_bps = mean;
	ms912x->usb_speed_samples = samples + 1;
}

static unsigned long ms912x_settle_delay_jiffies(
						struct ms912x_device *ms912x)
{
	unsigned int refresh_hz = READ_ONCE(ms912x->display_refresh_hz);

	if (!refresh_hz)
		refresh_hz = 60;
	return max_t(unsigned long, 1, DIV_ROUND_CLOSEST(HZ, 2 * refresh_hz));
}

/* Caller holds transfer_lock. Carry failed damage into the next pair/replay. */
static void ms912x_usb_carry_failed_rect_locked(
					struct ms912x_device *ms912x,
					const struct ms912x_usb_request *request)
{
	if (!ms912x->failed_rect_valid ||
	    ms912x->failed_generation != request->generation ||
	    ms912x->failed_width != request->width ||
	    ms912x->failed_height != request->height) {
		ms912x->failed_rect = request->base_rect;
		ms912x->failed_width = request->width;
		ms912x->failed_height = request->height;
		ms912x->failed_generation = request->generation;
		ms912x->failed_rect_valid = true;
	} else {
		ms912x->failed_rect.x1 = min(ms912x->failed_rect.x1,
					     request->base_rect.x1);
		ms912x->failed_rect.y1 = min(ms912x->failed_rect.y1,
					     request->base_rect.y1);
		ms912x->failed_rect.x2 = max(ms912x->failed_rect.x2,
					     request->base_rect.x2);
		ms912x->failed_rect.y2 = max(ms912x->failed_rect.y2,
					     request->base_rect.y2);
	}

	/* If no newer pair arrives, the one-shot settle work retries this region. */
	ms912x->last_wire_rect = ms912x->failed_rect;
	ms912x->last_wire_rect_valid = true;
	ms912x->frame_snapshot_valid = true;
	ms912x->last_frame_activity = jiffies;
	mod_delayed_work(system_percpu_wq, &ms912x->idle_work,
			 ms912x_settle_delay_jiffies(ms912x));
}

static void ms912x_usb_request_worker(struct work_struct *work)
{
	struct ms912x_usb_request *request =
		container_of(work, struct ms912x_usb_request, work);
	struct ms912x_device *ms912x = request->ms912x;
	struct usb_device *usbdev = interface_to_usbdev(ms912x->intf);
	struct usb_sg_request *sgr = &request->sgr;
	struct sg_table *transfer_sgt = &request->transfer_sgt;
	u64 start_ns, queue_wait_ns, usb_duration_ns;
	u64 transfer_start_ns = 0, transfer_duration_ns = 0;
	bool stats_enabled, short_transfer = false, unmute_screen = false;
	bool retry_settle = false;
	u8 zero_length_payload = 0;
	int actual_length;
	int idx, ret;

	if (!drm_dev_enter(&ms912x->drm, &idx)) {
		/* Do not leave this slot permanently busy during unplug. */
		mutex_lock(&ms912x->transfer_lock);
		request->state = MS912X_REQUEST_FREE;
		request->usb_started_ns = 0;
		mutex_unlock(&ms912x->transfer_lock);
		return;
	}
	stats_enabled = ms912x_stats_enabled();
	for (;;) {
		mutex_lock(&ms912x->transfer_lock);
		if (ms912x->transfer_stopping) {
			request->state = MS912X_REQUEST_FREE;
			mutex_unlock(&ms912x->transfer_lock);
			drm_dev_exit(idx);
			return;
		}
		if (request->generation != ms912x->frame_generation ||
		    (request->idle_replay &&
		     request->idle_replay_generation !=
			     ms912x->idle_replay_generation)) {
			request->state = MS912X_REQUEST_FREE;
			if (ms912x->pending_valid) {
				ms912x->pending_deadline = 0;
				ms912x_schedule_conversion_locked(ms912x);
			}
			mutex_unlock(&ms912x->transfer_lock);
			drm_dev_exit(idx);
			return;
		}
		if (request->source_generation != ms912x->scratch_generation) {
			mutex_unlock(&ms912x->transfer_lock);
			ret = ms912x_usb_request_refresh(request);
			if (ret) {
				mutex_lock(&ms912x->transfer_lock);
				request->state = MS912X_REQUEST_FREE;
				ms912x_schedule_conversion_locked(ms912x);
				mutex_unlock(&ms912x->transfer_lock);
				drm_dev_exit(idx);
				return;
			}
			continue;
		}
		request->state = MS912X_REQUEST_USB_ACTIVE;
		request->usb_started_ns = ktime_get_ns();
		if (stats_enabled) {
			start_ns = ktime_get_ns();
			queue_wait_ns = start_ns - request->queued_at_ns;
			ms912x_stats_record_duration(
				&ms912x->stats.workqueue_wait_ns,
				&ms912x->stats.workqueue_wait_last_ns,
				&ms912x->stats.workqueue_wait_max_ns,
				queue_wait_ns);
			start_ns = ktime_get_ns();
		}
		mutex_unlock(&ms912x->transfer_lock);
		break;
	}

	ret = usb_sg_init(sgr, usbdev, ms912x->bulk_pipe, 0, transfer_sgt->sgl,
			  transfer_sgt->nents, request->transfer_len,
			  GFP_KERNEL);
	if (ret)
		goto dev_exit;
	MS912X_STAT_INC(ms912x, usb_requests);

	transfer_start_ns = ktime_get_ns();
	mutex_lock(&ms912x->transfer_lock);
	request->usb_started_ns = transfer_start_ns;
	mutex_unlock(&ms912x->transfer_lock);
	mod_timer(&request->timer, jiffies + msecs_to_jiffies(5000));
	usb_sg_wait(sgr);
	transfer_duration_ns = ktime_get_ns() - transfer_start_ns;
	MS912X_STAT_ADD(ms912x, usb_bytes, sgr->bytes);
	MS912X_STAT_SET(ms912x, usb_last_bytes, sgr->bytes);

	if (!timer_delete_sync(&request->timer))
		ret = -ETIMEDOUT;
	else if (sgr->status < 0)
		ret = sgr->status;
	else if (sgr->bytes != request->transfer_len) {
		ret = -EIO;
		short_transfer = true;
	}
	if (!ret)
		ret = usb_bulk_msg(usbdev, ms912x->bulk_pipe,
				   &zero_length_payload, 0, &actual_length, 2000);

dev_exit:
	if (stats_enabled) {
		usb_duration_ns = ktime_get_ns() - start_ns;
		ms912x_stats_record_duration(&ms912x->stats.usb_ns,
				       &ms912x->stats.usb_last_ns,
				       &ms912x->stats.usb_max_ns,
				       usb_duration_ns);
	}
	mutex_lock(&ms912x->transfer_lock);
	if (!ret && transfer_duration_ns) {
		u64 completed_at_ns = ktime_get_ns();
		u64 measured_bps = div64_u64(
			(u64)request->transfer_len * NSEC_PER_SEC,
			transfer_duration_ns);

		ms912x_usb_rate_update_locked(ms912x, measured_bps);
		if (!ms912x->usb_mean_payload_bytes ||
		    request->transfer_len < ms912x->usb_mean_payload_bytes) {
			/* Adapt quickly to small interactive updates. */
			ms912x->usb_mean_payload_bytes = request->transfer_len;
		} else {
			u64 delta = (request->transfer_len -
				     ms912x->usb_mean_payload_bytes) / 8;

			/* Let the estimate grow smoothly as larger traffic returns. */
			ms912x->usb_mean_payload_bytes += max_t(u64, delta, 1);
		}
		if (!ms912x->usb_successful_transfers)
			ms912x->usb_first_success_ns = completed_at_ns;
		ms912x->usb_last_success_ns = completed_at_ns;
		ms912x->usb_successful_transfers++;
	}
	if (!ret && request->generation == ms912x->frame_generation) {
		if (ms912x->failed_rect_valid &&
		    ms912x->failed_generation == request->generation &&
		    ms912x->failed_width == request->width &&
		    ms912x->failed_height == request->height) {
			if (request->rect.x1 <= ms912x->failed_rect.x1 &&
			    request->rect.y1 <= ms912x->failed_rect.y1 &&
			    request->rect.x2 >= ms912x->failed_rect.x2 &&
			    request->rect.y2 >= ms912x->failed_rect.y2) {
				ms912x->failed_rect_valid = false;
			} else {
				request->rect.x1 = min(request->rect.x1,
						       ms912x->failed_rect.x1);
				request->rect.y1 = min(request->rect.y1,
						       ms912x->failed_rect.y1);
				request->rect.x2 = max(request->rect.x2,
						       ms912x->failed_rect.x2);
				request->rect.y2 = max(request->rect.y2,
						       ms912x->failed_rect.y2);
				retry_settle = true;
			}
		}
		if (!request->idle_replay) {
			ms912x->last_wire_rect = request->rect;
			ms912x->last_wire_rect_valid = true;
			mod_delayed_work(system_percpu_wq, &ms912x->idle_work,
					 ms912x_settle_delay_jiffies(ms912x));
		} else if (retry_settle) {
			ms912x->last_wire_rect = request->rect;
			ms912x->last_wire_rect_valid = true;
			mod_delayed_work(system_percpu_wq, &ms912x->idle_work,
					 ms912x_settle_delay_jiffies(ms912x));
		}
		if (request->idle_replay &&
		    request->idle_replay_generation ==
			    ms912x->idle_replay_generation &&
		    request->idle_replay_tile == ms912x->idle_replay_tile)
			ms912x->idle_replay_tile =
				(request->idle_replay_tile + 1) %
				MS912X_IDLE_REPLAY_TILES;
		if (request->idle_replay)
			MS912X_STAT_INC(ms912x, settle_replays);
		ms912x->frame_snapshot_valid = true;
		ms912x->last_frame_activity = jiffies;
		unmute_screen = ms912x->screen_muted &&
			request->rect.x1 == 0 && request->rect.y1 == 0 &&
			request->rect.x2 == request->width &&
			request->rect.y2 == request->height;
	}
	if (ret < 0 && ret != -ENODEV && !ms912x->transfer_stopping &&
	    request->generation == ms912x->frame_generation) {
		ms912x_usb_carry_failed_rect_locked(ms912x, request);
	}
	request->state = MS912X_REQUEST_FREE;
	request->usb_started_ns = 0;
	request->idle_replay = false;
	if (!ms912x->transfer_stopping && ms912x->pending_valid) {
		ms912x->pending_deadline = 0;
		ms912x_schedule_conversion_locked(ms912x);
	}
	if (!unmute_screen) {
		ms912x_transfer_update_pacing_locked(ms912x);
		wake_up_all(&ms912x->frame_present_wait);
	}
	mutex_unlock(&ms912x->transfer_lock);
	if (unmute_screen) {
		int unmute_ret = ms912x_screen_unmute(ms912x);

		if (!unmute_ret) {
			mutex_lock(&ms912x->transfer_lock);
			if (request->generation == ms912x->frame_generation)
				ms912x->screen_muted = false;
			ms912x_transfer_update_pacing_locked(ms912x);
			wake_up_all(&ms912x->frame_present_wait);
			mutex_unlock(&ms912x->transfer_lock);
		} else {
			mutex_lock(&ms912x->transfer_lock);
			ms912x_transfer_update_pacing_locked(ms912x);
			wake_up_all(&ms912x->frame_present_wait);
			mutex_unlock(&ms912x->transfer_lock);
		}
	}
	if (ret < 0 && ret != -ENODEV) {
		MS912X_STAT_INC(ms912x, usb_errors);
		MS912X_STAT_SET(ms912x, usb_last_error, ret);
		if (ret == -ETIMEDOUT)
			MS912X_STAT_INC(ms912x, usb_timeouts);
		if (short_transfer)
			MS912X_STAT_INC(ms912x, usb_short_transfers);
		drm_err_ratelimited(&ms912x->drm,
				    "failed to send framebuffer: %d\n", ret);
	}
	drm_dev_exit(idx);
}

void ms912x_usb_request_release(struct ms912x_usb_request *request)
{
	if (!request->transfer_buffer)
		return;

	cancel_work_sync(&request->work);
	timer_shutdown_sync(&request->timer);
	sg_free_table(&request->transfer_sgt);
	vfree(request->transfer_buffer);
	request->transfer_buffer = NULL;
}

int ms912x_usb_request_init(struct ms912x_device *ms912x,
			struct ms912x_usb_request *request, size_t len)
{
	int ret;
	unsigned int i, num_pages;
	void *data;
	struct page **pages;
	void *ptr;

	data = vmalloc_32(len);
	if (!data)
		return -ENOMEM;

	num_pages = DIV_ROUND_UP(len, PAGE_SIZE);
	pages = kmalloc_array(num_pages, sizeof(struct page *), GFP_KERNEL);
	if (!pages) {
		ret = -ENOMEM;
		goto err_vfree;
	}

	for (i = 0, ptr = data; i < num_pages; i++, ptr += PAGE_SIZE)
		pages[i] = vmalloc_to_page(ptr);
	ret = sg_alloc_table_from_pages(&request->transfer_sgt, pages,
					num_pages, 0, len, GFP_KERNEL);
	kfree(pages);
	if (ret)
		goto err_vfree;

	request->transfer_buffer = data;
	request->ms912x = ms912x;
	request->state = MS912X_REQUEST_FREE;
	request->usb_started_ns = 0;

	timer_setup(&request->timer, ms912x_usb_request_timeout, 0);
	INIT_WORK(&request->work, ms912x_usb_request_worker);
	return 0;

err_vfree:
	vfree(data);
	return ret;
}

void ms912x_transfer_stop(struct ms912x_device *ms912x)
{
	unsigned int i;

	mutex_lock(&ms912x->transfer_lock);
	ms912x->transfer_stopping = true;
	wake_up_all(&ms912x->conversion_wait);
	wake_up_all(&ms912x->frame_present_wait);
	ms912x->pending_valid = false;
	ms912x->pending_force_full = false;
	ms912x->pending_idle_replay = false;
	ms912x->pending_deadline = 0;
	ms912x->idle_replay_generation++;
	ms912x->present_backpressured = false;
	ms912x->present_queue_pressure_ns = 0;
	ms912x->present_queue_low_watermark_ns = 0;
	ms912x->present_queue_high_watermark_ns = 0;
	ms912x->drm_released_sequence = ms912x->published_frame_sequence;
	ms912x->frame_snapshot_valid = false;
	ms912x->last_queued_rect_valid = false;
	ms912x->failed_rect_valid = false;
	ms912x->last_wire_rect_valid = false;
	mutex_unlock(&ms912x->transfer_lock);
	mutex_lock(&ms912x->scan_queue_lock);
	ms912x->scan_stopping = true;
	ms912x->scan_work_scheduled = false;
	mutex_unlock(&ms912x->scan_queue_lock);
	cancel_work_sync(&ms912x->scan_work);
	cancel_delayed_work_sync(&ms912x->idle_work);
	cancel_delayed_work_sync(&ms912x->conversion_work);

	for (i = 0; i < ARRAY_SIZE(ms912x->requests); i++)
		cancel_work_sync(&ms912x->requests[i].work);

	mutex_lock(&ms912x->transfer_lock);
	ms912x->conversion_busy = false;
	for (i = 0; i < ARRAY_SIZE(ms912x->requests); i++) {
		ms912x->requests[i].state = MS912X_REQUEST_FREE;
		ms912x->requests[i].usb_started_ns = 0;
		ms912x->requests[i].idle_replay = false;
	}
	mutex_unlock(&ms912x->transfer_lock);

	mutex_lock(&ms912x->scan_queue_lock);
	ms912x->scan_work_scheduled = false;
	for (i = 0; i < ARRAY_SIZE(ms912x->scan_frames); i++) {
		ms912x->scan_frames[i].queued = false;
		ms912x->scan_frames[i].running = false;
	}
	mutex_unlock(&ms912x->scan_queue_lock);
}

void ms912x_transfer_start(struct ms912x_device *ms912x)
{
	unsigned int i;

	mutex_lock(&ms912x->transfer_lock);
	ms912x->transfer_stopping = false;
	ms912x->conversion_busy = false;
	ms912x->pending_rect_count = 0;
	ms912x->pending_valid = false;
	ms912x->pending_force_full = false;
	ms912x->pending_idle_replay = false;
	ms912x->pending_deadline = 0;
	ms912x->frame_snapshot_valid = false;
	ms912x->last_queued_rect_valid = false;
	ms912x->failed_rect_valid = false;
	ms912x->last_wire_rect_valid = false;
	ms912x->idle_replay_generation++;
	ms912x->present_backpressured = false;
	ms912x->present_queue_pressure_ns = 0;
	ms912x->present_queue_low_watermark_ns = 0;
	ms912x->present_queue_high_watermark_ns = 0;
	ms912x->drm_released_sequence = ms912x->published_frame_sequence;
	ms912x->usb_successful_transfers = 0;
	ms912x->usb_first_success_ns = 0;
	ms912x->usb_last_success_ns = 0;
	mutex_unlock(&ms912x->transfer_lock);

	mutex_lock(&ms912x->scan_queue_lock);
	ms912x->scan_stopping = false;
	ms912x->scan_work_scheduled = false;
	for (i = 0; i < ARRAY_SIZE(ms912x->scan_frames); i++) {
		ms912x->scan_frames[i].queued = false;
		ms912x->scan_frames[i].running = false;
	}
	mutex_unlock(&ms912x->scan_queue_lock);

	ms912x_damage_scan_invalidate(ms912x);
	ms912x_stats_reset(ms912x);
}

void ms912x_transfer_free_requests(struct ms912x_device *ms912x)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(ms912x->requests); i++)
		ms912x_usb_request_release(&ms912x->requests[i]);
}
