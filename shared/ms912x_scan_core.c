/* SPDX-License-Identifier: GPL-2.0-only */

#include "ms912x_scan_core.h"

#ifdef __KERNEL__
#include <linux/errno.h>
#else
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#endif

static int scan_word_count(unsigned int width, unsigned int height,
			   size_t row_stride_words,
			   unsigned int pixel_shift, size_t *count)
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

static size_t count_changed_rows(const ms912x_core_u32 *baseline,
				 const ms912x_core_u32 *newest,
				 unsigned int width, unsigned int height,
				 size_t row_stride_words,
				 unsigned int pixel_shift,
				 ms912x_core_u32 pixel_mask)
{
	const size_t word_width = (size_t)width >> pixel_shift;
	size_t rect_count = 0;
	unsigned int y;

	for (y = 0; y < height; ++y) {
		const ms912x_core_u32 *old_row = baseline +
			(size_t)y * row_stride_words;
		const ms912x_core_u32 *new_row = newest +
			(size_t)y * row_stride_words;
		size_t x;
		int changed = 0;

		for (x = 0; x < word_width; ++x)
			changed |= ms912x_core_word_changed(old_row[x], new_row[x],
							   pixel_mask);
		rect_count += !!changed;
	}
	return rect_count;
}

int ms912x_core_scan_words(const ms912x_core_u32 *baseline,
			   size_t baseline_word_count,
			   const ms912x_core_u32 *newest,
			   size_t newest_word_count,
			   unsigned int width, unsigned int height,
			   size_t row_stride_words, unsigned int pixel_shift,
			   ms912x_core_u32 pixel_mask,
			   struct ms912x_core_rect *rects,
			   size_t rect_capacity, size_t *rect_count)
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
	required_rects = count_changed_rows(baseline, newest, width, height,
					    row_stride_words, pixel_shift,
					    pixel_mask);
	*rect_count = required_rects;
	if (required_rects > rect_capacity)
		return -ENOSPC;

	for (y = 0; y < height; ++y) {
		const ms912x_core_u32 *old_row = baseline +
			(size_t)y * row_stride_words;
		const ms912x_core_u32 *new_row = newest +
			(size_t)y * row_stride_words;
		size_t x, first = word_width, last = 0;

		for (x = 0; x < word_width; ++x) {
			if (!ms912x_core_word_changed(old_row[x], new_row[x],
						     pixel_mask))
				continue;
			if (first == word_width)
				first = x;
			last = x;
		}
		if (first == word_width)
			continue;
		rects[output_index].x1 = (ms912x_core_s32)(first << pixel_shift);
		rects[output_index].y1 = (ms912x_core_s32)y;
		rects[output_index].x2 = (ms912x_core_s32)((last + 1U) << pixel_shift);
		rects[output_index].y2 = (ms912x_core_s32)(y + 1U);
		output_index++;
	}
	*rect_count = output_index;
	return 0;
}
