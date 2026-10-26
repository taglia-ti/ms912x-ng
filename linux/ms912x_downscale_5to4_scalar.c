// SPDX-License-Identifier: GPL-2.0-only
#include "ms912x_downscale_5to4.h"

#ifdef MS912X_5TO4_AREA_FILTER
void ms912x_downscale_xrgb8888_5to4_scalar_region(const u32 *src, u32 *dst,
		unsigned int sw, unsigned int sh, unsigned int dw, unsigned int dh,
		unsigned int ss, unsigned int ds, unsigned int ox, unsigned int oy)
{
	unsigned int x, y;

	if (!sw || !sh || !dw || !dh || ox >= sw || oy >= sh)
		return;
	for (y = 0; y < dh; y++) {
		unsigned int sy = oy + y + y / 4;
		unsigned int edge_x = DIV_ROUND_UP((sw - ox - 1) * 4, 5);
		const u32 *row0 = src + min(sy, sh - 1) * ss;
		const u32 *row1 = src + min(sy + 1, sh - 1) * ss;
		u32 *out = dst + y * ds;
		u32 edge = ms912x_5to4_area_pixel(row0, row1, sw, edge_x, ox, y & 3);

		/* Cache replicated padding before an in-place store reaches its source. */
		for (x = 0; x < min(dw, edge_x); x++)
			out[x] = ms912x_5to4_area_pixel(row0, row1, sw, x, ox, y & 3);
		for (; x < dw; x++)
			out[x] = edge;
	}
}

#else
#define MS912X_5TO4_SCOPE static
#define MS912X_5TO4_WORKER ms912x_5to4_scalar_rows
#define MS912X_5TO4_PIXELS 1
#define MS912X_5TO4_TYPE u32
#define MS912X_5TO4_LOAD(p) (*(p))
#define MS912X_5TO4_STORE(p, v) (*(p) = (v))
#define MS912X_5TO4_H(row, x, ox) ms912x_5to4_cubic_horizontal(row, ~0U, x, ox)
#define MS912X_5TO4_A(a, b, c, d) ms912x_5to4_cubic_pixel(a, b, c, d, 0)
#define MS912X_5TO4_B(a, b, c, d) ms912x_5to4_cubic_pixel(a, b, c, d, 1)
#define MS912X_5TO4_FINISH() do { } while (0)
#include "ms912x_downscale_5to4_template.h"

#define MS912X_5TO4_REGION ms912x_downscale_xrgb8888_5to4_scalar_region
#define MS912X_5TO4_BEGIN() do { } while (0)
#define MS912X_5TO4_END() do { } while (0)
#include "ms912x_downscale_5to4_entry.h"
#endif
