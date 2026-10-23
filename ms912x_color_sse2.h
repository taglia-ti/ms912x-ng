/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MS912X_COLOR_SSE2_H
#define MS912X_COLOR_SSE2_H
#include "ms912x_color.h"
#include "shared/ms912x_color_sse2_core.h"
static __always_inline void
ms912x_sse2_convert_four(u8 *dst, const __le32 *pixels)
{
    ms912x_color_sse2_four(dst, pixels, 2);
}
#endif
