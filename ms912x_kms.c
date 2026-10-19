// SPDX-License-Identifier: GPL-2.0-only

#include <linux/align.h>
#include <linux/ktime.h>
#include <linux/limits.h>
#include <linux/math64.h>
#include <linux/minmax.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_blend.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_damage_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_modeset_helper.h>
#include <drm/drm_plane.h>
#include <drm/drm_print.h>

#include "ms912x.h"

static const struct drm_mode_config_funcs ms912x_mode_config_funcs = {
	.fb_create = drm_gem_fb_create_with_dirty,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
};

static const struct drm_mode_config_helper_funcs
ms912x_mode_config_helper_funcs = {
	.atomic_commit_tail = drm_atomic_helper_commit_tail_rpm,
};

static const struct ms912x_mode ms912x_mode_list[] = {
	/* Fixed timings and the mode identifiers expected by the adapter. */
	MS912X_MODE(720, 480, 60, 0x02), /* 60.00 Hz */
	MS912X_MODE(720, 576, 50, 0x11), /* 50.00 Hz */
	MS912X_MODE(1280,  720, 50, 0x13), /* 50.00 Hz */
	MS912X_MODE(1920, 1080, 50, 0x1f), /* 50.00 Hz */
	MS912X_MODE(1920, 1080, 30, 0x22), /* 30.00 Hz */
	MS912X_MODE(640, 480, 60, 0x40), /* 59.94 Hz */
	MS912X_MODE(800, 600, 60, 0x42), /* 60.32 Hz */
	MS912X_MODE(800, 600, 75, 0x44), /* 75.00 Hz */
	MS912X_MODE(1024, 600, 60, 0x96),
	MS912X_MODE(1024,  768, 60, 0x47), /* 60.00 Hz */
	MS912X_MODE(1024,  768, 75, 0x49), /* 75.03 Hz */
	MS912X_MODE(1152,  864, 60, 0x4c), /* 60.00 Hz */
	MS912X_MODE(1280,  600, 60, 0x4e), /* 60.00 Hz */
	MS912X_MODE(1280,  720, 60, 0x4f), /* 60.00 Hz */
	MS912X_MODE(1280,  768, 60, 0x54), /* 59.87 Hz */
	MS912X_MODE(1280,  768, 75, 0x56), /* 74.89 Hz */
	MS912X_MODE(1280,  800, 60, 0x57), /* 59.81 Hz */
	MS912X_MODE(1280,  960, 60, 0x5b), /* 60.00 Hz */
	MS912X_MODE(1280, 1024, 60, 0x60), /* 60.02 Hz */
	MS912X_MODE(1280, 1024, 75, 0x61), /* 75.02 Hz */
	MS912X_MODE(1360,  768, 60, 0x64), /* 60.02 Hz */
	MS912X_MODE(1366,  768, 60, 0x66), /* 59.79 Hz */
	MS912X_MODE(1400, 1050, 60, 0x67), /* 59.98 Hz */
	MS912X_MODE(1440,  900, 60, 0x6b), /* 59.89 Hz */
	MS912X_MODE(1600, 1200, 60, 0x73), /* 60.00 Hz */
	MS912X_MODE(1680, 1050, 60, 0x78), /* 59.95 Hz */
	MS912X_MODE(1920, 1080, 60, 0x81), /* 60.00 Hz */
};

static const struct ms912x_mode *
ms912x_mode_lookup(struct ms912x_device *ms912x,
		const struct drm_display_mode *mode)
{
	unsigned int i;
	int width = mode->hdisplay;
	int height = mode->vdisplay;
	int hz = drm_mode_vrefresh(mode);

	for (i = 0; i < ms912x->num_custom_modes; i++) {
		const struct ms912x_mode *custom_mode =
			&ms912x->custom_modes[i].mode;

		if (custom_mode->width == width &&
		    custom_mode->height == height &&
		    custom_mode->hz == hz)
			return custom_mode;
	}

	for (i = 0; i < ARRAY_SIZE(ms912x_mode_list); i++) {
		if (ms912x_mode_list[i].width == width &&
		    ms912x_mode_list[i].height == height &&
		    ms912x_mode_list[i].hz == hz) {
			return &ms912x_mode_list[i];
		}
	}
	return NULL;
}

static void ms912x_crtc_enable(struct drm_crtc *crtc,
				      struct drm_atomic_commit *state)
{
	struct drm_crtc_state *crtc_state =
		drm_atomic_get_new_crtc_state(state, crtc);
	struct drm_device *dev = crtc->dev;
	struct ms912x_device *ms912x = to_ms912x(dev);
	const struct ms912x_mode *ms_mode;
	int ret;

	ms912x_transfer_start(ms912x);
	ret = ms912x_display_enable(ms912x);
	if (ret) {
		drm_err(dev, "failed to power on display: %d\n", ret);
		return;
	}

	ms_mode = ms912x_mode_lookup(ms912x, &crtc_state->mode);
	if (!ms_mode) {
		drm_err(dev, "unsupported mode passed to CRTC enable\n");
		return;
	}

	ret = ms912x_display_set_mode(ms912x, ms_mode);
	if (ret)
		drm_err(dev, "failed to set display mode: %d\n", ret);
}

static void ms912x_crtc_disable(struct drm_crtc *crtc,
				       struct drm_atomic_commit *state)
{
	struct drm_device *dev = crtc->dev;
	struct ms912x_device *ms912x = to_ms912x(dev);
	int ret;

	ms912x_transfer_stop(ms912x);
	ret = ms912x_display_disable(ms912x);
	if (ret && ret != -ENODEV)
		drm_err(dev, "failed to power off display: %d\n", ret);
}

static enum drm_mode_status
ms912x_crtc_validate_mode(struct drm_crtc *crtc,
		       const struct drm_display_mode *mode)
{
	struct ms912x_device *ms912x = to_ms912x(crtc->dev);
	const struct ms912x_mode *ms_mode = ms912x_mode_lookup(ms912x, mode);

	if (!ms_mode)
		return MODE_BAD;

	return MODE_OK;
}

static int ms912x_plane_check_atomic_state(struct drm_plane *plane,
				     struct drm_atomic_commit *state)
{
	struct drm_plane_state *old_plane_state;
	struct drm_plane_state *new_plane_state;
	struct drm_crtc_state *crtc_state = NULL;
	bool framebuffer_changed, has_damage_clips;

	old_plane_state = drm_atomic_get_old_plane_state(state, plane);
	new_plane_state = drm_atomic_get_new_plane_state(state, plane);
	if (new_plane_state->crtc)
		crtc_state = drm_atomic_get_new_crtc_state(state,
							   new_plane_state->crtc);

	framebuffer_changed = old_plane_state->fb != new_plane_state->fb;
	has_damage_clips = new_plane_state->fb &&
		drm_plane_get_damage_clips_count(new_plane_state);

	/*
	 * A mode set changes how the device interprets the framebuffer. On a
	 * framebuffer swap, use partial damage only when userspace supplied clips;
	 * otherwise the new buffer's contents are unknown and need a full update.
	 */
	new_plane_state->ignore_damage_clips =
		(crtc_state && drm_atomic_crtc_needs_modeset(crtc_state)) ||
		(framebuffer_changed && !has_damage_clips);

	return drm_atomic_helper_check_plane_state(new_plane_state, crtc_state,
						   DRM_PLANE_NO_SCALING,
						   DRM_PLANE_NO_SCALING,
						   false, false);
}

static bool ms912x_software_scan_enabled(struct ms912x_device *ms912x,
					 unsigned int damage_clip_count,
					 bool modeset)
{
	enum ms912x_damage_scan_mode mode;
	int generation;

	mode = READ_ONCE(ms912x_parameters.damage_scan);
	generation = atomic_read(&ms912x_parameters.damage_scan_generation);
	if (generation != READ_ONCE(ms912x->damage_scan_mode_generation)) {
		WRITE_ONCE(ms912x->damage_scan_mode_generation, generation);
		atomic_set(&ms912x->damage_scan_invalid, 1);
	}

	generation = atomic_read(&ms912x_parameters.damage_scan_config_generation);
	if (generation != READ_ONCE(ms912x->damage_scan_config_generation)) {
		WRITE_ONCE(ms912x->damage_scan_config_generation, generation);
		atomic_set(&ms912x->damage_scan_invalid, 1);
	}

	return mode == MS912X_DAMAGE_SCAN_ON ||
		(mode == MS912X_DAMAGE_SCAN_AUTO &&
		 (!damage_clip_count || modeset ||
		  atomic_read(&ms912x->damage_scan_invalid)));
}

static void ms912x_record_plane_update_stats(
		struct ms912x_device *ms912x,
		const struct drm_plane_state *new_plane_state,
		unsigned int damage_clip_count, bool modeset,
		bool framebuffer_changed, const struct drm_rect *damage,
		bool helper_damage)
{
	u64 damage_pixels;

	if (!ms912x_stats_enabled())
		return;

	if (modeset)
		atomic64_inc(&ms912x->stats.modeset_updates);
	if (framebuffer_changed) {
		atomic64_inc(&ms912x->stats.framebuffer_swaps);
		if (damage_clip_count)
			atomic64_inc(&ms912x->stats.fb_swaps_with_clips);
		else
			atomic64_inc(&ms912x->stats.fb_swaps_without_clips);
	}
	if (damage_clip_count)
		atomic64_add(damage_clip_count, &ms912x->stats.damage_clip_rects);

	if (!helper_damage) {
		atomic64_inc(&ms912x->stats.no_damage_updates);
		return;
	}

	damage_pixels = (u64)drm_rect_width(damage) * drm_rect_height(damage);
	atomic64_inc(&ms912x->stats.damage_updates);
	atomic64_add(damage_pixels, &ms912x->stats.damage_pixels);
	if (damage->x1 == 0 && damage->y1 == 0 &&
	    damage->x2 == new_plane_state->fb->width &&
	    damage->y2 == new_plane_state->fb->height)
		atomic64_inc(&ms912x->stats.damage_full_updates);
	if (new_plane_state->ignore_damage_clips)
		atomic64_inc(&ms912x->stats.forced_full_updates);
}

static void ms912x_plane_commit_update(struct drm_plane *plane,
				       struct drm_atomic_commit *state)
{
	struct drm_plane_state *old_plane_state;
	struct drm_plane_state *new_plane_state;
	struct drm_plane_state *new_cursor_state;
	struct drm_crtc_state *new_crtc_state;
	struct drm_shadow_plane_state *shadow_plane_state;
	struct ms912x_device *ms912x = to_ms912x(plane->dev);
	struct drm_rect current_rect;
	struct drm_mode_rect *clips;
	unsigned int damage_clip_count, refresh_hz;
	bool modeset, framebuffer_changed, helper_damage, software_scan;
	bool cursor_commit = false;
	int ret;

	old_plane_state = drm_atomic_get_old_plane_state(state, plane);
	new_plane_state = drm_atomic_get_new_plane_state(state, plane);

	if (!new_plane_state->fb)
		return;
	new_crtc_state = drm_atomic_get_new_crtc_state(state,
						       new_plane_state->crtc);
	shadow_plane_state = to_drm_shadow_plane_state(new_plane_state);
	damage_clip_count =
		drm_plane_get_damage_clips_count(new_plane_state);
	modeset = drm_atomic_crtc_needs_modeset(new_crtc_state);
	framebuffer_changed = old_plane_state->fb != new_plane_state->fb;
	refresh_hz = drm_mode_vrefresh(&new_crtc_state->mode);
	WRITE_ONCE(ms912x->display_refresh_hz, refresh_hz);
	if (modeset)
		atomic_set(&ms912x->damage_scan_invalid, 1);

	helper_damage = drm_atomic_helper_damage_merged(old_plane_state,
							 new_plane_state,
							 &current_rect);
	ms912x_record_plane_update_stats(ms912x, new_plane_state,
					 damage_clip_count, modeset,
					 framebuffer_changed, &current_rect,
					 helper_damage);
	software_scan = ms912x_software_scan_enabled(ms912x,
						     damage_clip_count,
						     modeset);
	if (!software_scan && !helper_damage)
		return;
	if (ms912x->cursor_plane_enabled) {
		new_cursor_state = drm_atomic_get_new_plane_state(
			state, &ms912x->cursor_plane);
		cursor_commit = ms912x_cursor_commit_begin(ms912x,
							   new_cursor_state);
	}
	if (helper_damage) {
		clips = drm_plane_get_damage_clips(new_plane_state);
		if (new_plane_state->ignore_damage_clips || !clips ||
		    damage_clip_count > MS912X_MAX_DAMAGE_RECTS) {
			clips = NULL;
			damage_clip_count = 0;
		}
	}

	ret = ms912x_framebuffer_submit(new_plane_state->fb,
				  &shadow_plane_state->data[0],
				  helper_damage ? &current_rect : NULL,
				  helper_damage ? clips : NULL,
				  helper_damage ? damage_clip_count : 0,
				  software_scan,
				  modeset || atomic_read(&ms912x->damage_scan_invalid),
				  modeset, cursor_commit);
	if (ret == -ENODATA)
		return;
	if (ret < 0)
		return;
	if (modeset)
		ms912x_transfer_force_full_update(ms912x);
}

static const struct drm_crtc_helper_funcs ms912x_crtc_helper_funcs = {
	.mode_valid = ms912x_crtc_validate_mode,
	.atomic_check = drm_crtc_helper_atomic_check,
	.atomic_enable = ms912x_crtc_enable,
	.atomic_disable = ms912x_crtc_disable,
};

static const struct drm_crtc_funcs ms912x_crtc_funcs = {
	.reset = drm_atomic_helper_crtc_reset,
	.destroy = drm_crtc_cleanup,
	.set_config = drm_atomic_helper_set_config,
	.page_flip = drm_atomic_helper_page_flip,
	.atomic_duplicate_state = drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_crtc_destroy_state,
};

static const struct drm_plane_helper_funcs ms912x_plane_helper_funcs = {
	DRM_GEM_SHADOW_PLANE_HELPER_FUNCS,
	.atomic_check = ms912x_plane_check_atomic_state,
	.atomic_update = ms912x_plane_commit_update,
};

static const struct drm_plane_funcs ms912x_plane_funcs = {
	.update_plane = drm_atomic_helper_update_plane,
	.disable_plane = drm_atomic_helper_disable_plane,
	.destroy = drm_plane_cleanup,
	DRM_GEM_SHADOW_PLANE_FUNCS,
};

static const u32 ms912x_plane_formats[] = {
	DRM_FORMAT_XRGB8888,
};

static const u32 ms912x_cursor_plane_formats[] = {
	DRM_FORMAT_ARGB8888,
};

static int ms912x_cursor_plane_check_atomic_state(
	struct drm_plane *plane, struct drm_atomic_commit *state)
{
	struct drm_plane_state *new_plane_state =
		drm_atomic_get_new_plane_state(state, plane);
	struct drm_crtc_state *crtc_state;

	if (!new_plane_state->crtc)
		return 0;
	if (new_plane_state->fb &&
	    (new_plane_state->fb->format->format != DRM_FORMAT_ARGB8888 ||
	     new_plane_state->fb->modifier != DRM_FORMAT_MOD_LINEAR ||
	     new_plane_state->fb->width > MS912X_CURSOR_MAX_WIDTH ||
	     new_plane_state->fb->height > MS912X_CURSOR_MAX_HEIGHT))
		return -EINVAL;

	crtc_state = drm_atomic_get_new_crtc_state(state,
						   new_plane_state->crtc);
	return drm_atomic_helper_check_plane_state(new_plane_state, crtc_state,
						   DRM_PLANE_NO_SCALING,
						   DRM_PLANE_NO_SCALING,
						   true, false);
}

static void ms912x_cursor_plane_update(struct drm_plane *plane,
				       struct drm_atomic_commit *state)
{
	struct ms912x_device *ms912x = to_ms912x(plane->dev);
	struct drm_plane_state *new_state =
		drm_atomic_get_new_plane_state(state, plane);

	MS912X_STAT_INC(ms912x, cursor_plane_updates);
	ms912x_cursor_plane_submit(ms912x, new_state);
}

static void ms912x_cursor_plane_disable(struct drm_plane *plane,
					struct drm_atomic_commit *state)
{
	struct ms912x_device *ms912x = to_ms912x(plane->dev);
	struct drm_plane_state *new_state =
		drm_atomic_get_new_plane_state(state, plane);

	MS912X_STAT_INC(ms912x, cursor_plane_updates);
	ms912x_cursor_plane_submit(ms912x, new_state);
}

static const struct drm_plane_helper_funcs ms912x_cursor_plane_helper_funcs = {
	.atomic_check = ms912x_cursor_plane_check_atomic_state,
	.atomic_update = ms912x_cursor_plane_update,
	.atomic_disable = ms912x_cursor_plane_disable,
};

static const struct drm_plane_funcs ms912x_cursor_plane_funcs = {
	.update_plane = drm_atomic_helper_update_plane,
	.disable_plane = drm_atomic_helper_disable_plane,
	.destroy = drm_plane_cleanup,
	.reset = drm_atomic_helper_plane_reset,
	.atomic_duplicate_state = drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_plane_destroy_state,
};

void ms912x_kms_mode_config_init(struct drm_device *drm)
{
	drm->mode_config.min_width = 0;
	drm->mode_config.max_width = MS912X_MAX_WIDTH;
	drm->mode_config.min_height = 0;
	drm->mode_config.max_height = MS912X_MAX_HEIGHT;
	if (to_ms912x(drm)->cursor_plane_enabled) {
		drm->mode_config.cursor_width = MS912X_CURSOR_MAX_WIDTH;
		drm->mode_config.cursor_height = MS912X_CURSOR_MAX_HEIGHT;
	}
	drm->mode_config.funcs = &ms912x_mode_config_funcs;
	drm->mode_config.helper_private = &ms912x_mode_config_helper_funcs;
}

const struct ms912x_mode *ms912x_default_mode(void)
{
	return &ms912x_mode_list[0];
}

int ms912x_kms_init(struct ms912x_device *ms912x)
{
	struct drm_device *drm = &ms912x->drm;
	struct drm_plane *cursor_plane = NULL;
	int ret;

	ret = drm_universal_plane_init(drm, &ms912x->plane, 0,
				       &ms912x_plane_funcs,
				       ms912x_plane_formats,
				       ARRAY_SIZE(ms912x_plane_formats), NULL,
				       DRM_PLANE_TYPE_PRIMARY, NULL);
	if (ret)
		return ret;

	drm_plane_helper_add(&ms912x->plane, &ms912x_plane_helper_funcs);
	drm_plane_enable_fb_damage_clips(&ms912x->plane);

	if (ms912x->cursor_plane_enabled) {
		ret = drm_universal_plane_init(drm, &ms912x->cursor_plane, 0,
					       &ms912x_cursor_plane_funcs,
					       ms912x_cursor_plane_formats,
					       ARRAY_SIZE(ms912x_cursor_plane_formats),
					       NULL, DRM_PLANE_TYPE_CURSOR, NULL);
		if (ret)
			return ret;
		ret = drm_plane_create_blend_mode_property(
			&ms912x->cursor_plane,
			BIT(DRM_MODE_BLEND_PREMULTI) |
			BIT(DRM_MODE_BLEND_COVERAGE));
		if (ret)
			return ret;

		drm_plane_helper_add(&ms912x->cursor_plane,
				     &ms912x_cursor_plane_helper_funcs);
		drm_plane_enable_fb_damage_clips(&ms912x->cursor_plane);
		cursor_plane = &ms912x->cursor_plane;
	}

	ret = drm_crtc_init_with_planes(drm, &ms912x->crtc, &ms912x->plane,
					cursor_plane,
					&ms912x_crtc_funcs, NULL);
	if (ret)
		return ret;

	drm_crtc_helper_add(&ms912x->crtc, &ms912x_crtc_helper_funcs);
	return 0;
}
