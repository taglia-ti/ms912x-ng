/* SPDX-License-Identifier: GPL-2.0-only */
#include <linux/kernel.h>

#include "ms912x_downscale.h"

void ms912x_downscale_xrgb8888(enum ms912x_scale_mode mode,
		const u32 *src, u32 *dst, unsigned int sw, unsigned int sh,
		unsigned int dw, unsigned int dh, unsigned int ss, unsigned int ds)
{
	if (mode == MS912X_SCALE_MODE_DOWNSCALE_2_TO_1)
		ms912x_downscale_xrgb8888_2x(src, dst, sw, sh, dw, dh, ss, ds);
	else if (mode == MS912X_SCALE_MODE_DOWNSCALE_3_TO_2)
		ms912x_downscale_xrgb8888_3to2(src, dst, sw, sh, dw, dh, ss, ds);
}

void ms912x_downscale_xrgb8888_region(enum ms912x_scale_mode mode,
		const u32 *src, u32 *dst, unsigned int sw, unsigned int sh,
		unsigned int dw, unsigned int dh, unsigned int ss, unsigned int ds,
		unsigned int ox, unsigned int oy)
{
	if (!sw || !sh || !dw || !dh || ox >= sw || oy >= sh)
		return;
	if (mode == MS912X_SCALE_MODE_DOWNSCALE_2_TO_1) {
		ms912x_downscale_xrgb8888_2x(src + oy * ss + ox, dst,
			sw - ox, sh - oy, dw, dh, ss, ds);
	} else if (mode == MS912X_SCALE_MODE_DOWNSCALE_3_TO_2) {
		ms912x_downscale_xrgb8888_3to2_region(src, dst, sw, sh, dw, dh,
			ss, ds, ox, oy);
	}
}
