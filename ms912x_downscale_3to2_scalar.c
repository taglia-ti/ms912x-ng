// SPDX-License-Identifier: GPL-2.0-only
#include "ms912x_downscale_3to2.h"

#ifdef MS912X_DOWNSCALE_AREA_FILTER
void ms912x_downscale_xrgb8888_3to2_scalar_region(const u32 *src, u32 *dst,
		unsigned int sw, unsigned int sh, unsigned int dw, unsigned int dh,
		unsigned int ss, unsigned int ds, unsigned int ox, unsigned int oy)
{
	unsigned int y, x;

	if (!sw || !sh || !dw || !dh || ox >= sw || oy >= sh)
		return;
	for (y = 0; y < dh; y++) {
		unsigned int sy = oy + y + y / 2;
		unsigned int phase = y & 1;
		const u32 *heavy = src + min(sy + phase, sh - 1) * ss;
		const u32 *light = src + min(sy + 1 - phase, sh - 1) * ss;
		u32 *out = dst + y * ds;

		for (x = 0; x < dw; x++)
			out[x] = ms912x_downscale_area_pixel(heavy, light, sw, x, ox);
	}
}

void ms912x_downscale_xrgb8888_3to2_scalar(const u32 *src, u32 *dst,
		unsigned int sw, unsigned int sh, unsigned int dw, unsigned int dh,
		unsigned int ss, unsigned int ds)
{
	ms912x_downscale_xrgb8888_3to2_scalar_region(src, dst, sw, sh, dw, dh, ss, ds, 0, 0);
}
#else
#define MS912X_CUBIC_SCOPE static
#define MS912X_CUBIC_WORKER ms912x_downscale_scalar_rows
#define MS912X_CUBIC_PIXELS 1
#define MS912X_CUBIC_TYPE u32
#define MS912X_CUBIC_LOAD(p) (*(p))
#define MS912X_CUBIC_STORE(p, v) (*(p) = (v))
#define MS912X_CUBIC_H(row, x, ox) \
	ms912x_downscale_cubic_horizontal(row, ~0U, x, ox)
#define MS912X_CUBIC_FILTER ms912x_downscale_cubic_pixel
#define MS912X_CUBIC_FINISH() do { } while (0)
#include "ms912x_downscale_3to2_template.h"

#define MS912X_CUBIC_REGION ms912x_downscale_xrgb8888_3to2_scalar_region
#define MS912X_CUBIC_ENTRY ms912x_downscale_xrgb8888_3to2_scalar
#define MS912X_CUBIC_BEGIN() do { } while (0)
#define MS912X_CUBIC_END() do { } while (0)
#include "ms912x_downscale_3to2_entry.h"
#endif
