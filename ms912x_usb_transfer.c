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
						   u64 measured_bytes_per_sec)
{
	u64 mean = ms912x->usb_speed_mean_bytes_per_sec;
	u64 samples = ms912x->usb_speed_samples;
	u64 delta;

	if (!samples) {
		ms912x->usb_speed_mean_bytes_per_sec = measured_bytes_per_sec;
		ms912x->usb_speed_samples = 1;
		return;
	}

	if (measured_bytes_per_sec >= mean) {
		delta = div64_u64(measured_bytes_per_sec - mean, samples + 1);
		mean += delta;
	} else {
		delta = div64_u64(mean - measured_bytes_per_sec, samples + 1);
		mean -= delta;
	}

	ms912x->usb_speed_mean_bytes_per_sec = mean;
	ms912x->usb_speed_samples = samples + 1;
}

/* Caller holds transfer_lock. Retry failed damage with the next real update. */
static void ms912x_usb_carry_failed_rect_locked(
					struct ms912x_device *ms912x,
					const struct ms912x_usb_request *request)
{
	ms912x->failed_rect = DRM_RECT_INIT(0, 0, request->width,
					     request->height);
	ms912x->failed_width = request->width;
	ms912x->failed_height = request->height;
	ms912x->failed_generation = request->generation;
	ms912x->failed_rect_valid = true;
}

static bool ms912x_usb_request_covers_rect(
				const struct ms912x_usb_request *request,
				const struct drm_rect *rect)
{
	unsigned int i;

	for (i = 0; i < request->rect_count; i++)
		if (request->rects[i].x1 <= rect->x1 &&
		    request->rects[i].y1 <= rect->y1 &&
		    request->rects[i].x2 >= rect->x2 &&
		    request->rects[i].y2 >= rect->y2)
			return true;
	return false;
}

/* Caller holds transfer_lock. Keep replay rectangles if this batch never sends. */
static void ms912x_usb_requeue_request_rects_locked(
					struct ms912x_device *ms912x,
					const struct ms912x_usb_request *request)
{
	unsigned int i;

	MS912X_STAT_INC(ms912x, usb_requests_requeued_before_send);
	for (i = 0; i < request->rect_count; i++)
		ms912x_transfer_requeue_rect_locked(ms912x, &request->rects[i],
						    request->width,
						    request->height);
}

/* Caller holds transfer_lock. Release a request and wake presentation waiters. */
static void ms912x_usb_request_release_locked(
					struct ms912x_usb_request *request)
{
	request->state = MS912X_REQUEST_FREE;
	wake_up_all(&request->ms912x->frame_present_wait);
}

/* Caller holds transfer_lock. Return false when this batch must not be sent. */
static bool ms912x_usb_request_prepare_locked(
					struct ms912x_usb_request *request,
					bool refreshed, bool *refresh)
{
	struct ms912x_device *ms912x = request->ms912x;

	*refresh = false;
	if (ms912x->transfer_stopping) {
		ms912x_usb_request_release_locked(request);
		return false;
	}
	if (request->generation != ms912x->frame_generation) {
		ms912x_usb_request_release_locked(request);
		if (ms912x->pending_valid) {
			ms912x->pending_deadline = 0;
			ms912x_schedule_conversion_locked(ms912x);
		}
		return false;
	}
	if (ms912x->failed_rect_valid &&
	    ms912x->failed_generation == request->generation &&
	    ms912x->failed_width == request->width &&
	    ms912x->failed_height == request->height &&
	    !ms912x_usb_request_covers_rect(request, &ms912x->failed_rect)) {
		ms912x_usb_request_release_locked(request);
		ms912x_transfer_requeue_rect_locked(ms912x, &ms912x->failed_rect,
						    request->width,
						    request->height);
		ms912x_transfer_update_pacing_locked(ms912x);
		return false;
	}
	if (ms912x->cursor_commit_pending) {
		ms912x_usb_request_release_locked(request);
		ms912x_usb_requeue_request_rects_locked(ms912x, request);
		ms912x_transfer_update_pacing_locked(ms912x);
		return false;
	}
	if (!refreshed &&
	    request->source_generation != request->content_generation) {
		*refresh = true;
		return true;
	}
	request->state = MS912X_REQUEST_USB_ACTIVE;
	ms912x_transfer_update_pacing_locked(ms912x);
	return true;
}

static int ms912x_usb_request_execute(
				struct ms912x_usb_request *request,
				struct usb_device *usbdev,
				bool stats_enabled, u64 start_ns,
				u64 *transfer_duration_ns,
				bool *short_transfer)
{
	struct ms912x_device *ms912x = request->ms912x;
	struct usb_sg_request *sgr = &request->sgr;
	struct sg_table *transfer_sgt = &request->transfer_sgt;
	u64 transfer_start_ns;
	int ret;

	MS912X_STAT_INC(ms912x, usb_requests);
	ret = usb_sg_init(sgr, usbdev, ms912x->bulk_pipe, 0, transfer_sgt->sgl,
			  transfer_sgt->nents, request->transfer_len,
			  GFP_KERNEL);
	if (ret)
		goto out;

	transfer_start_ns = ktime_get_ns();
	mod_timer(&request->timer, jiffies + msecs_to_jiffies(5000));
	usb_sg_wait(sgr);
	*transfer_duration_ns = ktime_get_ns() - transfer_start_ns;
	MS912X_STAT_ADD(ms912x, usb_bytes, sgr->bytes);

	if (!timer_delete_sync(&request->timer))
		ret = -ETIMEDOUT;
	else if (sgr->status < 0)
		ret = sgr->status;
	else if (sgr->bytes != request->transfer_len) {
		ret = -EIO;
		*short_transfer = true;
	}
out:
	if (stats_enabled) {
		u64 usb_duration_ns = ktime_get_ns() - start_ns;

		atomic64_add(usb_duration_ns, &ms912x->stats.usb_ns);
	}
	return ret;
}

/* Caller holds transfer_lock. Publish transfer outcome and release the slot. */
static void ms912x_usb_request_complete_locked(
				struct ms912x_usb_request *request, int ret,
				u64 transfer_duration_ns)
{
	struct ms912x_device *ms912x = request->ms912x;

	if (!ret && request->replay_included)
		MS912X_STAT_INC(ms912x, damage_replay_batches_sent);
	if (!ret && transfer_duration_ns) {
		u64 measured_bytes_per_sec = div64_u64(
			(u64)request->transfer_len * NSEC_PER_SEC,
			transfer_duration_ns);

		ms912x_usb_rate_update_locked(ms912x, measured_bytes_per_sec);
		ms912x->usb_successful_transfers++;
	}
	if (!ret && request->generation == ms912x->frame_generation &&
	    ms912x->failed_rect_valid &&
	    ms912x->failed_generation == request->generation &&
	    ms912x->failed_width == request->width &&
	    ms912x->failed_height == request->height &&
	    ms912x_usb_request_covers_rect(request, &ms912x->failed_rect))
		ms912x->failed_rect_valid = false;
	if (ret < 0 && ret != -ENODEV && !ms912x->transfer_stopping &&
	    request->generation == ms912x->frame_generation)
		ms912x_usb_carry_failed_rect_locked(ms912x, request);
	ms912x_usb_request_release_locked(request);
	if (!ms912x->transfer_stopping && ms912x->pending_valid) {
		ms912x->pending_deadline = 0;
		ms912x_schedule_conversion_locked(ms912x);
	}
	ms912x_transfer_update_pacing_locked(ms912x);
}

static void ms912x_usb_request_worker(struct work_struct *work)
{
	struct ms912x_usb_request *request =
		container_of(work, struct ms912x_usb_request, work);
	struct ms912x_device *ms912x = request->ms912x;
	struct usb_device *usbdev = interface_to_usbdev(ms912x->intf);
	u64 start_ns = 0;
	u64 transfer_duration_ns = 0;
	bool stats_enabled, short_transfer = false, refreshed = false;
	bool refresh;
	int idx, ret;

	if (!drm_dev_enter(&ms912x->drm, &idx)) {
		/* Do not leave this slot permanently busy during unplug. */
		mutex_lock(&ms912x->transfer_lock);
		ms912x_usb_request_release_locked(request);
		mutex_unlock(&ms912x->transfer_lock);
		return;
	}
	stats_enabled = ms912x_stats_enabled();
	for (;;) {
		mutex_lock(&ms912x->transfer_lock);
		if (!ms912x_usb_request_prepare_locked(request, refreshed, &refresh)) {
			mutex_unlock(&ms912x->transfer_lock);
			drm_dev_exit(idx);
			return;
		}
		if (refresh) {
			mutex_unlock(&ms912x->transfer_lock);
			ret = ms912x_usb_request_refresh(request);
			refreshed = true;
			if (ret) {
				mutex_lock(&ms912x->transfer_lock);
				ms912x_usb_request_release_locked(request);
				if (!ms912x->transfer_stopping &&
				    request->generation ==
					ms912x->frame_generation) {
					ms912x_usb_requeue_request_rects_locked(ms912x,
								       request);
					ms912x_schedule_conversion_locked(ms912x);
				} else if (ms912x->pending_valid) {
					ms912x->pending_deadline = 0;
					ms912x_schedule_conversion_locked(ms912x);
				}
				mutex_unlock(&ms912x->transfer_lock);
				drm_dev_exit(idx);
				return;
			}
			continue;
		}
		if (stats_enabled)
			start_ns = ktime_get_ns();
		mutex_unlock(&ms912x->transfer_lock);
		break;
	}

	ret = ms912x_usb_request_execute(request, usbdev, stats_enabled,
					 start_ns, &transfer_duration_ns,
					 &short_transfer);
	mutex_lock(&ms912x->transfer_lock);
	ms912x_usb_request_complete_locked(request, ret, transfer_duration_ns);
	mutex_unlock(&ms912x->transfer_lock);
	if (ret < 0 && ret != -ENODEV) {
		MS912X_STAT_INC(ms912x, usb_errors);
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
	if (request->transfer_buffer) {
		cancel_work_sync(&request->work);
		timer_shutdown_sync(&request->timer);
		sg_free_table(&request->transfer_sgt);
		vfree(request->transfer_buffer);
		request->transfer_buffer = NULL;
	}
	kvfree(request->rects);
	kvfree(request->current_rects);
	request->rects = NULL;
	request->current_rects = NULL;
	request->rect_capacity = 0;
	request->current_rect_capacity = 0;
}

int ms912x_usb_request_init(struct ms912x_device *ms912x,
			struct ms912x_usb_request *request, size_t len)
{
	int ret;
	unsigned int i, num_pages;
	void *data;
	struct page **pages;
	void *ptr;

	request->rects = kvcalloc(MS912X_INITIAL_DAMAGE_RECTS,
				  sizeof(*request->rects), GFP_KERNEL);
	request->current_rects = kvcalloc(MS912X_INITIAL_DAMAGE_RECTS,
					  sizeof(*request->current_rects),
					  GFP_KERNEL);
	if (!request->rects || !request->current_rects) {
		ret = -ENOMEM;
		goto err_vectors;
	}
	request->rect_capacity = MS912X_INITIAL_DAMAGE_RECTS;
	request->current_rect_capacity = MS912X_INITIAL_DAMAGE_RECTS;
	data = vmalloc_32(len);
	if (!data) {
		ret = -ENOMEM;
		goto err_vectors;
	}

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

	timer_setup(&request->timer, ms912x_usb_request_timeout, 0);
	INIT_WORK(&request->work, ms912x_usb_request_worker);
	return 0;

err_vfree:
	vfree(data);
err_vectors:
	kvfree(request->rects);
	kvfree(request->current_rects);
	request->rects = NULL;
	request->current_rects = NULL;
	request->rect_capacity = 0;
	request->current_rect_capacity = 0;
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
	ms912x->pending_deadline = 0;
	ms912x->present_backpressured = false;
	ms912x->present_queue_pressure_ns = 0;
	ms912x->present_queue_low_watermark_ns = 0;
	ms912x->present_queue_high_watermark_ns = 0;
	ms912x->drm_released_sequence = ms912x->published_frame_sequence;
	ms912x->failed_rect_valid = false;
	ms912x->previous_damage_rect_count = 0;
	ms912x->cursor_commit_pending = false;
	mutex_unlock(&ms912x->transfer_lock);
	mutex_lock(&ms912x->scan_queue_lock);
	ms912x->scan_stopping = true;
	ms912x->scan_epoch++;
	ms912x->scan_work_scheduled = false;
	mutex_unlock(&ms912x->scan_queue_lock);
	wake_up_all(&ms912x->scan_frame_wait);
	/* Drain CPU-access callbacks without holding either shared lock. */
	mutex_lock(&ms912x->submit_lock);
	mutex_unlock(&ms912x->submit_lock);
	cancel_work_sync(&ms912x->scan_work);
	cancel_delayed_work_sync(&ms912x->conversion_work);
	ms912x_scan_wait_captures(ms912x);

	for (i = 0; i < ARRAY_SIZE(ms912x->requests); i++)
		cancel_work_sync(&ms912x->requests[i].work);

	mutex_lock(&ms912x->transfer_lock);
	ms912x->conversion_busy = false;
	for (i = 0; i < ARRAY_SIZE(ms912x->requests); i++) {
		ms912x->requests[i].state = MS912X_REQUEST_FREE;
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
	ms912x->pending_deadline = 0;
	ms912x->cursor_commit_pending = false;
	ms912x->failed_rect_valid = false;
	ms912x->previous_damage_rect_count = 0;
	ms912x->present_backpressured = false;
	ms912x->present_queue_pressure_ns = 0;
	ms912x->present_queue_low_watermark_ns = 0;
	ms912x->present_queue_high_watermark_ns = 0;
	ms912x->drm_released_sequence = ms912x->published_frame_sequence;
	ms912x->usb_successful_transfers = 0;
	mutex_unlock(&ms912x->transfer_lock);

	mutex_lock(&ms912x->scan_queue_lock);
	ms912x->scan_stopping = false;
	ms912x->scan_epoch++;
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
