// SPDX-License-Identifier: GPL-2.0-only

#include <linux/kernel.h>
#include <linux/string.h>

#include "ms912x_scan_methods.h"

#define MS912X_SCAN_MEMCMP_BLOCK_BYTES 128

static void ms912x_scan_row_result_init(
				struct ms912x_scan_row_result *result,
				unsigned int width)
{
	memset(result, 0, sizeof(*result));
	result->min_x = width;
}

static void ms912x_scan_row_record_change(
				struct ms912x_scan_row_result *result,
				unsigned int x)
{
	result->changed_pixels++;
	result->min_x = min(result->min_x, x);
	result->max_x = x;
}

void ms912x_scan_row_scalar(const u32 *old, const u32 *new,
			    unsigned int width,
			    struct ms912x_scan_row_result *result)
{
	unsigned int x;

	ms912x_scan_row_result_init(result, width);
	for (x = 0; x < width; x++) {
		if (((old[x] ^ new[x]) & 0x00ffffff) == 0)
			continue;
		ms912x_scan_row_record_change(result, x);
	}
}

void ms912x_scan_row_memcmp(const u32 *old, const u32 *new,
			    unsigned int width,
			    struct ms912x_scan_row_result *result)
{
	unsigned int x = 0;
	unsigned int block_pixels = MS912X_SCAN_MEMCMP_BLOCK_BYTES /
		sizeof(*new);

	ms912x_scan_row_result_init(result, width);
	if (!memcmp(old, new, width * sizeof(*new))) {
		result->equal_row = true;
		return;
	}

	while (x < width) {
		unsigned int block_end = min(width, x + block_pixels);

		result->memcmp_blocks++;
		if (!memcmp(&old[x], &new[x],
			    (block_end - x) * sizeof(*new))) {
			result->memcmp_equal_blocks++;
			x = block_end;
			continue;
		}

		/* A changed block is conservatively dirty; do not rescan its pixels. */
		result->changed_pixels += block_end - x;
		result->min_x = min(result->min_x, x);
		result->max_x = block_end - 1;
		x = block_end;
	}
}
