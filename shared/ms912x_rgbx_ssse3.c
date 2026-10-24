/* SPDX-License-Identifier: GPL-2.0-only */
#include "ms912x_rgbx_internal.h"

#if defined(__SSE2__)
#include "ms912x_color_ssse3_core.h"

static MS912X_COLOR_SSSE3_TARGET inline __attribute__((always_inline)) void
convert_row(uint8_t *dst, const uint8_t *src, uint32_t width, unsigned int red)
{
	uint32_t x = 0;

	for (; x + 8U <= width; x += 8U)
		ms912x_color_ssse3_eight(dst + (size_t)x * 2U, src + (size_t)x * 4U, red);
	if (x + 4U <= width) {
		ms912x_color_sse2_four(dst + (size_t)x * 2U, src + (size_t)x * 4U, red);
		x += 4U;
	}
	if (x < width)
		ms912x_rgbx_pair(dst + (size_t)x * 2U, src + (size_t)x * 4U, 4, red);
}

MS912X_COLOR_SSSE3_TARGET void
ms912x_rgbx_row_ssse3(uint8_t *dst, const uint8_t *src, uint32_t width, size_t step)
{
	(void)step;
	convert_row(dst, src, width, 0);
}

MS912X_COLOR_SSSE3_TARGET void
ms912x_bgrx_row_ssse3(uint8_t *dst, const uint8_t *src, uint32_t width, size_t step)
{
	(void)step;
	convert_row(dst, src, width, 2);
}
#endif
