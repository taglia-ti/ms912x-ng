/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MS912X_DOWNSCALE_3TO2_H
#define MS912X_DOWNSCALE_3TO2_H

#include <linux/kernel.h>
#include <linux/types.h>

#include "ms912x_neon.h"

/* Bound rows per kernel SIMD section: one area pass or either cubic pass. */
#ifndef MS912X_DOWNSCALE_ROWS_PER_FPU
#define MS912X_DOWNSCALE_ROWS_PER_FPU 16
#endif

/* Keep the filter stable when a SIMD backend falls back to scalar. */
#if defined(CONFIG_ARM64) || defined(CONFIG_ARM)
#define MS912X_DOWNSCALE_AREA_FILTER 1
#else
#define MS912X_DOWNSCALE_CATMULL_ROM_Q7 1
#endif

enum ms912x_downscale_stage {
	MS912X_DOWNSCALE_HORIZONTAL,
	MS912X_DOWNSCALE_VERTICAL,
	MS912X_DOWNSCALE_DIRECT,
};

#ifdef MS912X_DOWNSCALE_AREA_FILTER
/*
 * Exact 3->2 area filter. For each axis the phases use weights (2,1) and
 * (1,2). Select the weight-2 sample first, then round only after both axes:
 * (4*a + 2*b + 2*c + d + 4) / 9. All four byte channels are preserved.
 */
static __always_inline u32
ms912x_downscale_area_channel(u32 a, u32 b, u32 c, u32 d, unsigned int shift)
{
	unsigned int sum = 4 * ((a >> shift) & 255) +
			   2 * ((b >> shift) & 255) +
			   2 * ((c >> shift) & 255) + ((d >> shift) & 255);

	return (sum + 4) / 9;
}

static __always_inline u32
ms912x_downscale_area_pixel(const u32 *heavy, const u32 *light,
			    unsigned int src_width, unsigned int x,
			    unsigned int offset)
{
	unsigned int sx = offset + x + x / 2;
	unsigned int phase = x & 1;
	unsigned int near = min(sx + phase, src_width - 1);
	unsigned int center = min(sx + 1 - phase, src_width - 1);
	u32 a = heavy[near], b = heavy[center];
	u32 c = light[near], d = light[center];

	return ms912x_downscale_area_channel(a, b, c, d, 0) |
	       (ms912x_downscale_area_channel(a, b, c, d, 8) << 8) |
	       (ms912x_downscale_area_channel(a, b, c, d, 16) << 16) |
	       (ms912x_downscale_area_channel(a, b, c, d, 24) << 24);
}

#else
/*
 * Catmull-Rom at source phases 1/4 and 3/4. In near/center/outer order both
 * phases use (111*near + 29*center - 9*outer9 - 3*outer3) / 128.
 * Round and clamp after each separable pass, preserving all byte channels.
 */
static __always_inline u32
ms912x_downscale_cubic_pixel(u32 near, u32 center, u32 outer9, u32 outer3)
{
	u32 out = 0;
	unsigned int shift;

	for (shift = 0; shift < 32; shift += 8) {
		int sum = 111 * (int)((near >> shift) & 255) +
			   29 * (int)((center >> shift) & 255) -
			   9 * (int)((outer9 >> shift) & 255) -
			   3 * (int)((outer3 >> shift) & 255);

		out |= (u32)clamp((sum + 64) >> 7, 0, 255) << shift;
	}
	return out;
}

static __always_inline u32
ms912x_downscale_cubic_horizontal(const u32 *row, unsigned int width,
				 unsigned int x, unsigned int offset)
{
	unsigned int sx = offset + x + x / 2;
	unsigned int phase = x & 1;
	unsigned int left = min(sx ? sx - 1 : 0, width - 1);
	unsigned int a = min(sx, width - 1);
	unsigned int b = min(sx + 1, width - 1);
	unsigned int right = min(sx + 2, width - 1);

	return phase ? ms912x_downscale_cubic_pixel(row[b], row[a], row[right], row[left]) :
		       ms912x_downscale_cubic_pixel(row[a], row[b], row[left], row[right]);
}

static __always_inline void
ms912x_downscale_cubic_rows(const u32 *src, unsigned int height, unsigned int stride,
			   unsigned int y, unsigned int offset, const u32 **rows)
{
	unsigned int sy = offset + y / 2 * 3;

	rows[0] = src + min(sy ? sy - 1 : 0, height - 1) * stride;
	rows[1] = src + min(sy, height - 1) * stride;
	rows[2] = src + min(sy + 1, height - 1) * stride;
	rows[3] = src + min(sy + 2, height - 1) * stride;
	rows[4] = src + min(sy + 3, height - 1) * stride;
}

static __always_inline void
ms912x_downscale_cubic_edge(const u32 **rows, unsigned int width,
			   unsigned int x, unsigned int offset,
			   u32 *top, u32 *bottom)
{
	u32 a = ms912x_downscale_cubic_horizontal(rows[0], width, x, offset);
	u32 b = ms912x_downscale_cubic_horizontal(rows[1], width, x, offset);
	u32 c = ms912x_downscale_cubic_horizontal(rows[2], width, x, offset);
	u32 d = ms912x_downscale_cubic_horizontal(rows[3], width, x, offset);
	u32 e = ms912x_downscale_cubic_horizontal(rows[4], width, x, offset);

	*top = ms912x_downscale_cubic_pixel(b, c, a, d);
	*bottom = ms912x_downscale_cubic_pixel(d, c, e, b);
}

#endif

typedef void (*ms912x_downscale_fn)(const u32 *src, u32 *dst,
				    unsigned int src_width,
				    unsigned int src_height,
				    unsigned int dst_width,
				    unsigned int dst_height,
				    unsigned int src_stride,
				    unsigned int dst_stride);

void ms912x_downscale_xrgb8888_3to2_scalar(const u32 *src, u32 *dst,
					unsigned int src_width,
					unsigned int src_height,
					unsigned int dst_width,
					unsigned int dst_height,
					unsigned int src_stride,
					unsigned int dst_stride);
void ms912x_downscale_xrgb8888_3to2(const u32 *src, u32 *dst,
					 unsigned int src_width,
					 unsigned int src_height,
					 unsigned int dst_width,
					 unsigned int dst_height,
					 unsigned int src_stride,
					 unsigned int dst_stride);

void ms912x_downscale_xrgb8888_3to2_scalar_region(const u32 *src, u32 *dst,
		unsigned int src_width, unsigned int src_height,
		unsigned int dst_width, unsigned int dst_height,
		unsigned int src_stride, unsigned int dst_stride,
		unsigned int offset_x, unsigned int offset_y);
void ms912x_downscale_xrgb8888_3to2_region(const u32 *src, u32 *dst,
		unsigned int src_width, unsigned int src_height,
		unsigned int dst_width, unsigned int dst_height,
		unsigned int src_stride, unsigned int dst_stride,
		unsigned int offset_x, unsigned int offset_y);

#ifdef CONFIG_X86_64
void ms912x_downscale_xrgb8888_3to2_avx2_region(const u32 *src, u32 *dst,
		unsigned int src_width, unsigned int src_height,
		unsigned int dst_width, unsigned int dst_height,
		unsigned int src_stride, unsigned int dst_stride,
		unsigned int offset_x, unsigned int offset_y);

void ms912x_downscale_xrgb8888_3to2_sse2_region(const u32 *src, u32 *dst,
		unsigned int src_width, unsigned int src_height,
		unsigned int dst_width, unsigned int dst_height,
		unsigned int src_stride, unsigned int dst_stride,
		unsigned int offset_x, unsigned int offset_y);

void ms912x_downscale_xrgb8888_3to2_ssse3_region(const u32 *src, u32 *dst,
		unsigned int src_width, unsigned int src_height,
		unsigned int dst_width, unsigned int dst_height,
		unsigned int src_stride, unsigned int dst_stride,
		unsigned int offset_x, unsigned int offset_y);

void ms912x_downscale_xrgb8888_3to2_sse2(const u32 *src, u32 *dst,
					       unsigned int src_width,
					       unsigned int src_height,
					       unsigned int dst_width,
					       unsigned int dst_height,
					       unsigned int src_stride,
					       unsigned int dst_stride);
void ms912x_downscale_xrgb8888_3to2_ssse3(const u32 *src, u32 *dst,
						unsigned int src_width,
						unsigned int src_height,
						unsigned int dst_width,
						unsigned int dst_height,
						unsigned int src_stride,
						unsigned int dst_stride);
void ms912x_downscale_xrgb8888_3to2_avx2(const u32 *src, u32 *dst,
					       unsigned int src_width,
					       unsigned int src_height,
					       unsigned int dst_width,
					       unsigned int dst_height,
					       unsigned int src_stride,
					       unsigned int dst_stride);
#endif

#if MS912X_HAS_NEON
void ms912x_downscale_xrgb8888_3to2_neon_region(const u32 *src, u32 *dst,
		unsigned int src_width, unsigned int src_height,
		unsigned int dst_width, unsigned int dst_height,
		unsigned int src_stride, unsigned int dst_stride,
		unsigned int offset_x, unsigned int offset_y);

void ms912x_downscale_xrgb8888_3to2_neon_rows_fpu(
		const u32 *src, u32 *dst, unsigned int src_width,
		unsigned int src_height, unsigned int dst_width,
		unsigned int src_stride, unsigned int dst_stride,
		unsigned int first_row, unsigned int end_row,
		unsigned int offset_x, unsigned int offset_y);
void ms912x_downscale_xrgb8888_3to2_neon(const u32 *src, u32 *dst,
					      unsigned int src_width,
					      unsigned int src_height,
					      unsigned int dst_width,
					      unsigned int dst_height,
					      unsigned int src_stride,
					      unsigned int dst_stride);
#endif

#endif /* MS912X_DOWNSCALE_3TO2_H */
