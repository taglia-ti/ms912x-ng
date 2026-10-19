// SPDX-License-Identifier: GPL-2.0-only

#include <linux/kernel.h>

#include <asm/neon-intrinsics.h>

#include "ms912x_scan_methods.h"

/* AArch32 has pairwise reductions; AArch64 can reduce a whole register. */
static __always_inline u32 ms912x_neon_max(uint32x4_t v)
{
#ifdef CONFIG_ARM64
	return vmaxvq_u32(v);
#else
	uint32x2_t p = vmax_u32(vget_low_u32(v), vget_high_u32(v));

	return vget_lane_u32(vpmax_u32(p, p), 0);
#endif
}

static __always_inline u32 ms912x_neon_min(uint32x4_t v)
{
#ifdef CONFIG_ARM64
	return vminvq_u32(v);
#else
	uint32x2_t p = vmin_u32(vget_low_u32(v), vget_high_u32(v));

	return vget_lane_u32(vpmin_u32(p, p), 0);
#endif
}

static __always_inline u32 ms912x_neon_sum(uint32x4_t v)
{
#ifdef CONFIG_ARM64
	return vaddvq_u32(v);
#else
	uint32x2_t p = vadd_u32(vget_low_u32(v), vget_high_u32(v));

	return vget_lane_u32(vpadd_u32(p, p), 0);
#endif
}

static __always_inline uint32x4_t
ms912x_scan_neon_difference(const u32 *old, const u32 *new)
{
	return veorq_u32(vld1q_u32(old), vld1q_u32(new));
}

/* Reduce vector differences once per damage block, preserving its granularity. */
static __always_inline void
ms912x_scan_row_neon_blocks(const u32 *old, const u32 *new, unsigned int width,
			    struct ms912x_scan_row_result *result)
{
	unsigned int x = 0, changed_blocks = 0, tail_changed = 0;
	unsigned int first = width, last = 0;
	unsigned int full_pixels = width & ~15U;

	ms912x_scan_row_result_init(result, width);
	for (; x + 16 <= width; x += 16) {
		uint32x4_t d0 = ms912x_scan_neon_difference(old + x, new + x);
		uint32x4_t d1 = ms912x_scan_neon_difference(old + x + 4, new + x + 4);
		uint32x4_t d2 = ms912x_scan_neon_difference(old + x + 8, new + x + 8);
		uint32x4_t d3 = ms912x_scan_neon_difference(old + x + 12, new + x + 12);
		uint32x4_t diff;
		unsigned int changed;

		diff = vorrq_u32(vorrq_u32(d0, d1), vorrq_u32(d2, d3));
		changed = ms912x_neon_max(diff) != 0;
		changed_blocks += changed;
		first = min(first, changed ? x : width);
		last = changed ? x + 15 : last;
	}
	if (x < width) {
		uint32x4_t diff = vdupq_n_u32(0);
		unsigned int vx = x, changed = 0;

		for (; vx + 4 <= width; vx += 4)
			diff = vorrq_u32(diff, ms912x_scan_neon_difference(old + vx, new + vx));
		for (; vx < width; vx++)
			changed |= old[vx] != new[vx];
		changed |= ms912x_neon_max(diff) != 0;
		tail_changed = changed;
		if (changed) {
			first = min(first, x);
			last = width - 1;
		}
	}
	/* Publish once per row, avoiding 64-bit counter updates per block on ARM32. */
	result->avx2_blocks = DIV_ROUND_UP(width, 16);
	result->avx2_equal_blocks = result->avx2_blocks - changed_blocks - tail_changed;
	result->changed_pixels = changed_blocks * 16 + (width - full_pixels) * tail_changed;
	result->min_x = first;
	result->max_x = last;
	result->equal_row = !result->changed_pixels;
}

/* The integer-only glue owns the FPU section. */
noinline void
ms912x_scan_rows_neon_blocks_fpu(const u32 *old, const u32 *new, unsigned int width,
				 unsigned int row_stride, unsigned int row_count,
				 struct ms912x_scan_row_result *results)
{
	unsigned int row;

	for (row = 0; row < row_count; row++)
		ms912x_scan_row_neon_blocks(old + row * row_stride, new + row * row_stride, width,
					    &results[row]);
}

struct ms912x_neon_bounds {
	uint32x4_t counts;
	uint32x4_t first;
	uint32x4_t last;
};

static __always_inline void
ms912x_neon_scan_four(const u32 *old, const u32 *new, u32 *next,
		      uint32x4_t positions, uint8x16_t threshold,
		      struct ms912x_neon_bounds *bounds)
{
	uint8x16_t a = vld1q_u8((const u8 *)old);
	uint8x16_t b = vld1q_u8((const u8 *)new);
	uint8x16_t over = vandq_u8(vcgtq_u8(vabdq_u8(a, b), threshold),
				 vreinterpretq_u8_u32(vdupq_n_u32(0x00ffffff)));
	uint32x4_t equal = vceqq_u32(vreinterpretq_u32_u8(over), vdupq_n_u32(0));
	uint32x4_t changed = vmvnq_u32(equal);

	vst1q_u8((u8 *)next, vbslq_u8(vreinterpretq_u8_u32(equal), a, b));
	bounds->counts = vsubq_u32(bounds->counts, changed);
	bounds->first = vminq_u32(bounds->first, vorrq_u32(positions, equal));
	bounds->last = vmaxq_u32(bounds->last, vandq_u32(positions, changed));
}

static __always_inline void
ms912x_scan_row_neon_relaxed(const u32 *old, const u32 *new, u32 *next, unsigned int width,
			     unsigned int tolerance, struct ms912x_scan_row_result *result)
{
	const uint8x16_t threshold = vdupq_n_u8(tolerance);
	const uint32x4_t zero = vdupq_n_u32(0);
	struct ms912x_neon_bounds a = { zero, vdupq_n_u32(width), zero };
	uint32x4_t positions = {0, 1, 2, 3};
	unsigned int x = 0;

	ms912x_scan_row_result_init(result, width);
	if (width < 32) {
		/* Avoid the second accumulator setup and merge for short rows. */
		for (; x + 4 <= width; x += 4) {
			ms912x_neon_scan_four(old + x, new + x, next + x,
					      positions, threshold, &a);
			positions = vaddq_u32(positions, vdupq_n_u32(4));
		}
	} else {
		struct ms912x_neon_bounds b = a;

		/* Independent bounds/count chains overlap work on in-order cores. */
		for (; x + 8 <= width; x += 8) {
			ms912x_neon_scan_four(old + x, new + x, next + x,
					      positions, threshold, &a);
			ms912x_neon_scan_four(old + x + 4, new + x + 4, next + x + 4,
					      vaddq_u32(positions, vdupq_n_u32(4)), threshold, &b);
			positions = vaddq_u32(positions, vdupq_n_u32(8));
		}
		if (x + 4 <= width) {
			ms912x_neon_scan_four(old + x, new + x, next + x,
					      positions, threshold, &a);
			x += 4;
		}
		a.counts = vaddq_u32(a.counts, b.counts);
		a.first = vminq_u32(a.first, b.first);
		a.last = vmaxq_u32(a.last, b.last);
	}
	result->changed_pixels = ms912x_neon_sum(a.counts);
	result->min_x = ms912x_neon_min(a.first);
	result->max_x = ms912x_neon_max(a.last);
	for (; x < width; x++) {
		unsigned int changed = ms912x_scan_pixel_relaxed_changed(old[x], new[x], tolerance);
		unsigned int mask = -changed;

		result->changed_pixels += changed;
		result->min_x = min(result->min_x, (x & mask) | (width & ~mask));
		result->max_x = max(result->max_x, x & mask);
		next[x] = (old[x] & ~mask) | (new[x] & mask);
	}
	result->equal_row = !result->changed_pixels;
}

noinline void
ms912x_scan_rows_neon_relaxed_fpu(const u32 *old, const u32 *new, u32 *next, unsigned int width,
				  unsigned int row_stride, unsigned int row_count,
				  unsigned int tolerance, struct ms912x_scan_row_result *results)
{
	unsigned int row;

	for (row = 0; row < row_count; row++)
		ms912x_scan_row_neon_relaxed(old + row * row_stride, new + row * row_stride,
					     next + row * row_stride, width, tolerance,
					     &results[row]);
}
