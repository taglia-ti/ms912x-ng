/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MS912X_NEON_H
#define MS912X_NEON_H

#include <linux/types.h>

#if defined(CONFIG_ARM64) || (defined(CONFIG_ARM) && defined(CONFIG_KERNEL_MODE_NEON))
#define MS912X_HAS_NEON 1
#else
#define MS912X_HAS_NEON 0
#endif

struct drm_rect;
struct ms912x_scan_row_result;

/* Called only by the integer-only glue inside a kernel FPU section. */
void ms912x_neon_convert_rows_bgrx(void *dst, const __le32 *src,
		const struct drm_rect *rect, unsigned int first_row,
		unsigned int end_row, unsigned int dst_stride);
void ms912x_neon_convert_rows_rgbx(void *dst, const __le32 *src,
		const struct drm_rect *rect, unsigned int first_row,
		unsigned int end_row, unsigned int dst_stride);
void ms912x_scan_rows_neon_relaxed_fpu(const u32 *old, const u32 *new, u32 *next,
				       unsigned int width, unsigned int row_stride,
					unsigned int row_count, u32 pixel_mask,
				       struct ms912x_scan_row_result *results);
#endif
