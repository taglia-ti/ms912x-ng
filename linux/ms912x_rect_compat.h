/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MS912X_RECT_COMPAT_H
#define MS912X_RECT_COMPAT_H

#include <drm/drm_rect.h>

#ifndef DRM_RECT_INIT
static inline struct drm_rect
ms912x_drm_rect_init_legacy(int x, int y, int width, int height)
{
	struct drm_rect rect;

	rect.x1 = x;
	rect.y1 = y;
	rect.x2 = x + width;
	rect.y2 = y + height;
	return rect;
}
#define DRM_RECT_INIT(x, y, width, height) \
	ms912x_drm_rect_init_legacy(x, y, width, height)
#endif

#endif /* MS912X_RECT_COMPAT_H */
