/* SPDX-License-Identifier: GPL-2.0-only */

#include "ms912x_scan_core.h"
#include "ms912x_scan_backend.h"
#include "../ms912x_scan_runs.h"
#include "../ms912x_scan_rects.h"
#include "../ms912x_rect_limits.h"

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

	if (!width || !height || width > 8192 || height > MS912X_SCAN_MAX_HEIGHT ||
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

int ms912x_core_scan_sampled_words_backend(const ms912x_core_u32 *baseline,
			   size_t baseline_word_count,
			   const ms912x_core_u32 *newest,
			   size_t newest_word_count, unsigned int width,
			   unsigned int height, size_t row_stride_words,
			   unsigned int pixel_shift, ms912x_core_u32 pixel_mask,
			   unsigned int stride, unsigned int phase,
			   struct ms912x_core_rect *rects, size_t rect_capacity,
			   size_t *rect_count,
			   struct ms912x_scan_rect_index *index,
			   enum ms912x_scan_backend backend)
{
	size_t required_words, word_width;
	unsigned int count = 0, capacity;
	unsigned int block_words;
	int ret;

	if (!baseline || !newest || !rect_count || !rects || !index ||
	    !rect_capacity || rect_capacity > MS912X_MAX_DAMAGE_RECTS ||
	    !stride || stride > 16 || phase >= stride)
		return -EINVAL;
	ret = scan_word_count(width, height, row_stride_words, pixel_shift,
			      &required_words);
	if (ret)
		return ret;
	if (baseline_word_count < required_words ||
	    newest_word_count < required_words)
		return -EINVAL;
	backend = ms912x_core_scan_resolve_backend(backend);
	pixel_mask = ms912x_core_scan_backend_mask(backend, pixel_mask);
	block_words = ms912x_core_scan_run_words(backend, pixel_shift);
	capacity = (unsigned int)rect_capacity;
	word_width = (size_t)width >> pixel_shift;
	ms912x_scan_rect_index_reset(index, height);
	for (unsigned int y = phase; y < height; y += stride) {
		const ms912x_core_u32 *old_row =
			baseline + (size_t)y * row_stride_words;
		const ms912x_core_u32 *new_row =
			newest + (size_t)y * row_stride_words;
		unsigned int cursor, first, end;
		struct ms912x_scan_row_result row;
		ms912x_core_scan_row(old_row, new_row, word_width, pixel_mask, &row, backend);
		if (row.equal_row)
			continue;
		cursor = row.min_x / block_words * block_words;

		while (ms912x_scan_row_next_run_mask(
			old_row, new_row, word_width, row.max_x, block_words,
			pixel_mask, &cursor, &first, &end)) {
			struct ms912x_core_rect changed = {
				first << pixel_shift, y >= stride - 1 ? y - stride + 1 : 0,
				end << pixel_shift, y + 1
			};

			/* Join before enforcing capacity, exactly as the Linux scanner does. */
			ms912x_scan_rect_add_with_growth(index, &rects, &capacity,
						       &count, &changed, width, height, NULL);
		}
	}
	/* Extend after joining, matching ms912x_damage_scan_extend_bottom(). */
	if (stride > 1)
		for (unsigned int i = 0; i < count; ++i)
			rects[i].y2 = min_t(unsigned int, rects[i].y2 + stride - 1, height);
	*rect_count = count;
	return 0;
}

int ms912x_core_scan_words_backend(const ms912x_core_u32 *baseline,
			   size_t baseline_word_count, const ms912x_core_u32 *newest,
			   size_t newest_word_count, unsigned int width,
			   unsigned int height, size_t row_stride_words,
			   unsigned int pixel_shift, ms912x_core_u32 pixel_mask,
			   struct ms912x_core_rect *rects, size_t rect_capacity,
			   size_t *rect_count, struct ms912x_scan_rect_index *index,
			   enum ms912x_scan_backend backend)
{
	return ms912x_core_scan_sampled_words_backend(baseline, baseline_word_count,
		newest, newest_word_count, width, height, row_stride_words, pixel_shift,
		pixel_mask, 1, 0, rects, rect_capacity, rect_count, index, backend);
}
