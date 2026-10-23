/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MS912X_SCAN_RUNS_H
#define MS912X_SCAN_RUNS_H
#include "ms912x_platform.h"
static __always_inline bool ms912x_scan_row_next_run_mask(
	const u32 *old, const u32 *new, unsigned int width,
	unsigned int max_changed_x, unsigned int block_pixels, u32 pixel_mask,
	unsigned int *cursor, unsigned int *run_start, unsigned int *run_end)
{
	unsigned int block = max(block_pixels, 1U);
	unsigned int search_end =
		min(width, (max_changed_x / block + 1) * block);

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

#endif
