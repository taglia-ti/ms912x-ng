/* SPDX-License-Identifier: GPL-2.0-only */

#include "ms912x_scan_core.h"
#include "../ms912x_scan_runs.h"

#ifdef __KERNEL__
#include <linux/errno.h>
#else
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#endif

static int scan_word_count(unsigned int width, unsigned int height,
			   size_t row_stride_words, unsigned int pixel_shift,
			   size_t *count)
{
	size_t word_width;

	if (!width || !height || width > 8192 || height > 8192 ||
	    pixel_shift > 1 || (pixel_shift && (width & 1U)))
		return -EINVAL;
	word_width = (size_t)width >> pixel_shift;
	if (row_stride_words < word_width ||
	    (size_t)(height - 1U) >
		    (((size_t)-1) - word_width) / row_stride_words)
		return -EINVAL;
	*count = (size_t)(height - 1U) * row_stride_words + word_width;
	return 0;
}

int ms912x_core_scan_words(const ms912x_core_u32 *baseline,
			   size_t baseline_word_count,
			   const ms912x_core_u32 *newest,
			   size_t newest_word_count, unsigned int width,
			   unsigned int height, size_t row_stride_words,
			   unsigned int pixel_shift, ms912x_core_u32 pixel_mask,
			   struct ms912x_core_rect *rects, size_t rect_capacity,
			   size_t *rect_count)
{
	size_t required_words, required_rects;
	size_t word_width;
	size_t output_index = 0;
	unsigned int y;
	int ret;

	if (!baseline || !newest || !rect_count || (rect_capacity && !rects))
		return -EINVAL;
	ret = scan_word_count(width, height, row_stride_words, pixel_shift,
			      &required_words);
	if (ret)
		return ret;
	if (baseline_word_count < required_words ||
	    newest_word_count < required_words)
		return -EINVAL;
	word_width = (size_t)width >> pixel_shift;
	for (unsigned int pass = 0; pass < 2; ++pass) {
		required_rects = 0;
		for (y = 0; y < height; ++y) {
			const ms912x_core_u32 *old_row =
				baseline + (size_t)y * row_stride_words;
			const ms912x_core_u32 *new_row =
				newest + (size_t)y * row_stride_words;
			unsigned int cursor = 0, first, end;
			while (ms912x_scan_row_next_run_mask(
				old_row, new_row, word_width, word_width - 1, 1,
				pixel_mask, &cursor, &first, &end)) {
				if (pass) {
					rects[required_rects] =
						(struct ms912x_core_rect){
							first << pixel_shift, y,
							end << pixel_shift,
							y + 1
						};
				}
				required_rects++;
			}
		}
		*rect_count = required_rects;
		if (required_rects > rect_capacity)
			return -ENOSPC;
		output_index = required_rects;
	}
	*rect_count = output_index;
	return 0;
}
