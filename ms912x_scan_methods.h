/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MS912X_SCAN_METHODS_H
#define MS912X_SCAN_METHODS_H

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include "ms912x_neon.h"
#include "ms912x_damage_policy.h"

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

/* Amortize FPU entry/exit across sampled rows. Exact AVX blocks are 256 bytes. */
#define MS912X_SCAN_SIMD_ROWS_PER_BATCH 16
#define MS912X_SCAN_MEMCMP_BLOCK_BYTES 128
#define MS912X_SCAN_MASKED_BLOCK_BYTES 128
#define MS912X_SCAN_MASKED_BLOCK_PIXELS (MS912X_SCAN_MASKED_BLOCK_BYTES / sizeof(u32))

static __always_inline void
ms912x_scan_row_result_init(struct ms912x_scan_row_result *result,
			    unsigned int width)
{
	memset(result, 0, sizeof(*result));
	result->min_x = width;
}

static __always_inline void
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

static __always_inline void
ms912x_scan_row_scalar(const u32 *old, const u32 *new, unsigned int width,
		       struct ms912x_scan_row_result *result)
{
	unsigned int x;

	ms912x_scan_row_result_init(result, width);
	for (x = 0; x < width; x++) {
		unsigned int changed = ((old[x] ^ new[x]) & 0x00ffffff) != 0;
		unsigned int mask = -changed;

		result->changed_pixels += changed;
		result->min_x = min(result->min_x,
				    (x & mask) | (width & ~mask));
		result->max_x = max(result->max_x, x & mask);
	}
	result->equal_row = !result->changed_pixels;
}

static __always_inline void
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

static __always_inline bool
ms912x_scan_pixel_relaxed_changed(u32 old, u32 new, unsigned int tolerance)
{
	u32 ignored = ms912x_damage_scan_tolerance_bucket(tolerance) * 0x00010101U;

	return ((old ^ new) & (0x00ffffffU & ~ignored)) != 0;
}

static __always_inline u32 ms912x_scan_tolerance_mask(unsigned int tolerance)
{
	return 0x00ffffffU &
		~(ms912x_damage_scan_tolerance_bucket(tolerance) * 0x00010101U);
}

static __always_inline bool
ms912x_scan_row_next_run(const u32 *old, const u32 *new, unsigned int width,
			 unsigned int max_changed_x,
			 unsigned int block_pixels, unsigned int tolerance,
			 unsigned int *cursor, unsigned int *run_start,
			 unsigned int *run_end)
{
	unsigned int block = max(block_pixels, 1U);
	unsigned int search_end = min(width,
					      (max_changed_x / block + 1) * block);
	u32 pixel_mask = ms912x_scan_tolerance_mask(tolerance);

	while (*cursor < search_end) {
		unsigned int start = *cursor;
		unsigned int end = min(width, start + block);
		unsigned int x;
		bool changed = false;

		for (x = start; x < end; x++) {
			if (((old[x] ^ new[x]) & pixel_mask) != 0) {
				changed = true;
				break;
			}
		}
		if (!changed) {
			*cursor = end;
			continue;
		}

		*run_start = start;
		*cursor = end;
		while (*cursor < search_end) {
			start = *cursor;
			end = min(width, start + block);
			for (x = start; x < end; x++) {
				if (((old[x] ^ new[x]) & pixel_mask) != 0) {
					*cursor = end;
					break;
				}
			}
			if (x == end)
				break;
		}
		*run_end = *cursor;
		return true;
	}
	return false;
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
#include "ms912x_scan_masked_template.h"

static __always_inline void
ms912x_scan_row_scalar_relaxed(const u32 *old, const u32 *new, u32 *next,
			       unsigned int width, unsigned int tolerance,
			       struct ms912x_scan_row_result *result)
{
	ms912x_scan_row_scalar_masked(old, new, next, width,
				      ms912x_scan_tolerance_mask(tolerance), result);
}

bool ms912x_scan_avx_available(void);
bool ms912x_scan_sse2_available(void);
bool ms912x_scan_neon_available(void);
void ms912x_scan_rows_avx_blocks(const u32 *old, const u32 *new,
				  unsigned int width,
				  unsigned int row_stride,
				  unsigned int row_count,
				  struct ms912x_scan_row_result *results);
void ms912x_scan_rows_avx_relaxed(const u32 *old, const u32 *new,
				   u32 *next, unsigned int width,
				   unsigned int row_stride,
				   unsigned int row_count,
				   unsigned int tolerance,
				   struct ms912x_scan_row_result *results);
void ms912x_scan_rows_sse2_blocks(const u32 *old, const u32 *new,
				  unsigned int width, unsigned int row_stride,
				  unsigned int row_count,
				  struct ms912x_scan_row_result *results);
void ms912x_scan_rows_sse2_relaxed(const u32 *old, const u32 *new,
				   u32 *next, unsigned int width,
				   unsigned int row_stride, unsigned int row_count,
				   unsigned int tolerance,
				   struct ms912x_scan_row_result *results);
void ms912x_scan_rows_neon_blocks(const u32 *old, const u32 *new,
				  unsigned int width, unsigned int row_stride,
				  unsigned int row_count,
				  struct ms912x_scan_row_result *results);
void ms912x_scan_rows_neon_relaxed(const u32 *old, const u32 *new,
				   u32 *next, unsigned int width,
				   unsigned int row_stride, unsigned int row_count,
				   unsigned int tolerance,
				   struct ms912x_scan_row_result *results);

#endif /* MS912X_SCAN_METHODS_H */
