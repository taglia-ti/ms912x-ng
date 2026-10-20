// SPDX-License-Identifier: GPL-2.0-only

#include <linux/align.h>
#include <linux/dma-direction.h>
#include <linux/iosys-map.h>
#include <linux/jiffies.h>
#include <linux/ktime.h>
#include <linux/math64.h>
#include <linux/minmax.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/workqueue.h>

#include <drm/drm_drv.h>
#include <drm/drm_blend.h>
#include <drm/drm_damage_helper.h>
#include <drm/drm_format_helper.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_print.h>

#include "ms912x.h"
#include "ms912x_mode3.h"
#include "ms912x_batch.h"
#include "ms912x_payload.h"

struct drm_rect *ms912x_rect_vector_grow(struct drm_rect *rects,
					 unsigned int *capacity,
					 unsigned int required,
					 unsigned int limit)
{
	struct drm_rect *grown;
	unsigned int new_capacity = *capacity;

	if (required <= new_capacity)
		return rects;
	if (required > limit)
		return NULL;
	if (!new_capacity)
		new_capacity = min_t(unsigned int,
				     MS912X_INITIAL_DAMAGE_RECTS, limit);
	while (new_capacity < required)
		new_capacity = min_t(unsigned int, new_capacity +
				     MS912X_DAMAGE_RECTS_GROWTH, limit);
	grown = kvrealloc(rects, new_capacity * sizeof(*rects), GFP_KERNEL);
	if (!grown)
		return NULL;
	*capacity = new_capacity;
	return grown;
}

static bool ms912x_rect_valid_for_frame(const struct drm_rect *rect,
					unsigned int width,
					unsigned int height)
{
	return width && height && width <= MS912X_MAX_WIDTH &&
		height <= MS912X_MAX_HEIGHT && rect->x1 >= 0 && rect->y1 >= 0 &&
		rect->x1 < rect->x2 && rect->y1 < rect->y2 &&
		rect->x2 <= width && rect->y2 <= height;
}

static bool ms912x_rect_clip_to_frame(struct drm_rect *rect,
				      unsigned int width,
				      unsigned int height)
{
	struct drm_rect frame;

	if (!width || !height || width > MS912X_MAX_WIDTH ||
	    height > MS912X_MAX_HEIGHT || rect->x1 >= rect->x2 ||
	    rect->y1 >= rect->y2)
		return false;

	frame = DRM_RECT_INIT(0, 0, width, height);
	return drm_rect_intersect(rect, &frame);
}

static bool ms912x_rect_map_output_to_input(struct drm_rect *rect,
					   unsigned int output_width,
					   unsigned int output_height,
					   unsigned int input_width,
					   unsigned int input_height,
					   enum ms912x_scale_mode scale_mode)
{
	struct drm_rect output_bounds = DRM_RECT_INIT(0, 0, output_width,
						       output_height);
	struct drm_rect input_bounds = DRM_RECT_INIT(0, 0, input_width,
						      input_height);

	if (!drm_rect_intersect(rect, &output_bounds))
		return false;
	if (scale_mode == MS912X_SCALE_MODE_DOWNSCALE_2_TO_1) {
		rect->x1 /= 2;
		rect->y1 /= 2;
		rect->x2 = DIV_ROUND_UP(rect->x2, 2);
		rect->y2 = DIV_ROUND_UP(rect->y2, 2);
	} else if (scale_mode == MS912X_SCALE_MODE_DOWNSCALE_3_TO_2) {
		/* A cubic footprint extends beyond the directly covered source pixels. */
		rect->x1 = max(rect->x1 - 2, 0);
		rect->y1 = max(rect->y1 - 2, 0);
		rect->x2 = min_t(unsigned int, rect->x2 + 2, output_width);
		rect->y2 = min_t(unsigned int, rect->y2 + 2, output_height);
		/* Preserve the global 3->2 phase when staging a partial update. */
		rect->x1 = (rect->x1 * 2 / 3) & ~1;
		rect->y1 = (rect->y1 * 2 / 3) & ~1;
		/* Refresh replicated padding when the source edge changes. */
		rect->x2 = rect->x2 == output_width ? input_width :
			DIV_ROUND_UP(rect->x2 * 2, 3);
		rect->y2 = rect->y2 == output_height ? input_height :
			DIV_ROUND_UP(rect->y2 * 2, 3);
	}
	return drm_rect_intersect(rect, &input_bounds);
}

static void ms912x_rects_collapse_bounds(struct drm_rect *rects,
					 unsigned int *rect_count,
					 const struct drm_rect *extra)
{
	struct drm_rect bounds = *extra;
	unsigned int i;

	for (i = 0; i < *rect_count; i++) {
		bounds.x1 = min(bounds.x1, rects[i].x1);
		bounds.y1 = min(bounds.y1, rects[i].y1);
		bounds.x2 = max(bounds.x2, rects[i].x2);
		bounds.y2 = max(bounds.y2, rects[i].y2);
	}
	rects[0] = bounds;
	*rect_count = 1;
}

/* Caller holds transfer_lock. Compact before estimating queue pressure. */
static void ms912x_pending_compact_locked(struct ms912x_device *ms912x)
{
	struct drm_rect bounds = DRM_RECT_INIT(0, 0,
		ms912x->pending_width, ms912x->pending_height);

	ms912x_rects_compact(ms912x->pending_rects,
			     &ms912x->pending_rect_count,
			     ms912x_mode3_rect_encoded_bytes(
				ms912x->pending_width, ms912x->pending_height));
	if (ms912x->pending_rect_count == 1 &&
	    drm_rect_equals(&ms912x->pending_rects[0], &bounds)) {
		ms912x->pending_force_full = true;
		ms912x->pending_deadline = 0;
	}
}

/* Caller holds transfer_lock. */
static void ms912x_pending_add_rect_locked(struct ms912x_device *ms912x,
						   const struct drm_rect *rect)
{
	struct drm_rect appended = *rect;
	struct drm_rect bounds = DRM_RECT_INIT(0, 0,
					       ms912x->pending_width,
					       ms912x->pending_height);
	struct drm_rect *grown;

	if (ms912x->pending_rect_count == 1 &&
	    drm_rect_equals(&ms912x->pending_rects[0], &bounds))
		return;

	if (!ms912x_rect_clip_to_frame(&appended, ms912x->pending_width,
				       ms912x->pending_height))
		return;
	if (drm_rect_equals(&appended, &bounds)) {
		ms912x->pending_rect_count = 0;
		ms912x->pending_force_full = true;
		ms912x->pending_deadline = 0;
	}
	if (ms912x->pending_rect_count == ms912x->pending_rect_capacity) {
		grown = ms912x_rect_vector_grow(ms912x->pending_rects,
						&ms912x->pending_rect_capacity,
						ms912x->pending_rect_count + 1,
						MS912X_MAX_DAMAGE_RECTS);
		if (grown)
			ms912x->pending_rects = grown;
	}
	if (ms912x->pending_rect_count == ms912x->pending_rect_capacity) {
		struct drm_rect merged = appended;
		unsigned int i;

		for (i = 0; i < ms912x->pending_rect_count; i++) {
			merged.x1 = min(merged.x1,
					ms912x->pending_rects[i].x1);
			merged.y1 = min(merged.y1,
					ms912x->pending_rects[i].y1);
			merged.x2 = max(merged.x2,
					ms912x->pending_rects[i].x2);
			merged.y2 = max(merged.y2,
					ms912x->pending_rects[i].y2);
		}
		ms912x->pending_rect_count = 0;
		ms912x->pending_deadline = 0;
		appended = merged;
		if (drm_rect_equals(&appended, &bounds))
			ms912x->pending_force_full = true;
		MS912X_STAT_INC(ms912x,
				damage_queue_overflow_merged_updates);
	}
	ms912x->pending_rects[ms912x->pending_rect_count++] = appended;
	ms912x->pending_valid = true;
}

void ms912x_transfer_requeue_rect_locked(struct ms912x_device *ms912x,
					 const struct drm_rect *rect,
					 unsigned int width,
					 unsigned int height)
{
	if (!ms912x->pending_rect_count) {
		ms912x->pending_width = width;
		ms912x->pending_height = height;
	}
	ms912x_pending_add_rect_locked(ms912x, rect);
	ms912x->pending_deadline = 0;
	ms912x_schedule_conversion_locked(ms912x);
}

static void ms912x_rect_align_x4(struct drm_rect *rect,
				 unsigned int fb_width);
static void ms912x_pending_update_deadline_locked(struct ms912x_device *ms912x,
						 bool had_pending);

static bool ms912x_cursor_bounds(int x, int y, unsigned int cursor_width,
				 unsigned int cursor_height, unsigned int width,
				 unsigned int height, struct drm_rect *rect)
{
	struct drm_rect clipped = {
		.x1 = max(x, 0),
		.y1 = max(y, 0),
		.x2 = min_t(s64, (s64)x + cursor_width, width),
		.y2 = min_t(s64, (s64)y + cursor_height, height),
	};

	if (!ms912x_rect_clip_to_frame(&clipped, width, height))
		return false;
	ms912x_rect_align_x4(&clipped, width);
	if (!ms912x_rect_valid_for_frame(&clipped, width, height))
		return false;
	*rect = clipped;
	return true;
}

static void ms912x_cursor_snapshot_request_locked(
	struct ms912x_device *ms912x, struct ms912x_usb_request *request,
	const struct drm_rect *rects, unsigned int rect_count)
{
	unsigned int i;

	request->cursor_enabled = ms912x->cursor_enabled;
	request->cursor_x = ms912x->cursor_x;
	request->cursor_y = ms912x->cursor_y;
	request->cursor_width = ms912x->cursor_width;
	request->cursor_height = ms912x->cursor_height;
	request->cursor_alpha_x = ms912x->cursor_alpha_x;
	request->cursor_alpha_y = ms912x->cursor_alpha_y;
	request->cursor_source_height = ms912x->cursor_source_height;
	request->cursor_premultiplied = ms912x->cursor_premultiplied;
	request->cursor_included = false;
	for (i = 0; i < rect_count; i++)
		request->cursor_included |= ms912x->cursor_enabled &&
			rects[i].x1 < ms912x->cursor_x +
				(int)ms912x->cursor_width &&
			rects[i].x2 > ms912x->cursor_x &&
			rects[i].y1 < ms912x->cursor_y +
				(int)ms912x->cursor_height &&
			rects[i].y2 > ms912x->cursor_y;
	if (request->cursor_enabled)
		memcpy(request->cursor_pixels, ms912x->cursor_pixels,
		       MS912X_CURSOR_MAX_WIDTH *
		       request->cursor_source_height *
		       sizeof(*request->cursor_pixels));
}

static void ms912x_cursor_state_position(const struct drm_plane_state *state,
					 int *x, int *y)
{
	*x = state->crtc_x - (int)(state->src_x >> 16);
	*y = state->crtc_y - (int)(state->src_y >> 16);
}

static int ms912x_cursor_scaled_coordinate(int value, enum ms912x_scale_mode scale_mode)
{
	return ms912x_scale_coordinate(value, scale_mode);
}

bool ms912x_cursor_commit_begin(struct ms912x_device *ms912x,
				const struct drm_plane_state *new_state)
{
	bool enabled;
	unsigned int width = 0, height = 0;
	int x = 0, y = 0;
	u32 fb_id = 0;
	bool premultiplied;
	bool pending;

	if (!ms912x->cursor_plane_enabled || !new_state)
		return false;
	enabled = new_state->fb && new_state->crtc && new_state->visible;
	if (new_state->fb) {
		width = new_state->fb->width;
		height = new_state->fb->height;
		fb_id = new_state->fb->base.id;
		ms912x_cursor_state_position(new_state, &x, &y);
	}
	premultiplied = !new_state ||
		new_state->pixel_blend_mode != DRM_MODE_BLEND_COVERAGE;

	mutex_lock(&ms912x->transfer_lock);
	pending = enabled != ms912x->cursor_plane_active ||
		width != ms912x->cursor_plane_width ||
		height != ms912x->cursor_plane_height ||
		x != ms912x->cursor_plane_x ||
		y != ms912x->cursor_plane_y || fb_id != ms912x->cursor_fb_id;
	pending |= premultiplied != ms912x->cursor_premultiplied;
	pending |= drm_plane_get_damage_clips_count(new_state) != 0;
	if (pending) {
		ms912x->cursor_commit_pending = true;
		ms912x->cursor_commit_sequence = 0;
	}
	mutex_unlock(&ms912x->transfer_lock);
	return pending;
}

static int ms912x_cursor_copy_framebuffer(struct drm_plane_state *state,
					 u32 *pixels)
{
	struct drm_framebuffer *fb = state->fb;
	struct iosys_map map, data[DRM_FORMAT_MAX_PLANES];
	unsigned int x, y;
	int ret;

	if (!fb || fb->format->format != DRM_FORMAT_ARGB8888 ||
	    fb->width > MS912X_CURSOR_MAX_WIDTH ||
	    fb->height > MS912X_CURSOR_MAX_HEIGHT ||
	    fb->modifier != DRM_FORMAT_MOD_LINEAR)
		return -EINVAL;

	ret = drm_gem_fb_begin_cpu_access(fb, DMA_FROM_DEVICE);
	if (ret)
		return ret;
	ret = drm_gem_fb_vmap(fb, &map, data);
	if (ret)
		goto out_end_access;

	for (y = 0; y < fb->height; y++) {
		u32 *dest = pixels + y * MS912X_CURSOR_MAX_WIDTH;
		iosys_map_memcpy_from(dest, &data[0],
				      y * fb->pitches[0],
				      fb->width * sizeof(*dest));
		for (x = 0; x < fb->width; x++)
			dest[x] = le32_to_cpu((__force __le32)dest[x]);
	}
	drm_gem_fb_vunmap(fb, &map);
out_end_access:
	drm_gem_fb_end_cpu_access(fb, DMA_FROM_DEVICE);
	return ret;
}

void ms912x_cursor_plane_submit(struct ms912x_device *ms912x,
				struct drm_plane_state *new_state)
{
	u32 *new_pixels = ms912x->cursor_staging_pixels;
	struct drm_rect new_rect, old_rect;
	unsigned int width, height, cursor_width = 0, cursor_height = 0;
	unsigned int plane_width = 0, plane_height = 0;
	unsigned int source_height = 0;
	unsigned int alpha_x = 0, alpha_y = 0;
	int cursor_x = 0, cursor_y = 0, ret = 0;
	int plane_x = 0, plane_y = 0;
	u32 cursor_fb_id = 0;
	enum ms912x_scale_mode scale_mode;
	bool new_valid = false, old_valid = false, had_pending;
	bool enabled, premultiplied, source_unchanged = false;
	bool prepared_unchanged;

	if (!ms912x->cursor_plane_enabled)
		return;
	if (new_state && new_state->fb) {
		plane_width = new_state->fb->width;
		plane_height = new_state->fb->height;
		cursor_width = plane_width;
		cursor_height = plane_height;
		cursor_fb_id = new_state->fb->base.id;
		ms912x_cursor_state_position(new_state, &cursor_x, &cursor_y);
	}
	plane_x = cursor_x;
	plane_y = cursor_y;
	enabled = new_state && new_state->fb && new_state->crtc &&
		new_state->visible;
	premultiplied = !new_state ||
		new_state->pixel_blend_mode != DRM_MODE_BLEND_COVERAGE;
	if (enabled) {
		mutex_lock(&ms912x->cursor_prepare_lock);
		ret = ms912x_cursor_copy_framebuffer(new_state, new_pixels);
		if (ret) {
			mutex_unlock(&ms912x->cursor_prepare_lock);
			dev_warn_ratelimited(&ms912x->intf->dev,
					     "failed to read cursor framebuffer: %d\n",
					     ret);
			mutex_lock(&ms912x->transfer_lock);
			ms912x->cursor_commit_pending = false;
			ms912x->cursor_commit_sequence = 0;
			ms912x_schedule_conversion_locked(ms912x);
			mutex_unlock(&ms912x->transfer_lock);
			return;
		}
	}

	enabled &= new_pixels != NULL;

	mutex_lock(&ms912x->transfer_lock);
	if (ms912x->transfer_stopping) {
		ms912x->cursor_commit_pending = false;
		ms912x->cursor_commit_sequence = 0;
		mutex_unlock(&ms912x->transfer_lock);
		if (enabled)
			mutex_unlock(&ms912x->cursor_prepare_lock);
		return;
	}
	scale_mode = ms912x->scale_mode;
	width = ms912x->frame_snapshot_width;
	height = ms912x->frame_snapshot_height;
	if (width && height) {
		ms912x->pending_width = width;
		ms912x->pending_height = height;
	}
	had_pending = ms912x->pending_rect_count != 0;
	old_valid = ms912x->cursor_enabled && width && height &&
		ms912x_cursor_bounds(ms912x->cursor_x, ms912x->cursor_y,
				     ms912x->cursor_width,
				     ms912x->cursor_height,
				     width, height, &old_rect);
	if (enabled) {
		source_unchanged = ms912x->cursor_plane_active &&
			plane_width == ms912x->cursor_plane_width &&
			plane_height == ms912x->cursor_plane_height &&
			ms912x_cursor_source_matches(
				new_pixels, ms912x->cursor_source_pixels,
				plane_width, plane_height);
		prepared_unchanged = source_unchanged &&
			scale_mode == ms912x->cursor_scale_mode;
		if (prepared_unchanged) {
			cursor_width = ms912x->cursor_width;
			cursor_height = ms912x->cursor_height;
			source_height = ms912x->cursor_source_height;
			alpha_x = ms912x->cursor_alpha_x;
			alpha_y = ms912x->cursor_alpha_y;
			if (scale_mode != MS912X_SCALE_MODE_NATIVE) {
				cursor_x = ms912x_cursor_scaled_coordinate(
					cursor_x, scale_mode);
				cursor_y = ms912x_cursor_scaled_coordinate(
					cursor_y, scale_mode);
			}
			cursor_x += alpha_x;
			cursor_y += alpha_y;
		} else {
			const u32 *source_pixels;

			source_height = plane_height;
			if (source_unchanged)
				source_pixels = ms912x->cursor_source_pixels;
			else {
				memcpy(ms912x->cursor_source_pixels, new_pixels,
				       MS912X_CURSOR_MAX_WIDTH * plane_height *
				       sizeof(*new_pixels));
				source_pixels = new_pixels;
			}
			memcpy(ms912x->cursor_pixels, source_pixels,
				MS912X_CURSOR_MAX_WIDTH * plane_height *
				sizeof(*source_pixels));
			if (scale_mode != MS912X_SCALE_MODE_NATIVE) {
				cursor_width = ms912x_scale_dimension(plane_width,
								 scale_mode);
				cursor_height = ms912x_scale_dimension(plane_height,
								  scale_mode);
				ms912x_downscale_xrgb8888(scale_mode,
					ms912x->cursor_pixels, ms912x->cursor_pixels,
					plane_width, plane_height,
					cursor_width, cursor_height,
					MS912X_CURSOR_MAX_WIDTH,
					MS912X_CURSOR_MAX_WIDTH);
				source_height = cursor_height;
				cursor_x = ms912x_cursor_scaled_coordinate(
					cursor_x, scale_mode);
				cursor_y = ms912x_cursor_scaled_coordinate(
					cursor_y, scale_mode);
			}
			if (ms912x_cursor_alpha_bounds(ms912x->cursor_pixels,
						       cursor_width,
						       cursor_height, &alpha_x, &alpha_y,
						       &cursor_width, &cursor_height)) {
				cursor_x += alpha_x;
				cursor_y += alpha_y;
			} else {
				cursor_width = 0;
				cursor_height = 0;
			}
		}
	} else {
		cursor_width = 0;
		cursor_height = 0;
	}

	ms912x->cursor_enabled = enabled && cursor_width && cursor_height;
	ms912x->cursor_x = cursor_x;
	ms912x->cursor_y = cursor_y;
	ms912x->cursor_width = cursor_width;
	ms912x->cursor_height = cursor_height;
	ms912x->cursor_alpha_x = alpha_x;
	ms912x->cursor_alpha_y = alpha_y;
	ms912x->cursor_source_height = source_height;
	ms912x->cursor_plane_active = enabled;
	ms912x->cursor_plane_x = plane_x;
	ms912x->cursor_plane_y = plane_y;
	ms912x->cursor_plane_width = plane_width;
	ms912x->cursor_plane_height = plane_height;
	ms912x->cursor_scale_mode = scale_mode;
	ms912x->cursor_fb_id = cursor_fb_id;
	ms912x->cursor_premultiplied = premultiplied;
	new_valid = ms912x->cursor_enabled && width && height &&
		ms912x_cursor_bounds(
		cursor_x, cursor_y, cursor_width, cursor_height,
		width, height, &new_rect);
	if (old_valid)
		ms912x_payload_invalidate_damage(ms912x, &old_rect, 1);
	if (new_valid)
		ms912x_payload_invalidate_damage(ms912x, &new_rect, 1);
	if (!width || !height) {
		if (ms912x->cursor_commit_pending &&
		    ms912x->cursor_commit_sequence >
			ms912x->published_frame_sequence)
			goto out_unlock;
		ms912x->cursor_commit_pending = false;
		ms912x->cursor_commit_sequence = 0;
		goto schedule;
	}
	if (old_valid)
		ms912x_pending_add_rect_locked(ms912x, &old_rect);
	if (new_valid)
		ms912x_pending_add_rect_locked(ms912x, &new_rect);
	ms912x->pending_width = width;
	ms912x->pending_height = height;
	ms912x_pending_compact_locked(ms912x);

	if (!ms912x->cursor_commit_pending ||
	    ms912x->cursor_commit_sequence <= ms912x->published_frame_sequence) {
		ms912x->cursor_commit_pending = false;
		ms912x->cursor_commit_sequence = 0;
	}
	schedule:
	ms912x_pending_update_deadline_locked(ms912x, had_pending);
	ms912x_schedule_conversion_locked(ms912x);
	out_unlock:
	mutex_unlock(&ms912x->transfer_lock);
	if (enabled)
		mutex_unlock(&ms912x->cursor_prepare_lock);
}

void ms912x_cursor_reconfigure(struct ms912x_device *ms912x,
			       enum ms912x_scale_mode scale_mode)
{
	unsigned int width, height, source_height, alpha_x = 0, alpha_y = 0;
	int cursor_x, cursor_y;
	bool visible;

	mutex_lock(&ms912x->transfer_lock);
	if (!ms912x->cursor_plane_active || !ms912x->cursor_plane_width ||
	    !ms912x->cursor_plane_height) {
		mutex_unlock(&ms912x->transfer_lock);
		return;
	}
	width = ms912x->cursor_plane_width;
	height = ms912x->cursor_plane_height;
	cursor_x = ms912x->cursor_plane_x;
	cursor_y = ms912x->cursor_plane_y;
	source_height = height;
	memcpy(ms912x->cursor_pixels, ms912x->cursor_source_pixels,
	       MS912X_CURSOR_MAX_WIDTH * height *
	       sizeof(*ms912x->cursor_pixels));
	if (scale_mode != MS912X_SCALE_MODE_NATIVE) {
		width = ms912x_scale_dimension(width, scale_mode);
		height = ms912x_scale_dimension(height, scale_mode);
		ms912x_downscale_xrgb8888(scale_mode,
			ms912x->cursor_pixels, ms912x->cursor_pixels,
			ms912x->cursor_plane_width, ms912x->cursor_plane_height,
			width, height, MS912X_CURSOR_MAX_WIDTH,
			MS912X_CURSOR_MAX_WIDTH);
		cursor_x = ms912x_cursor_scaled_coordinate(cursor_x, scale_mode);
		cursor_y = ms912x_cursor_scaled_coordinate(cursor_y, scale_mode);
		source_height = height;
	}
	visible = ms912x_cursor_alpha_bounds(
		ms912x->cursor_pixels, width, height, &alpha_x, &alpha_y,
		&ms912x->cursor_width, &ms912x->cursor_height);
	if (visible) {
		cursor_x += alpha_x;
		cursor_y += alpha_y;
	} else {
		ms912x->cursor_width = 0;
		ms912x->cursor_height = 0;
	}
	ms912x->cursor_x = cursor_x;
	ms912x->cursor_y = cursor_y;
	ms912x->cursor_alpha_x = alpha_x;
	ms912x->cursor_alpha_y = alpha_y;
	ms912x->cursor_source_height = source_height;
	ms912x->cursor_scale_mode = scale_mode;
	ms912x->cursor_enabled = visible;
	ms912x_payload_invalidate_all(ms912x);
	mutex_unlock(&ms912x->transfer_lock);
}
static void ms912x_rect_align_x4(struct drm_rect *rect,
				 unsigned int fb_width)
{
	int x = ALIGN_DOWN(rect->x1, 4);
	int x2 = min_t(int, ALIGN(rect->x2, 4), fb_width);

	rect->x1 = x;
	rect->x2 = x2;
}

static unsigned long
ms912x_transfer_coalesce_delay(struct ms912x_device *ms912x)
{
	unsigned int refresh_hz = READ_ONCE(ms912x->display_refresh_hz);
	u64 bytes = ms912x_rects_payload_bytes(ms912x->pending_rects,
					     ms912x->pending_rect_count);
	u64 delay_ns;

	if (!refresh_hz)
		refresh_hz = 60;
	delay_ns = ms912x_batch_coalesce_ns(bytes,
		div64_u64(NSEC_PER_SEC, 2ULL * refresh_hz),
		ms912x->present_queue_pressure_ns,
		ms912x->present_queue_high_watermark_ns,
		ms912x->present_backpressured, ms912x->pending_force_full);
	return delay_ns ? max_t(unsigned long, 1, nsecs_to_jiffies(delay_ns)) : 0;
}

/* Caller holds transfer_lock. Later updates may shorten, never renew, the wait. */
static void ms912x_pending_update_deadline_locked(struct ms912x_device *ms912x,
						 bool had_pending)
{
	unsigned long delay;

	ms912x_transfer_update_pacing_locked(ms912x);
	delay = ms912x_transfer_coalesce_delay(ms912x);
	if (!delay)
		ms912x->pending_deadline = 0;
	else if (!had_pending)
		ms912x->pending_deadline = jiffies + delay;
}

static bool ms912x_transfer_has_idle_request(struct ms912x_device *ms912x)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(ms912x->requests); i++) {
		if (ms912x->requests[i].state == MS912X_REQUEST_FREE)
			return true;
	}

	return false;
}

static unsigned long
ms912x_transfer_pending_delay(struct ms912x_device *ms912x)
{
	if (ms912x->pending_force_full || !ms912x->pending_deadline ||
	    time_after_eq(jiffies, ms912x->pending_deadline))
		return 0;

	return ms912x->pending_deadline - jiffies;
}

/* Caller holds transfer_lock. */
void ms912x_schedule_conversion_locked(struct ms912x_device *ms912x)
{
	if (!ms912x->transfer_stopping &&
	    !ms912x->cursor_commit_pending &&
	    ms912x->pending_valid &&
	    !ms912x->conversion_busy &&
	    ms912x_transfer_has_idle_request(ms912x))
		mod_delayed_work(ms912x->conversion_workqueue,
				 &ms912x->conversion_work,
				 ms912x_transfer_pending_delay(ms912x));
}

void ms912x_transfer_force_full_update(struct ms912x_device *ms912x)
{
	struct drm_rect full;
	int idx;

	if (!drm_dev_enter(&ms912x->drm, &idx))
		return;

	mutex_lock(&ms912x->transfer_lock);
	if (ms912x->transfer_stopping || !ms912x->frame_snapshot_width ||
	    !ms912x->frame_snapshot_height)
		goto out_unlock;
	full = DRM_RECT_INIT(0, 0, ms912x->frame_snapshot_width,
			     ms912x->frame_snapshot_height);
	ms912x->pending_rect_count = 0;
	ms912x->pending_valid = false;
	ms912x->pending_force_full = true;
	ms912x->pending_width = ms912x->frame_snapshot_width;
	ms912x->pending_height = ms912x->frame_snapshot_height;
	ms912x->pending_deadline = 0;
	ms912x_payload_invalidate_all(ms912x);
	ms912x_pending_add_rect_locked(ms912x, &full);
	ms912x_schedule_conversion_locked(ms912x);
	ms912x_transfer_update_pacing_locked(ms912x);
out_unlock:
	mutex_unlock(&ms912x->transfer_lock);
	drm_dev_exit(idx);
}

static void ms912x_snapshot_capture_damage(struct ms912x_device *ms912x,
				      struct drm_framebuffer *fb,
				      const struct iosys_map *map,
				      const struct drm_rect *rect)
{
	unsigned int y;
	size_t width = drm_rect_width(rect);

	if (ms912x->scale_mode == MS912X_SCALE_MODE_DOWNSCALE_2_TO_1) {
		u32 *staging = ms912x->damage_scan_snapshot_back;
		unsigned int source_x = rect->x1 * 2;
		unsigned int source_y = rect->y1 * 2;
		unsigned int source_width = min_t(unsigned int,
			width * 2, fb->width - source_x);
		unsigned int source_height = min_t(unsigned int,
			drm_rect_height(rect) * 2, fb->height - source_y);

		for (y = 0; y < source_height; y++)
			iosys_map_memcpy_from(staging + y * MS912X_MAX_WIDTH,
					     map,
					     (source_y + y) * fb->pitches[0] +
					     source_x * sizeof(*staging),
					     source_width * sizeof(*staging));
		ms912x_downscale_xrgb8888_region(MS912X_SCALE_MODE_DOWNSCALE_2_TO_1,
			staging, ms912x->latest_xrgb +
			rect->y1 * MS912X_MAX_WIDTH + rect->x1,
			source_width, source_height, width,
			drm_rect_height(rect), MS912X_MAX_WIDTH,
			MS912X_MAX_WIDTH, 0, 0);
		return;
	}

	if (ms912x->scale_mode == MS912X_SCALE_MODE_DOWNSCALE_3_TO_2) {
		u32 *staging = ms912x->damage_scan_snapshot_back;
		unsigned int center_x = rect->x1 / 2 * 3;
		unsigned int center_y = rect->y1 / 2 * 3;
		unsigned int source_x = center_x ? center_x - 1 : 0;
		unsigned int source_y = center_y ? center_y - 1 : 0;
		unsigned int source_width = min_t(unsigned int,
			center_x + DIV_ROUND_UP(width * 3, 2) + 1, fb->width) - source_x;
		unsigned int source_height = min_t(unsigned int,
			center_y + DIV_ROUND_UP(drm_rect_height(rect) * 3, 2) + 1,
			fb->height) - source_y;

		for (y = 0; y < source_height; y++)
			iosys_map_memcpy_from(staging + y * MS912X_MAX_WIDTH,
					     map,
					     (source_y + y) * fb->pitches[0] +
					     source_x * sizeof(*staging),
					     source_width * sizeof(*staging));
		ms912x_downscale_xrgb8888_region(MS912X_SCALE_MODE_DOWNSCALE_3_TO_2,
			staging, ms912x->latest_xrgb +
			rect->y1 * MS912X_MAX_WIDTH + rect->x1,
			source_width, source_height, width,
			drm_rect_height(rect), MS912X_MAX_WIDTH,
			MS912X_MAX_WIDTH, center_x - source_x, center_y - source_y);
		return;
	}

	for (y = rect->y1; y < rect->y2; y++) {
		u32 *dest = ms912x->latest_xrgb +
			y * MS912X_MAX_WIDTH + rect->x1;
		size_t offset = y * fb->pitches[0] + rect->x1 * sizeof(*dest);

		iosys_map_memcpy_from(dest, map, offset, width * sizeof(*dest));
	}
}

static void ms912x_scan_frame_worker(struct work_struct *work)
{
	struct ms912x_device *ms912x =
		container_of(work, struct ms912x_device, scan_work);

	for (;;) {
		struct ms912x_scan_frame *frame = NULL;
		struct drm_rect *rects = ms912x->scan_damage_rects;
		unsigned int rect_count = 0, i, slot;
		unsigned int width, height;
		u32 generation;
		u64 sequence;
		bool force_full, published = false;
		int ret, idx;

		mutex_lock(&ms912x->scan_queue_lock);
		if (ms912x->scan_stopping) {
			for (slot = 0; slot < ARRAY_SIZE(ms912x->scan_frames); slot++)
				ms912x->scan_frames[slot].queued = false;
			ms912x->scan_work_scheduled = false;
			mutex_unlock(&ms912x->scan_queue_lock);
			return;
		}
		for (slot = 0; slot < ARRAY_SIZE(ms912x->scan_frames); slot++) {
			struct ms912x_scan_frame *candidate =
				&ms912x->scan_frames[slot];

			if (!candidate->queued)
				continue;
			if (!frame || candidate->sequence < frame->sequence)
				frame = candidate;
		}
		if (!frame) {
			ms912x->scan_work_scheduled = false;
			mutex_unlock(&ms912x->scan_queue_lock);
			return;
		}
		frame->queued = false;
		frame->running = true;
		width = frame->width;
		height = frame->height;
		generation = frame->generation;
		sequence = frame->sequence;
		force_full = frame->force_full;
		mutex_unlock(&ms912x->scan_queue_lock);

		/* Drop only snapshots from an obsolete modeset/size epoch. */
		if (generation != READ_ONCE(ms912x->frame_generation)) {
			goto frame_done;
		}
		if (!drm_dev_enter(&ms912x->drm, &idx))
			goto frame_done;
		ret = ms912x_damage_scan_find_snapshot_changes(
			ms912x, frame->pixels, sequence, generation,
			ms912x->damage_scan_snapshot,
			ms912x->damage_scan_snapshot_back, width, height, force_full,
			&ms912x->scan_damage_rects,
			&ms912x->scan_damage_rect_capacity, &rect_count);
		rects = ms912x->scan_damage_rects;
		if (ret >= 0 && generation == READ_ONCE(ms912x->frame_generation)) {
			mutex_lock(&ms912x->transfer_lock);
			mutex_lock(&ms912x->damage_scan_lock);
			if (!ms912x->transfer_stopping &&
			    generation == ms912x->frame_generation &&
			    !atomic_read(&ms912x->damage_scan_invalid)) {
				bool full_update = force_full ||
					(rect_count == 1 && rects[0].x1 == 0 &&
					 rects[0].y1 == 0 && rects[0].x2 == width &&
					 rects[0].y2 == height);
				bool had_pending = ms912x->pending_rect_count != 0;
				u32 *buffer;

				ms912x_payload_invalidate_frame(ms912x, frame->pixels,
							       width, height);
				/* Publish the complete frame by transferring buffer ownership. */
				buffer = ms912x->latest_xrgb;
				ms912x->latest_xrgb = frame->pixels;
				frame->pixels = buffer;
				buffer = ms912x->damage_scan_snapshot;
				ms912x->damage_scan_snapshot =
					ms912x->damage_scan_snapshot_back;
				ms912x->damage_scan_snapshot_back = buffer;
				ms912x->published_frame_sequence = sequence;
				published = true;
				if (full_update) {
					ms912x->pending_rect_count = 0;
					ms912x->pending_valid = false;
					ms912x->pending_force_full = false;
					ms912x->pending_deadline = 0;
					had_pending = false;
				}
				if (rect_count && had_pending)
					MS912X_STAT_INC(ms912x,
							damage_batched_updates);
				ms912x->pending_width = width;
				ms912x->pending_height = height;
				for (i = 0; i < rect_count; i++) {
					ms912x_pending_add_rect_locked(ms912x,
							       &rects[i]);
				}
				ms912x->pending_width = width;
				ms912x->pending_height = height;
				ms912x_pending_compact_locked(ms912x);
				ms912x->pending_force_full |= full_update;
				ms912x_pending_update_deadline_locked(ms912x, had_pending);
				if (rect_count || ms912x->pending_valid) {
					if (ms912x->cursor_commit_pending &&
					    sequence >=
					    ms912x->cursor_commit_sequence) {
						ms912x->cursor_commit_pending = false;
						ms912x->cursor_commit_sequence = 0;
					}
					ms912x_schedule_conversion_locked(ms912x);
				}
				if (ms912x_rects_payload_bytes(rects, rect_count) <
				    MS912X_SMALL_DAMAGE_THRESHOLD_BYTES) {
					WRITE_ONCE(ms912x->small_damage_sequence,
						   sequence);
					wake_up_all(&ms912x->frame_present_wait);
				}
			}
			mutex_unlock(&ms912x->damage_scan_lock);
			mutex_unlock(&ms912x->transfer_lock);
			if (!published)
				ms912x_damage_scan_invalidate(ms912x);
		} else if (ret < 0) {
			ms912x_damage_scan_invalidate(ms912x);
			mutex_lock(&ms912x->transfer_lock);
			ms912x->failed_present_sequence = max(
				ms912x->failed_present_sequence, sequence);
			if (ms912x->cursor_commit_pending &&
			    sequence >= ms912x->cursor_commit_sequence) {
				ms912x->cursor_commit_pending = false;
				ms912x->cursor_commit_sequence = 0;
				ms912x_schedule_conversion_locked(ms912x);
			}
			wake_up_all(&ms912x->frame_present_wait);
			mutex_unlock(&ms912x->transfer_lock);
			drm_err_ratelimited(&ms912x->drm,
					    "async damage scan failed: %d\n", ret);
		} else {
			/* A newer modeset superseded this queued snapshot. */
			ms912x_damage_scan_invalidate(ms912x);
		}
		drm_dev_exit(idx);

frame_done:
		mutex_lock(&ms912x->scan_queue_lock);
		frame->running = false;
		mutex_unlock(&ms912x->scan_queue_lock);
		mutex_lock(&ms912x->transfer_lock);
		if (ms912x->cursor_commit_pending &&
		    generation != ms912x->frame_generation &&
		    sequence >= ms912x->cursor_commit_sequence) {
			ms912x->cursor_commit_pending = false;
			ms912x->cursor_commit_sequence = 0;
			ms912x_schedule_conversion_locked(ms912x);
		}
		ms912x_transfer_update_pacing_locked(ms912x);
		mutex_unlock(&ms912x->transfer_lock);
	}
}

static int ms912x_scan_enqueue_frame_locked(struct ms912x_device *ms912x,
					    struct drm_framebuffer *fb,
					    const struct iosys_map *map,
					    bool force_full,
					    u64 sequence)
{
	struct ms912x_scan_frame *slot = NULL;
	unsigned int input_width = ms912x->video_in_width ?
		ms912x->video_in_width : fb->width;
	unsigned int input_height = ms912x->video_in_height ?
		ms912x->video_in_height : fb->height;
	enum ms912x_scale_mode scale_mode = ms912x->scale_mode;
	unsigned int i, y;
	bool queue_scan = false;

	mutex_lock(&ms912x->scan_queue_lock);
	if (ms912x->scan_stopping) {
		mutex_unlock(&ms912x->scan_queue_lock);
		return -ESHUTDOWN;
	}
	for (i = 0; i < ARRAY_SIZE(ms912x->scan_frames); i++) {
		struct ms912x_scan_frame *candidate = &ms912x->scan_frames[i];

		if (!candidate->queued && !candidate->running) {
			slot = candidate;
			break;
		}
	}
	if (!slot) {
		/* Keep the running scan; replace the newest waiting snapshot. */
		for (i = 0; i < ARRAY_SIZE(ms912x->scan_frames); i++) {
			struct ms912x_scan_frame *candidate =
				&ms912x->scan_frames[i];

			if (candidate->queued &&
			    (!slot || candidate->sequence > slot->sequence))
				slot = candidate;
		}
	}
	if (!slot) {
		mutex_unlock(&ms912x->scan_queue_lock);
		return -EBUSY;
	}

	force_full |= slot->queued && slot->force_full;
	for (y = 0; y < fb->height; y++)
		iosys_map_memcpy_from(slot->pixels + y * MS912X_MAX_WIDTH, map,
				     y * fb->pitches[0],
				     fb->width * sizeof(*slot->pixels));
	if (scale_mode != MS912X_SCALE_MODE_NATIVE)
		ms912x_downscale_xrgb8888(scale_mode,
			slot->pixels, slot->pixels, fb->width, fb->height,
			input_width, input_height, MS912X_MAX_WIDTH,
			MS912X_MAX_WIDTH);
	slot->width = input_width;
	slot->height = input_height;
	slot->framebuffer_width = fb->width;
	slot->framebuffer_height = fb->height;

	slot->generation = ms912x->frame_generation;
	slot->force_full = force_full;
	slot->sequence = sequence;
	if (!slot->queued) {
		slot->queued = true;
		if (!ms912x->scan_work_scheduled) {
			ms912x->scan_work_scheduled = true;
			queue_scan = true;
		}
	}
	mutex_unlock(&ms912x->scan_queue_lock);
	if (queue_scan)
		queue_work(system_dfl_wq, &ms912x->scan_work);
	return 0;
}

static bool ms912x_scan_is_pending(struct ms912x_device *ms912x)
{
	unsigned int i;
	bool pending = false;

	mutex_lock(&ms912x->scan_queue_lock);
	for (i = 0; i < ARRAY_SIZE(ms912x->scan_frames); i++)
		pending |= ms912x->scan_frames[i].queued ||
			ms912x->scan_frames[i].running;
	mutex_unlock(&ms912x->scan_queue_lock);
	return pending;
}

/* Caller holds transfer_lock. Exclude the USB packet currently in flight. */
static u64 ms912x_present_queue_bytes_locked(struct ms912x_device *ms912x)
{
	u64 bytes = 0;
	unsigned int i;

	for (i = 0; i < ms912x->pending_rect_count; i++)
		bytes += (u64)drm_rect_width(&ms912x->pending_rects[i]) *
			drm_rect_height(&ms912x->pending_rects[i]) * 2 +
			MS912X_FRAME_OVERHEAD;
	if (ms912x->failed_rect_valid)
		bytes += (u64)drm_rect_width(&ms912x->failed_rect) *
			drm_rect_height(&ms912x->failed_rect) * 2 +
			MS912X_FRAME_OVERHEAD;

	for (i = 0; i < ARRAY_SIZE(ms912x->requests); i++)
		if (ms912x->requests[i].state != MS912X_REQUEST_FREE &&
		    ms912x->requests[i].state != MS912X_REQUEST_USB_ACTIVE)
			bytes += ms912x->requests[i].transfer_len;
	return bytes;
}

/* Caller holds transfer_lock. Pace against queued-byte drain time. */
void ms912x_transfer_update_pacing_locked(struct ms912x_device *ms912x)
{
	u64 bytes = ms912x_present_queue_bytes_locked(ms912x);
	u64 usb_rate = max_t(u64,
			     ms912x->usb_speed_mean_bytes_per_sec, 1);
	unsigned int refresh_hz = READ_ONCE(ms912x->display_refresh_hz);
	u64 refresh_period_denom;
	u64 low_watermark_ms, high_watermark_ms;
	u64 low_watermark_ns, high_watermark_ns, pressure_ns;

	if (!refresh_hz)
		refresh_hz = 60;
	refresh_period_denom = (u64)refresh_hz * NSEC_PER_MSEC;
	low_watermark_ms = div64_u64(NSEC_PER_SEC, 2 * refresh_period_denom);
	high_watermark_ms = DIV_ROUND_UP_ULL(2ULL * NSEC_PER_SEC,
					     refresh_period_denom);
	low_watermark_ns = low_watermark_ms * NSEC_PER_MSEC;
	high_watermark_ns = high_watermark_ms * NSEC_PER_MSEC;

	pressure_ns = div64_u64(bytes * NSEC_PER_SEC, usb_rate);
	WRITE_ONCE(ms912x->present_queue_pressure_ns, pressure_ns);
	WRITE_ONCE(ms912x->present_queue_low_watermark_ns,
		   low_watermark_ns);
	WRITE_ONCE(ms912x->present_queue_high_watermark_ns,
		   high_watermark_ns);

	ms912x->present_backpressured = ms912x_batch_backpressured(
		pressure_ns, low_watermark_ns, high_watermark_ns,
		ms912x->present_backpressured);

	if (!ms912x->present_backpressured &&
	    ms912x->failed_present_sequence <
		ms912x->published_frame_sequence &&
	    ms912x->published_frame_sequence > ms912x->drm_released_sequence) {
		ms912x->drm_released_sequence =
			ms912x->published_frame_sequence;
		wake_up_all(&ms912x->frame_present_wait);
	}
}

static unsigned long ms912x_small_damage_wait_jiffies(
						struct ms912x_device *ms912x)
{
	u64 usb_rate = max_t(u64,
			     READ_ONCE(ms912x->usb_speed_mean_bytes_per_sec), 1);
	u64 wait_ns = div64_u64(
		(u64)MS912X_SMALL_DAMAGE_THRESHOLD_BYTES * NSEC_PER_SEC,
		usb_rate);

	return max_t(unsigned long, 1, nsecs_to_jiffies(wait_ns));
}

static int ms912x_wait_for_pacing_release(struct ms912x_device *ms912x,
					  u64 sequence, u32 generation)
{
	bool stats_enabled = ms912x_stats_enabled();
	u64 start_ns = stats_enabled ? ktime_get_ns() : 0;
	unsigned long wait_start = jiffies;
	unsigned long small_damage_cap =
		ms912x_small_damage_wait_jiffies(ms912x);
	unsigned long elapsed, remaining;
	long waited;

	if (stats_enabled)
		MS912X_STAT_INC(ms912x, drm_present_wait_frames);
	waited = wait_event_timeout(ms912x->frame_present_wait,
		READ_ONCE(ms912x->transfer_stopping) ||
		READ_ONCE(ms912x->frame_generation) != generation ||
		READ_ONCE(ms912x->failed_present_sequence) >= sequence ||
		READ_ONCE(ms912x->drm_released_sequence) >= sequence ||
		READ_ONCE(ms912x->small_damage_sequence) == sequence,
		msecs_to_jiffies(5000));
	if (READ_ONCE(ms912x->small_damage_sequence) == sequence &&
	    READ_ONCE(ms912x->drm_released_sequence) < sequence &&
	    !READ_ONCE(ms912x->transfer_stopping) &&
	    READ_ONCE(ms912x->frame_generation) == generation &&
	    READ_ONCE(ms912x->failed_present_sequence) < sequence) {
		elapsed = jiffies - wait_start;
		remaining = elapsed < small_damage_cap ?
			small_damage_cap - elapsed : 0;
		if (remaining)
			wait_event_timeout(ms912x->frame_present_wait,
				READ_ONCE(ms912x->transfer_stopping) ||
				READ_ONCE(ms912x->frame_generation) != generation ||
				READ_ONCE(ms912x->failed_present_sequence) >= sequence ||
				READ_ONCE(ms912x->drm_released_sequence) >= sequence,
				remaining);

		/* Small damage must not wait longer than its capped USB service time. */
		mutex_lock(&ms912x->transfer_lock);
		if (ms912x->small_damage_sequence == sequence &&
		    ms912x->frame_generation == generation &&
		    ms912x->failed_present_sequence < sequence &&
		    ms912x->drm_released_sequence < sequence) {
			ms912x->drm_released_sequence = sequence;
			wake_up_all(&ms912x->frame_present_wait);
		}
		mutex_unlock(&ms912x->transfer_lock);
	}
	if (stats_enabled)
		atomic64_add(ktime_get_ns() - start_ns,
			     &ms912x->stats.drm_present_wait_ns);
	if (!waited && READ_ONCE(ms912x->small_damage_sequence) != sequence) {
		if (stats_enabled)
			MS912X_STAT_INC(ms912x, drm_present_wait_timeouts);
		drm_warn(&ms912x->drm,
			 "timed out waiting for USB queue pacing on frame %llu\n",
			 (unsigned long long)sequence);
		return -ETIMEDOUT;
	}
	if (READ_ONCE(ms912x->transfer_stopping))
		return -ENODEV;
	if (READ_ONCE(ms912x->drm_released_sequence) >= sequence)
		return 0;
	if (READ_ONCE(ms912x->failed_present_sequence) >= sequence)
		return -EIO;
	return 0;
}

static bool ms912x_snapshot_stage_conversion(struct ms912x_device *ms912x,
						     const struct drm_rect *rects,
						     unsigned int rect_count,
					     unsigned int width,
					     unsigned int height)
{
	unsigned int i, y;

	if (!rect_count || rect_count > MS912X_MAX_BATCH_RECTS)
		return false;
	for (i = 0; i < rect_count; i++) {
		const struct drm_rect *rect = &rects[i];
		size_t rect_width;

		if (WARN_ON_ONCE(!ms912x_rect_valid_for_frame(rect, width, height)))
			return false;
		rect_width = drm_rect_width(rect);
		for (y = rect->y1; y < rect->y2; y++) {
			u32 *src = ms912x->latest_xrgb +
				y * MS912X_MAX_WIDTH + rect->x1;
			u32 *dest = ms912x->conversion_xrgb +
				y * MS912X_MAX_WIDTH + rect->x1;

			memcpy(dest, src, rect_width * sizeof(*dest));
		}
	}
	return true;
}

static size_t ms912x_rect_encoded_bytes(const struct drm_rect *rect)
{
	return ms912x_mode3_rect_encoded_bytes(drm_rect_width(rect),
					       drm_rect_height(rect));
}

static bool ms912x_rect_list_transfer_len(const struct drm_rect *rects,
					 unsigned int rect_count,
					 unsigned int width,
					 unsigned int height,
					 size_t *transfer_len)
{
	u64 bytes = 0;
	unsigned int i;

	if (!rect_count || rect_count > MS912X_MAX_BATCH_RECTS)
		return false;
	for (i = 0; i < rect_count; i++) {
		if (!ms912x_rect_valid_for_frame(&rects[i], width, height) ||
		    (rects[i].x1 & 1) || (drm_rect_width(&rects[i]) & 1))
			return false;
		bytes += ms912x_rect_encoded_bytes(&rects[i]);
		if (bytes > MS912X_MAX_TRANSFER_LEN)
			return false;
	}
	*transfer_len = bytes;
	return true;
}

static u64 ms912x_request_finish_batch(struct ms912x_usb_request *request)
{
	size_t offset = 0;
	u64 cursor_pixels = 0;
	unsigned int i;

	for (i = 0; i < request->rect_count; i++) {
		const struct drm_rect *rect = &request->rects[i];
		void *rect_buffer = (u8 *)request->transfer_buffer + offset;

		if (request->cursor_included &&
		    i + 1 == request->rect_count)
			cursor_pixels += ms912x_cursor_composite_uyvy(
				rect_buffer, rect, request->cursor_pixels,
				request->cursor_x, request->cursor_y,
				request->cursor_width, request->cursor_height,
				request->cursor_alpha_x, request->cursor_alpha_y,
				request->cursor_premultiplied);
		if (READ_ONCE(ms912x_parameters.dbg_show_draws))
			ms912x_debug_draw_uyvy_border(rect_buffer, rect, rect);
		offset += ms912x_rect_encoded_bytes(rect);
	}
	if (cursor_pixels) {
		MS912X_STAT_INC(request->ms912x, cursor_composite_packets);
		MS912X_STAT_ADD(request->ms912x, cursor_composite_pixels,
				cursor_pixels);
	}
	return cursor_pixels;
}

/* Keep the complete cursor in the final rectangle used for composition. */
static void ms912x_request_include_cursor_locked(
	struct ms912x_device *ms912x, struct ms912x_usb_request *request,
	unsigned int *count, unsigned int width, unsigned int height)
{
	struct drm_rect full_rect = DRM_RECT_INIT(0, 0, width, height);
	struct drm_rect cursor_rect;
	unsigned int rect_count = *count;

	ms912x_cursor_snapshot_request_locked(ms912x, request,
					     request->rects, rect_count);
	if (request->cursor_included &&
	    !(rect_count == 1 && drm_rect_equals(&request->rects[0],
						&full_rect))) {
		if (!ms912x_cursor_bounds(ms912x->cursor_x, ms912x->cursor_y,
					 ms912x->cursor_width,
					 ms912x->cursor_height, width, height,
					 &cursor_rect))
			cursor_rect = full_rect;
		ms912x_batch_include_overlay(request->rects, &rect_count,
					    request->rect_capacity, &cursor_rect,
					    ms912x_rect_encoded_bytes(&full_rect));
	}
	*count = rect_count;
}

/* Refresh a queued YUV payload if its source image changed while waiting. */
int ms912x_usb_request_refresh(struct ms912x_usb_request *request)
{
	struct ms912x_device *ms912x = request->ms912x;
	unsigned int width = request->width, height = request->height;
	unsigned int rect_count = request->rect_count, i;
	u64 source_generation, source_sequence, pixels;
	u64 start_ns = 0, conversion_ns;
	size_t transfer_len;
	bool stats_enabled = ms912x_stats_enabled();
	int ret;

	for (;;) {
		mutex_lock(&ms912x->transfer_lock);
		if (ms912x->transfer_stopping ||
		    request->generation != ms912x->frame_generation) {
			mutex_unlock(&ms912x->transfer_lock);
			return -ECANCELED;
		}
		if (request->source_generation == request->content_generation) {
			mutex_unlock(&ms912x->transfer_lock);
			return 0;
		}
		if (ms912x->conversion_busy) {
			mutex_unlock(&ms912x->transfer_lock);
			wait_event(ms912x->conversion_wait,
				   !READ_ONCE(ms912x->conversion_busy) ||
				   READ_ONCE(ms912x->transfer_stopping));
			continue;
		}
		ms912x_request_include_cursor_locked(ms912x, request, &rect_count,
						     width, height);
		if (!ms912x_rect_list_transfer_len(request->rects, rect_count,
						  width, height,
						  &transfer_len)) {
			request->rects[0] = DRM_RECT_INIT(0, 0, width, height);
			request->rect_count = 1;
			rect_count = 1;
			transfer_len = ms912x_rect_encoded_bytes(
				&request->rects[0]);
			ms912x_cursor_snapshot_request_locked(ms912x, request,
							     request->rects, 1);
		}
		request->rect_count = rect_count;
		pixels = 0;
		if (stats_enabled)
			for (i = 0; i < rect_count; i++)
				pixels += (u64)drm_rect_width(&request->rects[i]) *
					drm_rect_height(&request->rects[i]);

		ms912x->conversion_busy = true;
		source_generation = request->content_generation;
		request->source_generation = source_generation;
		source_sequence = ms912x->published_frame_sequence;
		if (!ms912x_snapshot_stage_conversion(ms912x, request->rects,
						      rect_count, width, height)) {
			ms912x->conversion_busy = false;
			wake_up_all(&ms912x->conversion_wait);
			mutex_unlock(&ms912x->transfer_lock);
			return -EINVAL;
		}
		mutex_unlock(&ms912x->transfer_lock);

		if (stats_enabled)
			start_ns = ktime_get_ns();
		ret = ms912x_convert_xrgb8888_to_uyvy_batch(
			request->transfer_buffer,
			(const __le32 *)ms912x->conversion_xrgb,
			ms912x, width, height, request->rects, rect_count);
		if (!ret)
			ms912x_request_finish_batch(request);
		if (stats_enabled) {
			conversion_ns = ktime_get_ns() - start_ns;
			MS912X_STAT_INC(ms912x, conversion_attempts);
			MS912X_STAT_ADD(ms912x, conversion_pixels, pixels);
			if (!ret)
				MS912X_STAT_INC(ms912x, conversion_successes);
			atomic64_add(conversion_ns, &ms912x->stats.conversion_ns);
		}

		mutex_lock(&ms912x->transfer_lock);
		ms912x->conversion_busy = false;
		wake_up_all(&ms912x->conversion_wait);
		if (!ret && !ms912x->transfer_stopping &&
		    request->generation == ms912x->frame_generation) {
			request->source_generation = source_generation;
			request->source_sequence = source_sequence;
			request->transfer_len = transfer_len;
		} else if (!ret) {
			ret = -ECANCELED;
		}
		ms912x_schedule_conversion_locked(ms912x);
		ms912x_transfer_update_pacing_locked(ms912x);
		mutex_unlock(&ms912x->transfer_lock);
		if (ret) {
			ms912x_damage_scan_invalidate(ms912x);
			return ret;
		}
		if (READ_ONCE(ms912x->transfer_stopping))
			return -ECANCELED;
		/* Send this coherent snapshot; later publications remain pending. */
		return 0;
	}
}

/* Caller holds transfer_lock while taking ownership of the pending batch. */
static bool ms912x_conversion_prepare_batch_locked(
		struct ms912x_device *ms912x,
		struct ms912x_usb_request *request,
		unsigned int width, unsigned int height, u32 generation)
{
	struct drm_rect full_rect = DRM_RECT_INIT(0, 0, width, height);
	struct drm_rect *grown;
	unsigned int current_rect_count = ms912x->pending_rect_count;
	unsigned int required_rect_count, rect_count;
	size_t transfer_len;
	bool replay_previous, replay_added = false;
	unsigned int i;

	replay_previous = ms912x->previous_damage_rect_count &&
		ms912x->previous_damage_generation == generation &&
		ms912x->previous_damage_width == width &&
		ms912x->previous_damage_height == height;
	grown = ms912x_rect_vector_grow(
		request->current_rects, &request->current_rect_capacity,
		current_rect_count, MS912X_MAX_DAMAGE_RECTS);
	if (grown)
		request->current_rects = grown;
	if (current_rect_count > request->current_rect_capacity) {
		request->current_rects[0] = ms912x->pending_rects[0];
		ms912x_rect_align_x4(&request->current_rects[0], width);
		for (i = 1; i < current_rect_count; i++) {
			struct drm_rect rect = ms912x->pending_rects[i];

			ms912x_rect_align_x4(&rect, width);
			request->current_rects[0].x1 = min(
				request->current_rects[0].x1, rect.x1);
			request->current_rects[0].y1 = min(
				request->current_rects[0].y1, rect.y1);
			request->current_rects[0].x2 = max(
				request->current_rects[0].x2, rect.x2);
			request->current_rects[0].y2 = max(
				request->current_rects[0].y2, rect.y2);
		}
		current_rect_count = 1;
	} else {
		for (i = 0; i < current_rect_count; i++) {
			request->current_rects[i] = ms912x->pending_rects[i];
			ms912x_rect_align_x4(&request->current_rects[i], width);
		}
	}
	ms912x_rects_compact(request->current_rects, &current_rect_count,
			     ms912x_rect_encoded_bytes(&full_rect));
	required_rect_count = current_rect_count +
		(replay_previous ? ms912x->previous_damage_rect_count : 0) + 1;
	grown = ms912x_rect_vector_grow(request->rects,
					&request->rect_capacity,
					required_rect_count,
					MS912X_MAX_BATCH_RECTS);
	if (grown)
		request->rects = grown;
	if (ms912x->failed_rect_valid &&
	    ms912x->failed_generation == generation &&
	    ms912x->failed_width == width && ms912x->failed_height == height) {
		request->current_rects[0] = full_rect;
		current_rect_count = 1;
	} else {
		replay_added = replay_previous &&
			!(current_rect_count == 1 &&
			  drm_rect_equals(&request->current_rects[0], &full_rect));
	}
	ms912x_batch_build(request->rects, &rect_count, request->rect_capacity,
			   request->current_rects, current_rect_count,
			   ms912x->previous_damage_rects,
			   replay_added ? ms912x->previous_damage_rect_count : 0,
			   ms912x_rect_encoded_bytes(&full_rect));
	if (!ms912x_rect_list_transfer_len(request->rects, rect_count,
					   width, height, &transfer_len)) {
		ms912x_rects_collapse_bounds(request->rects, &rect_count,
					     &request->rects[0]);
		if (!ms912x_rect_list_transfer_len(request->rects, rect_count,
						   width, height,
						   &transfer_len)) {
			request->rects[0] = full_rect;
			rect_count = 1;
		}
	}
	ms912x_request_include_cursor_locked(ms912x, request, &rect_count,
					     width, height);
	if (!ms912x_rect_list_transfer_len(request->rects, rect_count,
					   width, height, &transfer_len)) {
		ms912x_rects_collapse_bounds(request->rects, &rect_count,
					     &request->rects[0]);
		if (!ms912x_rect_list_transfer_len(request->rects, rect_count,
						   width, height,
						   &transfer_len)) {
			request->rects[0] = full_rect;
			rect_count = 1;
			transfer_len = ms912x_rect_encoded_bytes(&full_rect);
		}
	}
	request->rect_count = rect_count;
	request->current_rect_count = current_rect_count;
	request->replay_included = replay_added;
	request->width = width;
	request->height = height;
	request->transfer_len = transfer_len;
	request->generation = generation;
	request->source_generation = request->content_generation;
	return true;
}

static void ms912x_conversion_worker(struct work_struct *work)
{
	struct delayed_work *delayed_work = to_delayed_work(work);
	struct ms912x_device *ms912x =
		container_of(delayed_work, struct ms912x_device, conversion_work);
	struct ms912x_usb_request *request = NULL;
	struct drm_rect full_rect;
	struct drm_rect *grown;
	u64 conversion_start_ns = 0, conversion_ns;
	u64 conversion_pixels;
	u64 source_sequence, content_generation;
	u32 generation;
	unsigned int width, height, i, rect_count, current_rect_count;
	bool stats_enabled;
	int idx, ret;

	if (!drm_dev_enter(&ms912x->drm, &idx))
		return;

	mutex_lock(&ms912x->transfer_lock);
	/* A USB stale-payload refresh may own the shared conversion scratch. */
	if (ms912x->transfer_stopping || ms912x->conversion_busy ||
	    ms912x->cursor_commit_pending || !ms912x->pending_valid ||
	    !ms912x->pending_rect_count) {
		mutex_unlock(&ms912x->transfer_lock);
		drm_dev_exit(idx);
		return;
	}
	stats_enabled = ms912x_stats_enabled();
	for (i = 0; i < ARRAY_SIZE(ms912x->requests); i++) {
		if (ms912x->requests[i].state == MS912X_REQUEST_FREE) {
			request = &ms912x->requests[i];
			break;
		}
	}
	if (!request) {
		mutex_unlock(&ms912x->transfer_lock);
		drm_dev_exit(idx);
		return;
	}

	ms912x->conversion_busy = true;
	request->state = MS912X_REQUEST_CONVERTING;
	request->replay_included = false;
	width = ms912x->pending_width;
	height = ms912x->pending_height;
	generation = ms912x->frame_generation;
	source_sequence = ms912x->published_frame_sequence;
	content_generation = request->content_generation;
	full_rect = DRM_RECT_INIT(0, 0, width, height);
	if (!ms912x_conversion_prepare_batch_locked(ms912x, request, width,
						    height, generation)) {
		ms912x->conversion_busy = false;
		request->state = MS912X_REQUEST_FREE;
		ms912x_pending_add_rect_locked(ms912x, &full_rect);
		ms912x_schedule_conversion_locked(ms912x);
		mutex_unlock(&ms912x->transfer_lock);
		drm_dev_exit(idx);
		return;
	}
	rect_count = request->rect_count;
	current_rect_count = request->current_rect_count;
	if (WARN_ON_ONCE(!ms912x_snapshot_stage_conversion(
			ms912x, request->rects, rect_count, width, height))) {
		ms912x->conversion_busy = false;
		request->state = MS912X_REQUEST_FREE;
		ms912x_pending_add_rect_locked(ms912x, &full_rect);
		ms912x_schedule_conversion_locked(ms912x);
		mutex_unlock(&ms912x->transfer_lock);
		drm_dev_exit(idx);
		return;
	}
	ms912x->pending_rect_count = 0;
	ms912x->pending_valid = false;
	ms912x->pending_force_full = false;
	ms912x->pending_deadline = 0;
	ms912x_transfer_update_pacing_locked(ms912x);
	mutex_unlock(&ms912x->transfer_lock);

	conversion_pixels = 0;
	if (stats_enabled)
		for (i = 0; i < rect_count; i++)
			conversion_pixels += (u64)drm_rect_width(&request->rects[i]) *
				drm_rect_height(&request->rects[i]);
	if (stats_enabled)
		conversion_start_ns = ktime_get_ns();
	ret = ms912x_convert_xrgb8888_to_uyvy_batch(
		request->transfer_buffer,
		(const __le32 *)ms912x->conversion_xrgb, ms912x, width, height,
		request->rects, rect_count);
	if (!ret)
		ms912x_request_finish_batch(request);
	if (stats_enabled) {
		conversion_ns = ktime_get_ns() - conversion_start_ns;
		MS912X_STAT_INC(ms912x, conversion_attempts);
		MS912X_STAT_ADD(ms912x, conversion_pixels, conversion_pixels);
		if (!ret)
			MS912X_STAT_INC(ms912x, conversion_successes);
		atomic64_add(conversion_ns, &ms912x->stats.conversion_ns);
	}

	mutex_lock(&ms912x->transfer_lock);
	ms912x->conversion_busy = false;
	if (ret || ms912x->transfer_stopping ||
	    generation != ms912x->frame_generation) {
		request->state = MS912X_REQUEST_FREE;
		if (ret) {
			ms912x->failed_present_sequence = max(
				ms912x->failed_present_sequence, source_sequence);
			ms912x->pending_rect_count = 0;
			ms912x->pending_valid = false;
			ms912x->pending_force_full = true;
			ms912x->pending_width = width;
			ms912x->pending_height = height;
			ms912x_pending_add_rect_locked(ms912x, &full_rect);
		}
		ms912x_schedule_conversion_locked(ms912x);
		ms912x_transfer_update_pacing_locked(ms912x);
		wake_up_all(&ms912x->conversion_wait);
		mutex_unlock(&ms912x->transfer_lock);
		if (ret) {
			ms912x_damage_scan_invalidate(ms912x);
			drm_err_ratelimited(&ms912x->drm,
					    "frame conversion failed: %d\n", ret);
		}
		drm_dev_exit(idx);
		return;
	}

	request->source_sequence = source_sequence;
	request->generation = generation;
	request->source_generation = content_generation;
	request->state = MS912X_REQUEST_USB_QUEUED;
	if (WARN_ON_ONCE(!queue_work(ms912x->workqueue, &request->work))) {
		request->state = MS912X_REQUEST_FREE;
		ms912x->pending_rect_count = 0;
		ms912x->pending_valid = false;
		ms912x->pending_force_full = true;
		ms912x->pending_width = width;
		ms912x->pending_height = height;
		ms912x_pending_add_rect_locked(ms912x, &full_rect);
	} else {
		if (request->replay_included)
			MS912X_STAT_INC(ms912x, damage_replay_batches_queued);
		grown = ms912x_rect_vector_grow(ms912x->previous_damage_rects,
						&ms912x->previous_damage_rect_capacity,
						current_rect_count,
						MS912X_MAX_DAMAGE_RECTS);
		if (grown) {
			ms912x->previous_damage_rects = grown;
			memcpy(ms912x->previous_damage_rects,
			       request->current_rects,
			       current_rect_count * sizeof(*request->current_rects));
			ms912x->previous_damage_rect_count = current_rect_count;
		} else {
			ms912x->previous_damage_rects[0] =
				request->current_rects[0];
			for (i = 1; i < current_rect_count; i++) {
				ms912x->previous_damage_rects[0].x1 = min(
					ms912x->previous_damage_rects[0].x1,
					request->current_rects[i].x1);
				ms912x->previous_damage_rects[0].y1 = min(
					ms912x->previous_damage_rects[0].y1,
					request->current_rects[i].y1);
				ms912x->previous_damage_rects[0].x2 = max(
					ms912x->previous_damage_rects[0].x2,
					request->current_rects[i].x2);
				ms912x->previous_damage_rects[0].y2 = max(
					ms912x->previous_damage_rects[0].y2,
					request->current_rects[i].y2);
			}
			ms912x->previous_damage_rect_count = 1;
		}
		ms912x->previous_damage_width = width;
		ms912x->previous_damage_height = height;
		ms912x->previous_damage_generation = generation;
	}
	ms912x_transfer_update_pacing_locked(ms912x);
	wake_up_all(&ms912x->conversion_wait);
	ms912x_schedule_conversion_locked(ms912x);
	mutex_unlock(&ms912x->transfer_lock);
	drm_dev_exit(idx);
}

void ms912x_transfer_work_init(struct ms912x_device *ms912x)
{
	init_waitqueue_head(&ms912x->conversion_wait);
	init_waitqueue_head(&ms912x->frame_present_wait);
	INIT_DELAYED_WORK(&ms912x->conversion_work, ms912x_conversion_worker);
	INIT_WORK(&ms912x->scan_work, ms912x_scan_frame_worker);
}

int ms912x_framebuffer_submit(struct drm_framebuffer *fb, const struct iosys_map *map,
			const struct drm_rect *damage,
			const struct drm_mode_rect *clips,
			unsigned int clip_count,
			bool software_scan, bool force_full_scan,
			bool new_epoch, bool cursor_commit)
{
	int ret = 0, idx;
	struct ms912x_device *ms912x = to_ms912x(fb->dev);
	struct drm_device *drm = &ms912x->drm;
	struct drm_rect output_bounds;
	struct drm_rect input_bounds;
	struct drm_rect *current_rects = ms912x->submit_damage_rects;
	struct drm_rect clip_bounds;
	struct drm_rect *grown;
	unsigned int input_width, input_height;
	enum ms912x_scale_mode scale_mode;
	bool had_pending, full_update = false;
	bool cpu_access = false, scan_lock_held = false;
	bool wait_for_present = false;
	bool snapshot_enabled = ms912x->damage_scan_snapshot;
	unsigned int current_count = 0, valid_count = 0, i;
	u32 wait_generation = 0;
	u64 sequence;

	if (!drm_dev_enter(drm, &idx))
		return -ENODEV;
	output_bounds = DRM_RECT_INIT(0, 0, fb->width, fb->height);
	MS912X_STAT_INC(ms912x, updates);

	mutex_lock(&ms912x->transfer_lock);
	if (ms912x->transfer_stopping) {
		ret = -ENODEV;
		goto dev_exit;
	}
	ret = drm_gem_fb_begin_cpu_access(fb, DMA_FROM_DEVICE);
	if (ret < 0)
		goto invalidate_scan;
	cpu_access = true;
	input_width = ms912x->video_in_width ?
		ms912x->video_in_width : fb->width;
	input_height = ms912x->video_in_height ?
		ms912x->video_in_height : fb->height;
	scale_mode = ms912x->scale_mode;
	input_bounds = DRM_RECT_INIT(0, 0, input_width, input_height);

	new_epoch |= ms912x->framebuffer_width &&
		(ms912x->framebuffer_width != fb->width ||
		 ms912x->framebuffer_height != fb->height);
	new_epoch |= ms912x->frame_snapshot_width &&
		(ms912x->frame_snapshot_width != input_width ||
		 ms912x->frame_snapshot_height != input_height);
	force_full_scan |= new_epoch ||
		atomic_read(&ms912x->damage_scan_invalid);
	sequence = ++ms912x->frame_sequence;
	if (cursor_commit)
		ms912x->cursor_commit_sequence = sequence;
	if (!software_scan && ms912x_scan_is_pending(ms912x))
		software_scan = true;
	if (new_epoch) {
		ms912x->frame_generation++;
		ms912x->failed_rect_valid = false;
		ms912x->previous_damage_rect_count = 0;
		ms912x->pending_rect_count = 0;
		ms912x->pending_valid = false;
		ms912x->pending_force_full = false;
		ms912x->pending_deadline = 0;
		atomic_set(&ms912x->damage_scan_invalid, 1);
	}
	wait_generation = ms912x->frame_generation;
	ms912x->framebuffer_width = fb->width;
	ms912x->framebuffer_height = fb->height;
	ms912x->frame_snapshot_width = input_width;
	ms912x->frame_snapshot_height = input_height;

	if (software_scan) {
		ret = ms912x_scan_enqueue_frame_locked(ms912x, fb, map,
						       force_full_scan, sequence);
		if (ret < 0)
			goto invalidate_scan;
		wait_for_present = !cursor_commit &&
			!ms912x->cursor_commit_pending;
		goto end_cpu_access;
	}
	if (snapshot_enabled) {
		mutex_lock(&ms912x->damage_scan_lock);
		scan_lock_held = true;
	}
	if (force_full_scan) {
		current_rects[0] = output_bounds;
		current_count = 1;
	} else if (damage && clips && clip_count &&
		   clip_count <= MS912X_MAX_DAMAGE_RECTS) {
		grown = ms912x_rect_vector_grow(current_rects,
						&ms912x->submit_damage_rect_capacity,
						clip_count,
						MS912X_MAX_DAMAGE_RECTS);
		if (grown) {
			current_rects = grown;
			ms912x->submit_damage_rects = grown;
			clip_bounds.x1 = clips[0].x1;
			clip_bounds.y1 = clips[0].y1;
			clip_bounds.x2 = clips[0].x2;
			clip_bounds.y2 = clips[0].y2;
			for (i = 0; i < clip_count; i++) {
				current_rects[i].x1 = clips[i].x1;
				current_rects[i].y1 = clips[i].y1;
				current_rects[i].x2 = clips[i].x2;
				current_rects[i].y2 = clips[i].y2;
				if (i) {
					clip_bounds.x1 = min(clip_bounds.x1,
							     current_rects[i].x1);
					clip_bounds.y1 = min(clip_bounds.y1,
							     current_rects[i].y1);
					clip_bounds.x2 = max(clip_bounds.x2,
							     current_rects[i].x2);
					clip_bounds.y2 = max(clip_bounds.y2,
							     current_rects[i].y2);
				}
			}
			if (drm_rect_equals(&clip_bounds, damage))
				current_count = clip_count;
			else {
				current_rects[0] = *damage;
				current_count = 1;
			}
		} else {
			current_rects[0] = *damage;
			current_count = 1;
		}
	} else if (damage) {
		current_rects[0] = *damage;
		current_count = 1;
	}
	if (!current_count) {
		ret = -ENODATA;
		goto no_damage;
	}
	for (i = 0; i < current_count; i++) {
		struct drm_rect rect = current_rects[i];

		if (!ms912x_rect_map_output_to_input(
			    &rect, fb->width, fb->height,
			    input_width, input_height, scale_mode))
			continue;
		if (rect.x1 >= rect.x2 || rect.y1 >= rect.y2)
			continue;
		ms912x_rect_align_x4(&rect, input_width);
		current_rects[valid_count++] = rect;
		full_update |= rect.x1 == 0 && rect.y1 == 0 &&
			rect.x2 == input_width && rect.y2 == input_height;
	}
	current_count = valid_count;
	if (!current_count) {
		ret = -ENODATA;
		goto no_damage;
	}
	full_update |= current_count == 1 &&
		drm_rect_equals(&current_rects[0], &input_bounds);
	if (ms912x_rects_payload_bytes(current_rects, current_count) <
	    MS912X_SMALL_DAMAGE_THRESHOLD_BYTES) {
		WRITE_ONCE(ms912x->small_damage_sequence, sequence);
		wake_up_all(&ms912x->frame_present_wait);
	}

	for (i = 0; i < current_count; i++)
		ms912x_snapshot_capture_damage(ms912x, fb, map,
					       &current_rects[i]);
	if (snapshot_enabled) {
		for (i = 0; i < current_count; i++)
			ms912x_damage_scan_update_snapshot(ms912x,
						   &current_rects[i]);
	}
	ms912x->published_frame_sequence = sequence;
	ms912x_payload_invalidate_damage(ms912x, current_rects, current_count);

	if (full_update) {
		ms912x->pending_rect_count = 0;
		ms912x->pending_valid = false;
		ms912x->pending_force_full = false;
		ms912x->pending_deadline = 0;
	}
	had_pending = ms912x->pending_rect_count != 0;
	if (had_pending) {
		MS912X_STAT_INC(ms912x, damage_batched_updates);
	} else {
		ms912x->pending_width = input_width;
		ms912x->pending_height = input_height;
		ms912x->pending_deadline = 0;
	}
	ms912x->pending_force_full |= force_full_scan || full_update;
	for (i = 0; i < current_count; i++)
		ms912x_pending_add_rect_locked(ms912x, &current_rects[i]);
	ms912x_pending_compact_locked(ms912x);
	ms912x_pending_update_deadline_locked(ms912x, had_pending);
	ms912x_schedule_conversion_locked(ms912x);
	wait_for_present = !cursor_commit &&
		!ms912x->cursor_commit_pending;
	goto end_cpu_access;

no_damage:
	if (ret == -ENODATA)
		goto end_cpu_access;
invalidate_scan:
	if (snapshot_enabled) {
		if (!scan_lock_held) {
			mutex_lock(&ms912x->damage_scan_lock);
			scan_lock_held = true;
		}
		ms912x->damage_scan_valid = false;
		atomic_set(&ms912x->damage_scan_invalid, 1);
	}
end_cpu_access:
	if (cpu_access)
		drm_gem_fb_end_cpu_access(fb, DMA_FROM_DEVICE);
	if (scan_lock_held)
		mutex_unlock(&ms912x->damage_scan_lock);
dev_exit:
	if (cursor_commit && ret < 0) {
		ms912x->cursor_commit_pending = false;
		ms912x->cursor_commit_sequence = 0;
		ms912x_schedule_conversion_locked(ms912x);
	}
	mutex_unlock(&ms912x->transfer_lock);
	if (wait_for_present && !ret)
		ret = ms912x_wait_for_pacing_release(ms912x, sequence,
						     wait_generation);
	drm_dev_exit(idx);
	return ret;
}
