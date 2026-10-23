/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MS912X_SCAN_CORE_H
#define MS912X_SCAN_CORE_H

#ifdef __KERNEL__
#include <linux/types.h>
typedef u32 ms912x_core_u32;
typedef s32 ms912x_core_s32;
#else
#include <stddef.h>
#include <stdint.h>
typedef uint32_t ms912x_core_u32;
typedef int32_t ms912x_core_s32;
#endif

#include "../ms912x_platform.h"
#define ms912x_core_rect drm_rect

/* Return exact changed runs using the Linux run detector; no writes on ENOSPC. */
int ms912x_core_scan_words(const ms912x_core_u32 *baseline,
			   size_t baseline_word_count,
			   const ms912x_core_u32 *newest,
			   size_t newest_word_count,
			   unsigned int width, unsigned int height,
			   size_t row_stride_words, unsigned int pixel_shift,
			   ms912x_core_u32 pixel_mask,
			   struct ms912x_core_rect *rects,
			   size_t rect_capacity, size_t *rect_count);

static inline int
ms912x_core_word_changed(ms912x_core_u32 old_word,
			 ms912x_core_u32 new_word,
			 ms912x_core_u32 pixel_mask)
{
	return !!((old_word ^ new_word) & pixel_mask);
}

#endif /* MS912X_SCAN_CORE_H */
