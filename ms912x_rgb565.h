/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MS912X_RGB565_H
#define MS912X_RGB565_H

#include <linux/unaligned.h>

#include "ms912x_neon.h"

#define MS912X_RGB565_WIRE_MASK 0xf7feU

/* At most seven pixels; branches depend only on length, never pixel content. */
static __always_inline void
ms912x_rgb565_mask_tail(u8 *out, const u16 *src, unsigned int width)
{
	if (width & 4) {
		put_unaligned_le64(get_unaligned_le64(src) &
				   0xf7fef7fef7fef7feULL, out);
		src += 4;
		out += 8;
	}
	if (width & 2) {
		put_unaligned_le32(get_unaligned_le32(src) & 0xf7fef7feU, out);
		src += 2;
		out += 4;
	}
	if (width & 1)
		put_unaligned_le16(get_unaligned_le16(src) &
				   MS912X_RGB565_WIRE_MASK, out);
}

/* Independent source/destination buffers, native packed little-endian pixels. */
static __always_inline void
ms912x_rgb565_mask_row_scalar(void *dst, const u16 *src, unsigned int width)
{
	u8 *out = dst;
	size_t x = 0;

	for (; x + 4 <= width; x += 4)
		put_unaligned_le64(get_unaligned_le64(src + x) &
				   0xf7fef7fef7fef7feULL,
				   out + x * 2);
	ms912x_rgb565_mask_tail(out + x * 2, src + x, width - x);
}

#ifdef CONFIG_X86_64
bool ms912x_rgb565_mask_sse2(void *dst, const u16 *src,
			     const struct drm_rect *rect);
#endif
#if MS912X_HAS_NEON
bool ms912x_rgb565_mask_neon(void *dst, const u16 *src,
			     const struct drm_rect *rect);
#endif

#endif
