/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MS912X_SCAN_NEON_CORE_H
#define MS912X_SCAN_NEON_CORE_H
#ifdef __KERNEL__
#include <asm/neon-intrinsics.h>
#else
#include <arm_neon.h>
#endif
#include "ms912x_scan_methods.h"

/* AArch32 has pairwise reductions; AArch64 can reduce a whole register. */
#if defined(CONFIG_ARM64) || defined(__aarch64__)
static inline __attribute__((always_inline)) u32 ms912x_neon_max(uint32x4_t v)
{
	return vmaxvq_u32(v);
}
#else
static inline __attribute__((always_inline)) u32 ms912x_neon_max(uint32x4_t v)
{
	uint32x2_t p = vmax_u32(vget_low_u32(v), vget_high_u32(v));

	return vget_lane_u32(vpmax_u32(p, p), 0);
}
#endif

static inline __attribute__((always_inline)) uint32x4_t
ms912x_scan_neon_difference(const u32 *old, const u32 *new)
{
	return veorq_u32(vld1q_u32(old), vld1q_u32(new));
}

/* Reduce vector differences once per damage block, preserving its granularity. */
static inline __attribute__((always_inline)) void
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

#define MS912X_MASKED_TYPE uint32x4_t
#define MS912X_MASKED_PIXELS 4
#define MS912X_MASKED_ZERO() vdupq_n_u32(0)
#define MS912X_MASKED_OR(a, b) vorrq_u32(a, b)
#define MS912X_MASKED_DIFF(a, b) ms912x_scan_neon_difference(a, b)
#define MS912X_MASKED_CHANGED(diff, mask) \
	(ms912x_neon_max(vandq_u32(diff, vdupq_n_u32(mask))) != 0)
#define MS912X_MASKED_COPY(dst, src) vst1q_u32(dst, vld1q_u32(src))
#define MS912X_MASKED_BLOCK ms912x_scan_neon_masked_block
#define MS912X_MASKED_ROW ms912x_scan_row_neon_masked
#define MS912X_MASKED_PREFIX ms912x_scan_neon_masked_prefix
#include "ms912x_scan_masked_template.h"

#endif
