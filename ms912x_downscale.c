/* SPDX-License-Identifier: GPL-2.0-only */
#include <linux/kernel.h>

#include "ms912x_downscale.h"

void ms912x_downscale_xrgb8888(unsigned int scale,
		const u32 *src, u32 *dst, unsigned int sw, unsigned int sh,
		unsigned int dw, unsigned int dh, unsigned int ss, unsigned int ds)
{
	if (scale == MS912X_SCALE_50)
		ms912x_downscale_xrgb8888_2x(src, dst, sw, sh, dw, dh, ss, ds);
	else if (scale == MS912X_SCALE_75)
		ms912x_downscale_xrgb8888_3to2(src, dst, sw, sh, dw, dh, ss, ds);
}

void ms912x_downscale_xrgb8888_region(unsigned int scale,
		const u32 *src, u32 *dst, unsigned int sw, unsigned int sh,
		unsigned int dw, unsigned int dh, unsigned int ss, unsigned int ds,
		unsigned int ox, unsigned int oy)
{
	if (!sw || !sh || !dw || !dh || ox >= sw || oy >= sh)
		return;
	if (scale == MS912X_SCALE_50) {
		ms912x_downscale_xrgb8888_2x(src + oy * ss + ox, dst,
			sw - ox, sh - oy, dw, dh, ss, ds);
	} else if (scale == MS912X_SCALE_75) {
		ms912x_downscale_xrgb8888_3to2_region(src, dst, sw, sh, dw, dh,
			ss, ds, ox, oy);
	}
}
