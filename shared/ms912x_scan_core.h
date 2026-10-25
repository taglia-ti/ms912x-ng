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
#include "ms912x_scan_backend_types.h"
#define ms912x_core_rect drm_rect

struct ms912x_scan_rect_index;
/* Rotate the sampled row phase between snapshots, using Linux's vertical margins. */
int ms912x_core_scan_sampled_words_backend(const ms912x_core_u32 *baseline,
                           size_t baseline_word_count,
                           const ms912x_core_u32 *newest,
                           size_t newest_word_count,
                           unsigned int width, unsigned int height,
                           size_t row_stride_words, unsigned int pixel_shift,
                           ms912x_core_u32 pixel_mask, unsigned int stride,
                           unsigned int phase, struct ms912x_core_rect *rects,
                           size_t rect_capacity, size_t *rect_count,
                           struct ms912x_scan_rect_index *index,
                           enum ms912x_scan_backend backend);
/* Linux run geometry and indexed unions, with bounding fallback on exhaustion.
 * The caller owns the index and fixed rectangle storage (1..8192 entries).
 */
/* Explicit backend selection uses the same mask and run geometry as Linux. */
int ms912x_core_scan_words_backend(const ms912x_core_u32 *baseline,
			   size_t baseline_word_count,
			   const ms912x_core_u32 *newest,
			   size_t newest_word_count,
			   unsigned int width, unsigned int height,
			   size_t row_stride_words, unsigned int pixel_shift,
			   ms912x_core_u32 pixel_mask,
			   struct ms912x_core_rect *rects,
			   size_t rect_capacity, size_t *rect_count, struct ms912x_scan_rect_index *index,
			   enum ms912x_scan_backend backend);

#endif /* MS912X_SCAN_CORE_H */
