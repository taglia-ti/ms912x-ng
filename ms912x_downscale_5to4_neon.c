// SPDX-License-Identifier: GPL-2.0-only
#include <asm/neon-intrinsics.h>

#include "ms912x_downscale_5to4.h"

static __always_inline uint8x8_t ms912x_5to4_neon_average(uint8x8_t a, uint8x8_t b,
		uint8x8_t c, uint8x8_t d, uint16x8_t wx, unsigned int wy)
{
	uint16x8_t other = vsubq_u16(vdupq_n_u16(5), wx);
	uint16x8_t top = vmlaq_u16(vmulq_u16(vmovl_u8(a), wx), vmovl_u8(b), other);
	uint16x8_t bottom = vmlaq_u16(vmulq_u16(vmovl_u8(c), wx), vmovl_u8(d), other);
	uint16x8_t sum = vmlaq_n_u16(vmulq_n_u16(top, wy), bottom, 5 - wy);
	int16x8_t quotient;

	/* floor((sum + 12) * 5243 / 2^17) is exact over the full 0..6375 range. */
	quotient = vqdmulhq_s16(vreinterpretq_s16_u16(vaddq_u16(sum, vdupq_n_u16(12))),
				vdupq_n_s16(5243));
	return vmovn_u16(vshrq_n_u16(vreinterpretq_u16_s16(quotient), 2));
}

static __always_inline uint32x4_t ms912x_5to4_neon_four(const u32 *row0,
						       const u32 *row1, unsigned int wy)
{
	uint8x16_t a = vreinterpretq_u8_u32(vld1q_u32(row0));
	uint8x16_t b = vreinterpretq_u8_u32(vld1q_u32(row0 + 1));
	uint8x16_t c = vreinterpretq_u8_u32(vld1q_u32(row1));
	uint8x16_t d = vreinterpretq_u8_u32(vld1q_u32(row1 + 1));
	uint16x8_t lo_w = vcombine_u16(vdup_n_u16(4), vdup_n_u16(3));
	uint16x8_t hi_w = vcombine_u16(vdup_n_u16(2), vdup_n_u16(1));
	uint8x8_t lo = ms912x_5to4_neon_average(vget_low_u8(a), vget_low_u8(b),
					vget_low_u8(c), vget_low_u8(d), lo_w, wy);
	uint8x8_t hi = ms912x_5to4_neon_average(vget_high_u8(a), vget_high_u8(b),
					vget_high_u8(c), vget_high_u8(d), hi_w, wy);

	return vreinterpretq_u32_u8(vcombine_u8(lo, hi));
}

noinline void ms912x_downscale_xrgb8888_5to4_neon_rows_fpu(const u32 *src, u32 *dst,
		unsigned int sw, unsigned int sh, unsigned int dw,
		unsigned int ss, unsigned int ds, unsigned int first, unsigned int end,
		unsigned int ox, unsigned int oy)
{
	unsigned int full = min(dw & ~3U, (sw - ox) / 5 * 4);
	unsigned int x, y;

	for (y = first; y < end; y++) {
		unsigned int sy = oy + y + y / 4, phase = y & 3;
		unsigned int edge_x = DIV_ROUND_UP((sw - ox - 1) * 4, 5);
		const u32 *row0 = src + min(sy, sh - 1) * ss;
		const u32 *row1 = src + min(sy + 1, sh - 1) * ss;
		u32 *out = dst + y * ds;
		u32 edge = ms912x_5to4_area_pixel(row0, row1, sw, edge_x, ox, phase);

		for (x = 0; x < full; x += 4)
			vst1q_u32(out + x, ms912x_5to4_neon_four(
				row0 + ox + x / 4 * 5, row1 + ox + x / 4 * 5, 4 - phase));
		for (; x < min(dw, edge_x); x++)
			out[x] = ms912x_5to4_area_pixel(row0, row1, sw, x, ox, phase);
		for (; x < dw; x++)
			out[x] = edge;
	}
}
