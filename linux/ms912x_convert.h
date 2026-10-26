/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MS912X_CONVERT_H
#define MS912X_CONVERT_H

#include <linux/types.h>
#include <drm/drm_rect.h>
#include "ms912x_limits.h"

/* DRM XRGB8888/XBGR8888 input and UYVY output use MS912X_MAX_WIDTH pitches.
 * The rectangle addresses both images; only its destination pixels are written.
 */
int ms912x_convert_rgb_to_uyvy(u32 *dst, const __le32 *src,
			     const struct drm_rect *rect, u32 format);

/* Internal SIMD batches: the source uses MS912X_MAX_WIDTH pixels per row;
 * dst points at the rectangle origin, with an explicit byte pitch.
 * rgbx selects RGBX bytes (true) or BGRX bytes (false), ignoring the X byte.
 * Callers validate geometry and CPU support before selecting a backend.
 * Each backend is linked only on its corresponding architecture.
 */
bool ms912x_convert_rgb_to_uyvy_sse2_strided(void *dst, const __le32 *src,
		const struct drm_rect *rect, unsigned int dst_stride, bool rgbx);
bool ms912x_convert_rgb_to_uyvy_ssse3_strided(void *dst, const __le32 *src,
		const struct drm_rect *rect, unsigned int dst_stride, bool rgbx);
void ms912x_convert_rgb_to_uyvy_avx2_strided(void *dst, const __le32 *src,
		const struct drm_rect *rect, unsigned int dst_stride, bool rgbx);
bool ms912x_convert_rgb_to_uyvy_neon_strided(void *dst, const __le32 *src,
		const struct drm_rect *rect, unsigned int dst_stride, bool rgbx);

#endif
