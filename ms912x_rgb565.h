/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MS912X_RGB565_H
#define MS912X_RGB565_H

#include <linux/unaligned.h>

#define MS912X_RGB565_WIRE_MASK 0xf7feU

/* Independent source/destination buffers, native packed little-endian pixels. */
static __always_inline void ms912x_rgb565_mask_row(void *dst, const u16 *src,
						   unsigned int width)
{
	u8 *out = dst;
	unsigned int x = 0;

	for (; x + 4 <= width; x += 4)
		put_unaligned_le64(get_unaligned_le64(src + x) &
					   0xf7fef7fef7fef7feULL,
				   out + x * 2);
	if (x + 2 <= width) {
		put_unaligned_le32(get_unaligned_le32(src + x) & 0xf7fef7feU,
				   out + x * 2);
		x += 2;
	}
	if (x < width)
		put_unaligned_le16(get_unaligned_le16(src + x) &
					   MS912X_RGB565_WIRE_MASK,
				   out + x * 2);
}

#endif
