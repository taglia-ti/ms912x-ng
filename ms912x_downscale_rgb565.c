// SPDX-License-Identifier: GPL-2.0-only

#include <linux/kernel.h>

#include "ms912x_downscale.h"

static __always_inline u32 ms912x_rgb565_filter_channel(const u32 *p,
							const int *weights,
							unsigned int bits,
							unsigned int shift)
{
	unsigned int max_value = (1U << bits) - 1;
	int sum = weights[0] * (int)((p[0] >> shift) & max_value) +
		  weights[1] * (int)((p[1] >> shift) & max_value) -
		  weights[2] * (int)((p[2] >> shift) & max_value) -
		  weights[3] * (int)((p[3] >> shift) & max_value);

	return clamp_t(int, (sum + 64) >> 7, 0, max_value);
}

static __always_inline u32 ms912x_rgb565_filter4(u32 a, u32 b, u32 c, u32 d)
{
	const int weights[4] = { 111, 29, 9, 3 };
	u32 p[4] = { a, b, c, d };
	u32 r, g, bl;

	r = ms912x_rgb565_filter_channel(p, weights, 5, 11);
	g = ms912x_rgb565_filter_channel(p, weights, 6, 5);
	bl = ms912x_rgb565_filter_channel(p, weights, 5, 0);
	return (r << 11) | (g << 5) | bl;
}

static __always_inline u32 ms912x_rgb565_cubic3(const u16 *row,
						unsigned int width,
						unsigned int x,
						unsigned int offset)
{
	unsigned int sx = offset + x + x / 2;
	unsigned int phase = x & 1;
	unsigned int left = min(sx ? sx - 1 : 0, width - 1);
	unsigned int a = min(sx + phase, width - 1);
	unsigned int b = min(sx + 1 - phase, width - 1);
	unsigned int right = min(sx + 2, width - 1);

	return ms912x_rgb565_filter4(row[a], row[b], row[phase ? right : left],
				     row[phase ? left : right]);
}

static __always_inline u32 ms912x_rgb565_cubic5_h(const u16 *row,
						  unsigned int width,
						  unsigned int x,
						  unsigned int offset)
{
	unsigned int sx = offset + x + x / 4;
	unsigned int phase = x & 3;
	unsigned int reverse = phase >> 1;
	unsigned int left = min(sx ? sx - 1 : 0, width - 1);
	unsigned int right = min(sx + 2, width - 1);
	unsigned int a = min(sx + reverse, width - 1);
	unsigned int b = min(sx + 1 - reverse, width - 1);
	unsigned int c = reverse ? right : left;
	unsigned int d = reverse ? left : right;
	u32 p[4] = { row[a], row[b], row[c], row[d] };
	int n = 123 - 30 * (int)((phase ^ (phase >> 1)) & 1);
	int center = 12 + 38 * (int)((phase ^ (phase >> 1)) & 1);
	int outer = 6 + 3 * (int)((phase ^ (phase >> 1)) & 1);
	int far = 1 + 5 * (int)((phase ^ (phase >> 1)) & 1);
	int weights[4] = { n, center, outer, far };

	return (ms912x_rgb565_filter_channel(p, weights, 5, 11) << 11) |
	       (ms912x_rgb565_filter_channel(p, weights, 6, 5) << 5) |
	       ms912x_rgb565_filter_channel(p, weights, 5, 0);
}

static __always_inline u32 ms912x_rgb565_cubic5_v(const u32 p[7],
						  unsigned int phase)
{
	unsigned int near, center, outer, far, inner;
	int weights[4];
	u32 q[4];

	if (phase == 0) {
		near = 1;
		center = 2;
		outer = 0;
		far = 3;
		inner = 0;
	} else if (phase == 1) {
		near = 2;
		center = 3;
		outer = 1;
		far = 4;
		inner = 1;
	} else if (phase == 2) {
		near = 4;
		center = 3;
		outer = 5;
		far = 2;
		inner = 1;
	} else {
		near = 5;
		center = 4;
		outer = 6;
		far = 3;
		inner = 0;
	}
	weights[0] = 123 - 30 * inner;
	weights[1] = 12 + 38 * inner;
	weights[2] = 6 + 3 * inner;
	weights[3] = 1 + 5 * inner;
	q[0] = p[near];
	q[1] = p[center];
	q[2] = p[outer];
	q[3] = p[far];
	return (ms912x_rgb565_filter_channel(q, weights, 5, 11) << 11) |
	       (ms912x_rgb565_filter_channel(q, weights, 6, 5) << 5) |
	       ms912x_rgb565_filter_channel(q, weights, 5, 0);
}

static void ms912x_downscale_rgb565_2x(const u16 *src, u16 *dst,
				       unsigned int sw, unsigned int sh,
				       unsigned int dw, unsigned int dh,
				       unsigned int ss, unsigned int ds)
{
	unsigned int x, y;

	for (y = 0; y < dh; y++) {
		const u16 *r0 = src + min(2 * y, sh - 1) * ss;
		const u16 *r1 = src + min(2 * y + 1, sh - 1) * ss;
		for (x = 0; x < dw; x++) {
			unsigned int sx = min(2 * x, sw - 1);
			unsigned int sx1 = min(2 * x + 1, sw - 1);
			u32 a = r0[sx], b = r0[sx1], c = r1[sx], d = r1[sx1];
			u32 r = (((a >> 11) & 31) + ((b >> 11) & 31) +
				 ((c >> 11) & 31) + ((d >> 11) & 31) + 2) >>
				2;
			u32 g = (((a >> 5) & 63) + ((b >> 5) & 63) +
				 ((c >> 5) & 63) + ((d >> 5) & 63) + 2) >>
				2;
			u32 bl = ((a & 31) + (b & 31) + (c & 31) + (d & 31) +
				  2) >>
				 2;

			dst[y * ds + x] = (r << 11) | (g << 5) | bl;
		}
	}
}

#ifdef MS912X_DOWNSCALE_AREA_FILTER
static __always_inline u32 ms912x_rgb565_area_channel(
	u32 a, u32 b, u32 c, u32 d, unsigned int wx, unsigned int wy,
	unsigned int period, unsigned int shift, unsigned int mask)
{
	unsigned int sum = (((a >> shift) & mask) * wx +
			    ((b >> shift) & mask) * (period - wx)) *
				   wy +
			   (((c >> shift) & mask) * wx +
			    ((d >> shift) & mask) * (period - wx)) *
				   (period - wy);

	return (sum + period * period / 2) / (period * period);
}

/* Match the ARM XRGB filter policy without expanding the native framebuffer. */
static __always_inline void ms912x_rgb565_area(const u16 *src, u16 *dst,
					       unsigned int sw, unsigned int sh,
					       unsigned int dw, unsigned int dh,
					       unsigned int ss, unsigned int ds,
					       unsigned int ox, unsigned int oy,
					       unsigned int period)
{
	unsigned int x, y;

	for (y = 0; y < dh; y++) {
		unsigned int sy = oy + y + y / (period - 1);
		unsigned int wy = period - 1 - y % (period - 1);
		const u16 *r0 = src + min(sy, sh - 1) * ss;
		const u16 *r1 = src + min(sy + 1, sh - 1) * ss;

		for (x = 0; x < dw; x++) {
			unsigned int sx = ox + x + x / (period - 1);
			unsigned int wx = period - 1 - x % (period - 1);
			u32 a = r0[min(sx, sw - 1)],
			    b = r0[min(sx + 1, sw - 1)];
			u32 c = r1[min(sx, sw - 1)],
			    d = r1[min(sx + 1, sw - 1)];

			dst[y * ds + x] =
				(ms912x_rgb565_area_channel(a, b, c, d, wx, wy,
							    period, 11, 31)
				 << 11) |
				(ms912x_rgb565_area_channel(a, b, c, d, wx, wy,
							    period, 5, 63)
				 << 5) |
				ms912x_rgb565_area_channel(a, b, c, d, wx, wy,
							   period, 0, 31);
		}
	}
}
#endif

void ms912x_downscale_rgb565_region(enum ms912x_scale_mode mode, const u16 *src,
				    u16 *dst, unsigned int sw, unsigned int sh,
				    unsigned int dw, unsigned int dh,
				    unsigned int ss, unsigned int ds,
				    unsigned int ox, unsigned int oy)
{
	unsigned int x, y;

	if (!src || !dst || !sw || !sh || !dw || !dh || ox >= sw || oy >= sh)
		return;
	if (mode == MS912X_SCALE_MODE_DOWNSCALE_2_TO_1) {
		ms912x_downscale_rgb565_2x(src + oy * ss + ox, dst, sw - ox,
					   sh - oy, dw, dh, ss, ds);
		return;
	}
#ifdef MS912X_DOWNSCALE_AREA_FILTER
	if (mode == MS912X_SCALE_MODE_DOWNSCALE_3_TO_2) {
		ms912x_rgb565_area(src, dst, sw, sh, dw, dh, ss, ds, ox, oy, 3);
		return;
	}
	if (mode == MS912X_SCALE_MODE_DOWNSCALE_5_TO_4) {
		ms912x_rgb565_area(src, dst, sw, sh, dw, dh, ss, ds, ox, oy, 5);
		return;
	}
#endif
	if (mode == MS912X_SCALE_MODE_DOWNSCALE_3_TO_2) {
		for (y = 0; y < dh; y += 2) {
			unsigned int sy = oy + y / 2 * 3;
			const u16 *rows[5];
			unsigned int k;

			rows[0] = src + min(sy ? sy - 1 : 0, sh - 1) * ss;
			for (k = 1; k < 5; k++)
				rows[k] = src + min(sy + k - 1, sh - 1) * ss;
			for (x = 0; x < dw; x++) {
				u32 h[5];

				for (k = 0; k < 5; k++)
					h[k] = ms912x_rgb565_cubic3(rows[k], sw,
								    x, ox);
				dst[y * ds + x] = ms912x_rgb565_filter4(
					h[1], h[2], h[0], h[3]);
				if (y + 1 < dh)
					dst[(y + 1) * ds + x] =
						ms912x_rgb565_filter4(
							h[3], h[2], h[4], h[1]);
			}
		}
		return;
	}
	if (mode != MS912X_SCALE_MODE_DOWNSCALE_5_TO_4)
		return;
	for (y = 0; y < dh; y += 4) {
		unsigned int sy = oy + y / 4 * 5;
		unsigned int rows = min(4U, dh - y);

		for (x = 0; x < dw; x++) {
			u32 h[7];
			unsigned int k;

			for (k = 0; k < 7; k++) {
				unsigned int row =
					k ? sy + k - 1 : (sy ? sy - 1 : 0);

				h[k] = ms912x_rgb565_cubic5_h(
					src + min(row, sh - 1) * ss, sw, x, ox);
			}
			for (k = 0; k < rows; k++)
				dst[(y + k) * ds + x] =
					ms912x_rgb565_cubic5_v(h, k);
		}
	}
}

void ms912x_downscale_rgb565(enum ms912x_scale_mode mode, const u16 *src,
			     u16 *dst, unsigned int sw, unsigned int sh,
			     unsigned int dw, unsigned int dh, unsigned int ss,
			     unsigned int ds)
{
	ms912x_downscale_rgb565_region(mode, src, dst, sw, sh, dw, dh, ss, ds,
				       0, 0);
}
