/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MS912X_SCAN_METHODS_H
#define MS912X_SCAN_METHODS_H

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include "ms912x_neon.h"
#else
#include "ms912x_platform.h"
#ifndef DIV_ROUND_UP
#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#endif
#endif
#include "ms912x_damage_policy.h"
#include "ms912x_scan_rects.h"

/* Widths, x bounds and block constants count u32 comparison words. */
struct ms912x_scan_row_result {
	u64 changed_pixels;
	u64 memcmp_blocks;
	u64 memcmp_equal_blocks;
	u64 avx2_blocks;
	u64 avx2_equal_blocks;
	unsigned int min_x;
	unsigned int max_x;
	bool equal_row;
};

/* Amortize FPU entry/exit across sampled rows of the UYVY cache. */
#define MS912X_SCAN_SIMD_ROWS_PER_BATCH 16
#define MS912X_SCAN_MEMCMP_BLOCK_BYTES 128
#define MS912X_SCAN_MASKED_BLOCK_BYTES 128
#define MS912X_SCAN_MASKED_BLOCK_PIXELS (MS912X_SCAN_MASKED_BLOCK_BYTES / sizeof(u32))

static inline __attribute__((always_inline)) void
ms912x_scan_row_result_init(struct ms912x_scan_row_result *result,
			    unsigned int width)
{
	memset(result, 0, sizeof(*result));
	result->min_x = width;
}

static inline __attribute__((always_inline)) void
ms912x_scan_record_block(struct ms912x_scan_row_result *result,
			unsigned int width, unsigned int first,
			unsigned int end, unsigned int changed)
{
	unsigned int mask = -changed;

	result->avx2_blocks++;
	result->avx2_equal_blocks += !changed;
	result->changed_pixels += (end - first) * changed;
	result->min_x = min(result->min_x,
			    (first & mask) | (width & ~mask));
	result->max_x = max(result->max_x, (end - 1) & mask);
}

static inline __attribute__((always_inline)) void
ms912x_scan_row_memcmp(const u32 *old, const u32 *new, unsigned int width,
		       struct ms912x_scan_row_result *result)
{
	unsigned int x = 0;
	const unsigned int block_pixels =
		MS912X_SCAN_MEMCMP_BLOCK_BYTES / sizeof(*new);
	unsigned int full_pixels, search_pixels;
	unsigned int changed_blocks = 0, tail_changed = 0;
	unsigned int first = width, last = 0;

	ms912x_scan_row_result_init(result, width);
	if (!memcmp(old, new, width * sizeof(*new))) {
		result->equal_row = true;
		return;
	}

	full_pixels = width - width % block_pixels;
	/*
	 * The private snapshots are stable. If every earlier block matches,
	 * the row comparison already proves that its final block differs.
	 */
	search_pixels = width == full_pixels ? full_pixels - block_pixels :
		full_pixels;
	/* Skip the clean prefix without updating bounds or block counters. */
	for (; x < search_pixels; x += block_pixels) {
		if (memcmp(old + x, new + x, MS912X_SCAN_MEMCMP_BLOCK_BYTES))
			break;
	}
	if (x < full_pixels) {
		first = x;
		last = x + block_pixels - 1;
		changed_blocks = 1;
		x += block_pixels;
		for (; x < full_pixels; x += block_pixels) {
			unsigned int changed = !!memcmp(old + x, new + x,
						       MS912X_SCAN_MEMCMP_BLOCK_BYTES);

			changed_blocks += changed;
			/* Blocks are visited in order; only the final bound can move. */
			last = changed ? x + block_pixels - 1 : last;
		}
	}
	if (x < width) {
		tail_changed = !changed_blocks || memcmp(old + x, new + x,
					(width - x) * sizeof(*new));
		if (tail_changed) {
			first = min(first, x);
			last = width - 1;
		}
	}
	/* Keep reductions local across memcmp calls and publish the row once. */
	result->memcmp_blocks = DIV_ROUND_UP(width, block_pixels);
	result->memcmp_equal_blocks = result->memcmp_blocks -
		changed_blocks - tail_changed;
	result->changed_pixels = (u64)changed_blocks * block_pixels +
		(width - full_pixels) * tail_changed;
	result->min_x = first;
	result->max_x = last;
}

static inline __attribute__((always_inline)) u32
ms912x_scan_uyvy_tolerance_mask(unsigned int tolerance)
{
	return ~(ms912x_damage_scan_tolerance_bucket(tolerance) * 0x01010101U);
}

#define MS912X_MASKED_TYPE u32
#define MS912X_MASKED_PIXELS 1
#define MS912X_MASKED_ZERO() 0
#define MS912X_MASKED_OR(a, b) ((a) | (b))
#define MS912X_MASKED_DIFF(a, b) (*(a) ^ *(b))
#define MS912X_MASKED_CHANGED(diff, mask) (((diff) & (mask)) != 0)
#define MS912X_MASKED_COPY(dst, src) (*(dst) = *(src))
#define MS912X_MASKED_BLOCK ms912x_scan_scalar_masked_block
#define MS912X_MASKED_ROW ms912x_scan_row_scalar_masked
#define MS912X_MASKED_PREFIX ms912x_scan_scalar_masked_prefix
#include "ms912x_scan_masked_template.h"

bool ms912x_scan_avx_available(void);
bool ms912x_scan_sse2_available(void);
bool ms912x_scan_neon_available(void);
void ms912x_scan_rows_avx_relaxed_masked(const u32 *old, const u32 *new,
				   u32 *next, unsigned int width,
				   unsigned int row_stride, unsigned int row_count,
				   u32 pixel_mask,
				   struct ms912x_scan_row_result *results);
void ms912x_scan_rows_sse2_relaxed_masked(const u32 *old, const u32 *new,
				   u32 *next, unsigned int width,
				   unsigned int row_stride, unsigned int row_count,
				   u32 pixel_mask,
				   struct ms912x_scan_row_result *results);
void ms912x_scan_rows_neon_relaxed_masked(const u32 *old, const u32 *new,
				   u32 *next, unsigned int width,
				   unsigned int row_stride, unsigned int row_count,
				   u32 pixel_mask,
				   struct ms912x_scan_row_result *results);

#endif /* MS912X_SCAN_METHODS_H */
