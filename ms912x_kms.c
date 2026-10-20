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
	MS912X_MODE_TIMED(720, 480, 60, 0x02, 27000, 858, 525, 122, 36, 62, 6, 1),
	MS912X_MODE_TIMED(720, 576, 50, 0x11, 27000, 864, 625, 132, 44, 64, 5, 1),
	MS912X_MODE_TIMED(1280, 720, 50, 0x13, 74250, 1980, 750, 260, 25, 40, 5, 7),
	MS912X_MODE_TIMED(1920, 1080, 50, 0x1f, 148500, 2640, 1125, 192, 41, 44, 5, 7),
	MS912X_MODE_TIMED(1920, 1080, 30, 0x22, 74170, 2200, 1125, 192, 41, 44, 5, 7),
	MS912X_MODE_TIMED(640, 480, 60, 0x40, 25170, 800, 525, 144, 35, 96, 2, 1),
	MS912X_MODE_TIMED(640, 480, 75, 0x41, 31500, 840, 500, 184, 19, 64, 3, 1),
	MS912X_MODE_TIMED(800, 600, 60, 0x42, 40000, 1056, 628, 216, 27, 128, 4, 7),
	MS912X_MODE_TIMED(800, 600, 75, 0x44, 49500, 1056, 625, 240, 24, 80, 3, 7),
	MS912X_MODE_TIMED(1024, 600, 60, 0x96, 49000, 1312, 624, 248, 21, 104, 10, 5),
	MS912X_MODE_TIMED(1024, 768, 60, 0x47, 65000, 1344, 806, 296, 35, 136, 6, 1),
	MS912X_MODE_TIMED(1024, 768, 75, 0x49, 78750, 1312, 800, 272, 31, 96, 3, 7),
	MS912X_MODE_TIMED(1152, 864, 60, 0x4c, 81620, 1520, 895, 304, 30, 120, 3, 5),
	MS912X_MODE_TIMED(1280, 600, 60, 0x4e, 61500, 1648, 622, 312, 21, 128, 3, 5),
	MS912X_MODE_TIMED(1280, 720, 60, 0x4f, 74250, 1650, 750, 260, 25, 40, 5, 7),
	MS912X_MODE_TIMED(1280, 768, 60, 0x54, 79500, 1664, 798, 320, 27, 128, 7, 5),
	MS912X_MODE_TIMED(1280, 768, 75, 0x56, 102250, 1696, 805, 336, 34, 128, 7, 5),
	MS912X_MODE_TIMED(1280, 800, 60, 0x57, 83500, 1680, 831, 328, 28, 128, 6, 5),
	MS912X_MODE_TIMED(1280, 960, 60, 0x5b, 108000, 1800, 1000, 424, 39, 112, 3, 7),
	MS912X_MODE_TIMED(1280, 1024, 60, 0x60, 108000, 1688, 1066, 360, 41, 112, 3, 7),
	MS912X_MODE_TIMED(1280, 1024, 75, 0x61, 135000, 1688, 1066, 392, 41, 144, 3, 7),
	MS912X_MODE_TIMED(1360, 768, 60, 0x64, 85500, 1792, 795, 368, 24, 112, 6, 7),
	MS912X_MODE_TIMED(1366, 768, 60, 0x66, 85500, 1792, 798, 356, 27, 143, 3, 7),
	MS912X_MODE_TIMED(1400, 1050, 60, 0x67, 121750, 1864, 1089, 376, 36, 144, 4, 5),
	MS912X_MODE_TIMED(1440, 900, 60, 0x6b, 106500, 1904, 934, 384, 31, 152, 6, 5),
	MS912X_MODE_TIMED(1600, 1200, 60, 0x73, 162000, 2160, 1250, 496, 49, 192, 3, 7),
	MS912X_MODE_TIMED(1680, 1050, 60, 0x78, 146250, 2240, 1089, 456, 36, 176, 6, 5),
	MS912X_MODE_TIMED(1920, 1080, 60, 0x81, 148500, 2200, 1125, 192, 41, 44, 5, 7),
	MS912X_MODE_TIMED(1920, 1200, 60, 0x85, 154000, 2080, 1235, 112, 32, 32, 6, 3),
};

static bool ms912x_modes_have_same_timing(
				const struct drm_display_mode *first,
				const struct drm_display_mode *second)
{
	const u32 timing_flags = DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NHSYNC |
		DRM_MODE_FLAG_PVSYNC | DRM_MODE_FLAG_NVSYNC |
		DRM_MODE_FLAG_INTERLACE;

	return first->clock == second->clock &&
		first->hdisplay == second->hdisplay &&
		first->hsync_start == second->hsync_start &&
		first->hsync_end == second->hsync_end &&
		first->htotal == second->htotal &&
		first->vdisplay == second->vdisplay &&
		first->vsync_start == second->vsync_start &&
		first->vsync_end == second->vsync_end &&
		first->vtotal == second->vtotal &&
		!((first->flags ^ second->flags) & timing_flags);
}

static const struct ms912x_mode *
ms912x_mode_lookup(struct ms912x_device *ms912x,
		const struct drm_display_mode *mode)
{
	const struct ms912x_mode *match = NULL;
	unsigned int i, matches = 0;
	unsigned int count = ms912x_supported_mode_count(ms912x);
	int width = mode->hdisplay;
	int height = mode->vdisplay;
	int hz = drm_mode_vrefresh(mode);

	for (i = 0; i < count; i++) {
		const struct ms912x_mode *supported_mode =
			ms912x_supported_mode_at(ms912x, i);

		if (supported_mode->width == width &&
		    supported_mode->height == height &&
		    supported_mode->hz == hz) {
			match = supported_mode;
			matches++;
		}
	}
	if (matches < 2)
		return match;

	match = NULL;
	for (i = 0; i < count; i++) {
		const struct ms912x_mode *supported_mode =
			ms912x_supported_mode_at(ms912x, i);
		struct drm_display_mode supported_timing;

		if (supported_mode->width != width ||
		    supported_mode->height != height ||
		    supported_mode->hz != hz ||
		    ms912x_mode_to_display_mode(ms912x, supported_mode,
						&supported_timing) ||
		    !ms912x_modes_have_same_timing(mode, &supported_timing))
			continue;
		if (!match)
			match = supported_mode;
	}

	return match;
}

unsigned int ms912x_supported_mode_count(struct ms912x_device *ms912x)
{
	return ms912x->num_custom_modes + ARRAY_SIZE(ms912x_mode_list);
}

const struct ms912x_mode *
ms912x_supported_mode_at(struct ms912x_device *ms912x, unsigned int index)
{
	if (index < ms912x->num_custom_modes)
		return &ms912x->custom_modes[index].mode;

	index -= ms912x->num_custom_modes;
	if (index < ARRAY_SIZE(ms912x_mode_list))
		return &ms912x_mode_list[index];

	return NULL;
}

int ms912x_mode_to_display_mode(struct ms912x_device *ms912x,
				const struct ms912x_mode *mode,
				struct drm_display_mode *display_mode)
{
	unsigned int i;

	for (i = 0; i < ms912x->num_custom_modes; i++) {
		if (mode == &ms912x->custom_modes[i].mode) {
			*display_mode = ms912x->custom_modes[i].display_mode;
			return 0;
		}
	}

	if (!mode->clock_khz || mode->htotal <= mode->width ||
	    mode->vtotal <= mode->height || mode->hoffset > mode->htotal ||
	    mode->voffset > mode->vtotal || !mode->hsync_width ||
	    !mode->vsync_width || mode->hsync_width > mode->hoffset ||
	    mode->vsync_width > mode->voffset ||
	    mode->htotal - mode->hoffset < mode->width ||
	    mode->vtotal - mode->voffset < mode->height)
		return -EINVAL;

	memset(display_mode, 0, sizeof(*display_mode));
	display_mode->clock = mode->clock_khz;
	display_mode->hdisplay = mode->width;
	display_mode->hsync_start = mode->htotal - mode->hoffset;
	display_mode->hsync_end = display_mode->hsync_start +
		mode->hsync_width;
	display_mode->htotal = mode->htotal;
	display_mode->vdisplay = mode->height;
	display_mode->vsync_start = mode->vtotal - mode->voffset;
	display_mode->vsync_end = display_mode->vsync_start +
		mode->vsync_width;
	display_mode->vtotal = mode->vtotal;
	display_mode->flags = mode->polarity & BIT(1) ?
		DRM_MODE_FLAG_PHSYNC : DRM_MODE_FLAG_NHSYNC;
	display_mode->flags |= mode->polarity & BIT(2) ?
		DRM_MODE_FLAG_PVSYNC : DRM_MODE_FLAG_NVSYNC;
	if (!(mode->polarity & BIT(0)))
		display_mode->flags |= DRM_MODE_FLAG_INTERLACE;
	display_mode->type = DRM_MODE_TYPE_DRIVER;
	drm_mode_set_name(display_mode);

	return 0;
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

	if (READ_ONCE(ms912x->sdram_type) == MS912X_SDRAM_UNKNOWN) {
		ret = ms912x_sdram_detect(ms912x);
		if (ret) {
			drm_err(dev, "cannot modeset: SDRAM detection failed: %d\n",
				ret);
			return;
		}
	}
	if (READ_ONCE(ms912x->sdram_type) == MS912X_SDRAM_NONE) {
		drm_err(dev, "modeset rejected: SDRAM-less adapters are unsupported\n");
		return;
	}

	ms_mode = ms912x_mode_lookup(ms912x, &crtc_state->mode);
	if (!ms_mode) {
		drm_err(dev, "unsupported mode passed to CRTC enable\n");
		return;
	}

	ms912x_transfer_start(ms912x);
	ret = ms912x_display_enable(ms912x);
	if (ret) {
		drm_err(dev, "failed to power on display: %d\n", ret);
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

	if (READ_ONCE(ms912x->sdram_type) == MS912X_SDRAM_NONE ||
	    READ_ONCE(ms912x->sdram_type) == MS912X_SDRAM_UNKNOWN)
		return MODE_BAD;

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

static void ms912x_plane_commit_update(struct drm_plane *plane,
				       struct drm_atomic_commit *state)
{
	struct drm_plane_state *old_plane_state;
	struct drm_plane_state *new_plane_state;
	struct drm_plane_state *new_cursor_state;
	struct drm_crtc_state *new_crtc_state;
	struct drm_shadow_plane_state *shadow_plane_state;
	struct ms912x_device *ms912x = to_ms912x(plane->dev);
	const struct ms912x_mode *ms_mode;
	struct drm_rect current_rect;
	struct drm_mode_rect *clips;
	unsigned int damage_clip_count, refresh_hz;
	bool modeset, helper_damage, software_scan;
	bool cursor_commit = false, scale_reconfigured = false;
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
	refresh_hz = drm_mode_vrefresh(&new_crtc_state->mode);
	WRITE_ONCE(ms912x->display_refresh_hz, refresh_hz);
	if (ms912x->scale_config_generation !=
	    atomic_read(&ms912x_parameters.scale_generation)) {
		ms_mode = ms912x_mode_lookup(ms912x, &new_crtc_state->mode);
		if (!ms_mode)
			return;
		ms912x_transfer_stop(ms912x);
		ret = ms912x_display_set_mode(ms912x, ms_mode);
		ms912x_transfer_start(ms912x);
		if (ret < 0) {
			drm_err(&ms912x->drm,
				"failed to apply scale mode update: %d\n", ret);
			return;
		}
		modeset = true;
		scale_reconfigured = true;
	}
	if (modeset)
		atomic_set(&ms912x->damage_scan_invalid, 1);
	helper_damage = drm_atomic_helper_damage_merged(old_plane_state,
							 new_plane_state,
							 &current_rect);
	software_scan = ms912x->scale_percent != MS912X_SCALE_100 ||
		ms912x_software_scan_enabled(ms912x, damage_clip_count, modeset);
	if (!software_scan && !helper_damage && !scale_reconfigured)
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
