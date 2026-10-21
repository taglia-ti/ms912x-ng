/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MS912X_DOWNSCALE_5TO4_H
#define MS912X_DOWNSCALE_5TO4_H

#include <linux/kernel.h>
#include <linux/types.h>

#include "ms912x_neon.h"

#ifndef MS912X_DOWNSCALE_ROWS_PER_FPU
#define MS912X_DOWNSCALE_ROWS_PER_FPU 16
#endif

/* Match the existing 3:2 filter policy, including scalar fallback. */
#if defined(CONFIG_ARM64) || defined(CONFIG_ARM)
#define MS912X_5TO4_AREA_FILTER 1
#endif

enum ms912x_5to4_stage {
	MS912X_5TO4_HORIZONTAL,
	MS912X_5TO4_VERTICAL,
	MS912X_5TO4_DIRECT,
};

/* Four area phases use (4,1), (3,2), (2,3), (1,4); round once after both axes. */
static __always_inline u32 ms912x_5to4_area_pixel(const u32 *row0, const u32 *row1,
		unsigned int width, unsigned int x, unsigned int ox, unsigned int phase_y)
{
	const u32 mask = 0x00ff00ff;
	unsigned int sx = ox + x + x / 4;
	unsigned int wx = 4 - (x & 3), wy = 4 - phase_y;
	u32 a = row0[min(sx, width - 1)], b = row0[min(sx + 1, width - 1)];
	u32 c = row1[min(sx, width - 1)], d = row1[min(sx + 1, width - 1)];
	u32 rb = ((a & mask) * wx + (b & mask) * (5 - wx)) * wy +
		 ((c & mask) * wx + (d & mask) * (5 - wx)) * (5 - wy);
	u32 ga = (((a >> 8) & mask) * wx + ((b >> 8) & mask) * (5 - wx)) * wy +
		 (((c >> 8) & mask) * wx + ((d >> 8) & mask) * (5 - wx)) * (5 - wy);

	/* The packed 16-bit lanes cannot carry: each numerator is at most 6375. */
	return (((rb & 65535) + 12) / 25) |
	       ((((ga & 65535) + 12) / 25) << 8) |
	       (((rb >> 16) + 12) / 25 << 16) |
	       (((ga >> 16) + 12) / 25 << 24);
}

/* Q7 Catmull-Rom at 1/8, 3/8, 5/8 and 7/8; the latter phases mirror the first. */
static __always_inline u32 ms912x_5to4_cubic_pixel(u32 near, u32 center,
						 u32 outer, u32 far, unsigned int inner)
{
	int n = 123 - 30 * (int)inner, c = 12 + 38 * (int)inner;
	int o = 6 + 3 * (int)inner, f = 1 + 5 * (int)inner;
	u32 out = 0;
	unsigned int shift;

	for (shift = 0; shift < 32; shift += 8) {
		int sum = n * (int)((near >> shift) & 255) +
			  c * (int)((center >> shift) & 255) -
			  o * (int)((outer >> shift) & 255) -
			  f * (int)((far >> shift) & 255);

		out |= (u32)clamp((sum + 64) >> 7, 0, 255) << shift;
	}
	return out;
}

static __always_inline u32 ms912x_5to4_cubic_horizontal(const u32 *row,
		unsigned int width, unsigned int x, unsigned int ox)
{
	unsigned int sx = ox + x + x / 4, phase = x & 3;
	unsigned int reverse = phase >> 1, mask = -reverse;
	unsigned int left = min(sx - (sx != 0), width - 1);
	unsigned int right = min(sx + 2, width - 1);
	unsigned int a = min(sx + reverse, width - 1);
	unsigned int b = min(sx + 1 - reverse, width - 1);
	unsigned int c = (left & ~mask) | (right & mask);
	unsigned int d = (right & ~mask) | (left & mask);

	return ms912x_5to4_cubic_pixel(row[a], row[b], row[c], row[d],
					 (phase ^ (phase >> 1)) & 1);
}

static __always_inline void ms912x_5to4_cubic_rows(const u32 *src,
		unsigned int height, unsigned int stride, unsigned int y,
		unsigned int oy, const u32 **rows)
{
	unsigned int sy = oy + y / 4 * 5, i;

	rows[0] = src + min(sy - (sy != 0), height - 1) * stride;
	for (i = 1; i < 7; i++)
		rows[i] = src + min(sy + i - 1, height - 1) * stride;
}

static __always_inline void ms912x_5to4_cubic_column(const u32 **rows,
		unsigned int width, unsigned int x, unsigned int ox,
		bool vertical, u32 *out)
{
	u32 v[7];
	unsigned int i;

	for (i = 0; i < 7; i++)
		v[i] = vertical ? rows[i][x] :
			ms912x_5to4_cubic_horizontal(rows[i], width, x, ox);
	out[0] = ms912x_5to4_cubic_pixel(v[1], v[2], v[0], v[3], 0);
	out[1] = ms912x_5to4_cubic_pixel(v[2], v[3], v[1], v[4], 1);
	out[2] = ms912x_5to4_cubic_pixel(v[4], v[3], v[5], v[2], 1);
	out[3] = ms912x_5to4_cubic_pixel(v[5], v[4], v[6], v[3], 0);
}

#define MS912X_DECLARE_5TO4(name) \
void name(const u32 *src, u32 *dst, unsigned int sw, unsigned int sh, \
	  unsigned int dw, unsigned int dh, unsigned int ss, unsigned int ds); \
void name##_region(const u32 *src, u32 *dst, unsigned int sw, unsigned int sh, \
	  unsigned int dw, unsigned int dh, unsigned int ss, unsigned int ds, \
	  unsigned int ox, unsigned int oy)

MS912X_DECLARE_5TO4(ms912x_downscale_xrgb8888_5to4);
MS912X_DECLARE_5TO4(ms912x_downscale_xrgb8888_5to4_scalar);
#ifdef CONFIG_X86_64
MS912X_DECLARE_5TO4(ms912x_downscale_xrgb8888_5to4_sse2);
MS912X_DECLARE_5TO4(ms912x_downscale_xrgb8888_5to4_avx2);
#endif
#if MS912X_HAS_NEON
MS912X_DECLARE_5TO4(ms912x_downscale_xrgb8888_5to4_neon);
void ms912x_downscale_xrgb8888_5to4_neon_rows_fpu(const u32 *src, u32 *dst,
		unsigned int sw, unsigned int sh, unsigned int dw,
		unsigned int ss, unsigned int ds, unsigned int first, unsigned int end,
		unsigned int ox, unsigned int oy);
#endif
#undef MS912X_DECLARE_5TO4

#endif /* MS912X_DOWNSCALE_5TO4_H */
