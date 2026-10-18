/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MS912X_SCAN_METHODS_H
#define MS912X_SCAN_METHODS_H

#include <linux/types.h>

struct ms912x_scan_row_result {
	u64 changed_pixels;
	u64 memcmp_blocks;
	u64 memcmp_equal_blocks;
	unsigned int min_x;
	unsigned int max_x;
	bool equal_row;
};

typedef void (*ms912x_scan_row_fn)(const u32 *old, const u32 *new,
				   unsigned int width,
				   struct ms912x_scan_row_result *result);

void ms912x_scan_row_scalar(const u32 *old, const u32 *new,
			    unsigned int width,
			    struct ms912x_scan_row_result *result);
void ms912x_scan_row_memcmp(const u32 *old, const u32 *new,
			    unsigned int width,
			    struct ms912x_scan_row_result *result);

#endif /* MS912X_SCAN_METHODS_H */
