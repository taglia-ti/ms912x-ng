/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MS912X_RGBX_INTERNAL_H
#define MS912X_RGBX_INTERNAL_H

#include "ms912x_rgbx_convert.h"
#include "ms912x_color_core.h"

#if defined(__GNUC__) || defined(__clang__)
#define MS912X_COLOR_INLINE static inline __attribute__((always_inline))
#else
#define MS912X_COLOR_INLINE static inline
#endif

/* All coefficients, truncation, and U/Y/V/Y wire bytes match fast_chroma. */
MS912X_COLOR_INLINE void
ms912x_rgbx_pair(uint8_t *dst, const uint8_t *src, size_t step, unsigned int red)
{
	unsigned int blue = 2U - red;
	const uint8_t *second = src + step;
	unsigned int pair = ms912x_core_rgb_pair_to_uyvy(
		src[red], src[1], src[blue],
		second[red], second[1], second[blue]);

	dst[0] = (uint8_t)pair;
	dst[1] = (uint8_t)(pair >> 8);
	dst[2] = (uint8_t)(pair >> 16);
	dst[3] = (uint8_t)(pair >> 24);
}

MS912X_COLOR_INLINE void
ms912x_rgbx_scalar_tail(uint8_t *dst, const uint8_t *src,
			uint32_t width, size_t step, unsigned int red)
{
	uint32_t x = 0;

	for (; x + 4U <= width; x += 4U) {
		ms912x_rgbx_pair(dst + (size_t)x * 2U, src + (size_t)x * step, step, red);
		ms912x_rgbx_pair(dst + (size_t)x * 2U + 4U,
				  src + ((size_t)x + 2U) * step, step, red);
	}
	if (x < width)
		ms912x_rgbx_pair(dst + (size_t)x * 2U, src + (size_t)x * step, step, red);
}

void ms912x_rgbx_row_scalar(uint8_t *, const uint8_t *, uint32_t, size_t);
void ms912x_bgrx_row_scalar(uint8_t *, const uint8_t *, uint32_t, size_t);
#if defined(__SSE2__)
void ms912x_rgbx_row_sse2(uint8_t *, const uint8_t *, uint32_t, size_t);
void ms912x_bgrx_row_sse2(uint8_t *, const uint8_t *, uint32_t, size_t);
#endif
#if defined(__ARM_NEON) || defined(__aarch64__)
void ms912x_rgbx_row_neon(uint8_t *, const uint8_t *, uint32_t, size_t);
void ms912x_bgrx_row_neon(uint8_t *, const uint8_t *, uint32_t, size_t);
#endif

#endif
