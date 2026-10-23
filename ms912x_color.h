/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MS912X_COLOR_H
#define MS912X_COLOR_H

#include <asm/byteorder.h>

#include <linux/compiler.h>
#include <linux/types.h>

#include "shared/ms912x_color_core.h"

static __always_inline unsigned int
ms912x_rgb_to_y(unsigned int r, unsigned int g, unsigned int b)
{
	return ((16 << 16) + 16763 * r + 32904 * g + 6391 * b) >> 16;
}

static __always_inline unsigned int
ms912x_rgb_pair_to_u(unsigned int r, unsigned int g, unsigned int b)
{
	return ((128 << 17) - 9676 * r - 18996 * g + 28672 * b) >> 17;
}

static __always_inline unsigned int
ms912x_rgb_pair_to_v(unsigned int r, unsigned int g, unsigned int b)
{
	return ((128 << 17) + 28672 * r - 24009 * g - 4663 * b) >> 17;
}

static __always_inline u32 ms912x_xrgb_pair_to_uyvy(const __le32 *pixels)
{
	u32 first = le32_to_cpup(&pixels[0]);
	u32 second = le32_to_cpup(&pixels[1]);
	unsigned int r0 = (first >> 16) & 0xff;
	unsigned int g0 = (first >> 8) & 0xff;
	unsigned int b0 = first & 0xff;
	unsigned int r1 = (second >> 16) & 0xff;
	unsigned int g1 = (second >> 8) & 0xff;
	unsigned int b1 = second & 0xff;
	return ms912x_core_rgb_pair_to_uyvy(r0, g0, b0, r1, g1, b1);
}

#endif /* MS912X_COLOR_H */
