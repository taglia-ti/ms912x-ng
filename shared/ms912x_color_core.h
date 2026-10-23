/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MS912X_COLOR_CORE_H
#define MS912X_COLOR_CORE_H

/* Return two RGB pixels as four little-endian UYVY bytes in a u32. */
static inline unsigned int
ms912x_core_rgb_pair_to_uyvy(unsigned int r0, unsigned int g0,
			     unsigned int b0, unsigned int r1,
			     unsigned int g1, unsigned int b1)
{
	unsigned int u = ((128U << 17) - 9676U * (r0 + r1) -
			  18996U * (g0 + g1) + 28672U * (b0 + b1)) >> 17;
	unsigned int v = ((128U << 17) + 28672U * (r0 + r1) -
			  24009U * (g0 + g1) - 4663U * (b0 + b1)) >> 17;
	unsigned int y0 = ((16U << 16) + 16763U * r0 + 32904U * g0 +
			  6391U * b0) >> 16;
	unsigned int y1 = ((16U << 16) + 16763U * r1 + 32904U * g1 +
			  6391U * b1) >> 16;

	return u | (y0 << 8) | (v << 16) | (y1 << 24);
}

#endif /* MS912X_COLOR_CORE_H */
