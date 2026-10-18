/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MS912X_SCAN_METHODS_H
#define MS912X_SCAN_METHODS_H

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/string.h>

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

/* Amortize FPU entry/exit; damage blocks are independently 256 bytes. */
#define MS912X_SCAN_AVX2_ROWS_PER_BATCH 16
#define MS912X_SCAN_MEMCMP_BLOCK_BYTES 128

static __always_inline void
ms912x_scan_row_result_init(struct ms912x_scan_row_result *result,
			    unsigned int width)
{
	memset(result, 0, sizeof(*result));
	result->min_x = width;
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
	unsigned int block_pixels = MS912X_SCAN_MEMCMP_BLOCK_BYTES / sizeof(*new);

	ms912x_scan_row_result_init(result, width);
	if (!memcmp(old, new, width * sizeof(*new))) {
		result->equal_row = true;
		return;
	}

	while (x < width) {
		unsigned int block_end = min(width, x + block_pixels);
		unsigned int block_width = block_end - x;
		unsigned int changed = !!memcmp(&old[x], &new[x],
					       block_width * sizeof(*new));
		unsigned int mask = -changed;

		result->memcmp_blocks++;
		result->memcmp_equal_blocks += !changed;
		/* A changed block is conservatively dirty; don't rescan its pixels. */
		result->changed_pixels += block_width * changed;
		result->min_x = min(result->min_x,
				    x + block_width * !changed);
		result->max_x = max(result->max_x,
				    (block_end - 1) & mask);
		x = block_end;
	}
}

bool ms912x_scan_avx2_available(void);
void ms912x_scan_rows_avx2_blocks(const u32 *old, const u32 *new,
				  unsigned int width,
				  unsigned int row_stride,
				  unsigned int row_count,
				  struct ms912x_scan_row_result *results);

#endif /* MS912X_SCAN_METHODS_H */
