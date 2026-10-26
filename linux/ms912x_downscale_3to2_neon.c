// SPDX-License-Identifier: GPL-2.0-only

#include <linux/kernel.h>

#include <asm/neon-intrinsics.h>

#include "ms912x_downscale_3to2.h"

static __always_inline uint16x8_t
ms912x_downscale_neon_sum(uint8x8_t a, uint8x8_t b, uint8x8_t c, uint8x8_t d)
{
	uint16x8_t heavy = vaddq_u16(vshlq_n_u16(vmovl_u8(a), 1), vmovl_u8(b));
	uint16x8_t light = vaddq_u16(vshlq_n_u16(vmovl_u8(c), 1), vmovl_u8(d));

	return vaddq_u16(vshlq_n_u16(heavy, 1), light);
}

static __always_inline uint8x8_t ms912x_downscale_neon_div9(uint16x8_t sum)
{
	/* Rounded Q15 multiply is exact for the area numerator range 0..2295. */
	int16x8_t average = vqrdmulhq_s16(vreinterpretq_s16_u16(sum),
					vdupq_n_s16(3641));

	return vmovn_u16(vreinterpretq_u16_s16(average));
}

static __always_inline void
ms912x_downscale_neon_gather_four(const u32 *row, uint8x16_t *near,
				 uint8x16_t *center)
{
	uint32x2x3_t pixels = vld3_u32(row);
	uint32x2x2_t n = vzip_u32(pixels.val[0], pixels.val[2]);
	uint32x2x2_t c = vzip_u32(pixels.val[1], pixels.val[1]);

	*near = vreinterpretq_u8_u32(vcombine_u32(n.val[0], n.val[1]));
	*center = vreinterpretq_u8_u32(vcombine_u32(c.val[0], c.val[1]));
}

static __always_inline uint32x4_t
ms912x_downscale_neon_four(const u32 *heavy, const u32 *light)
{
	uint8x16_t a, b, c, d;
	uint16x8_t lo, hi;

	ms912x_downscale_neon_gather_four(heavy, &a, &b);
	ms912x_downscale_neon_gather_four(light, &c, &d);
	lo = ms912x_downscale_neon_sum(vget_low_u8(a), vget_low_u8(b),
				      vget_low_u8(c), vget_low_u8(d));
	hi = ms912x_downscale_neon_sum(vget_high_u8(a), vget_high_u8(b),
				      vget_high_u8(c), vget_high_u8(d));
	return vreinterpretq_u32_u8(vcombine_u8(ms912x_downscale_neon_div9(lo),
					      ms912x_downscale_neon_div9(hi)));
}

static __always_inline uint32x2_t
ms912x_downscale_neon_two(const u32 *heavy, const u32 *light)
{
	uint32x2_t a = vdup_n_u32(heavy[0]), b = vdup_n_u32(heavy[1]);
	uint32x2_t c = vdup_n_u32(light[0]), d = vdup_n_u32(light[1]);
	uint16x8_t sum;

	a = vld1_lane_u32(heavy + 2, a, 1);
	c = vld1_lane_u32(light + 2, c, 1);
	sum = ms912x_downscale_neon_sum(vreinterpret_u8_u32(a),
		vreinterpret_u8_u32(b), vreinterpret_u8_u32(c), vreinterpret_u8_u32(d));
	return vreinterpret_u32_u8(ms912x_downscale_neon_div9(sum));
}

noinline void ms912x_downscale_xrgb8888_3to2_neon_rows_fpu(
		const u32 *src, u32 *dst, unsigned int src_width,
		unsigned int src_height, unsigned int dst_width,
		unsigned int src_stride, unsigned int dst_stride,
		unsigned int first_row, unsigned int end_row,
		unsigned int offset_x, unsigned int offset_y)
{
	unsigned int pairs = min(dst_width & ~1U, ((src_width - offset_x) / 3) * 2);
	unsigned int vector_width = pairs & ~3U;
	unsigned int y, x;

	for (y = first_row; y < end_row; y++) {
		unsigned int sy = offset_y + y + y / 2;
		unsigned int phase = y & 1;
		const u32 *heavy = src + min(sy + phase, src_height - 1) * src_stride;
		const u32 *light = src + min(sy + 1 - phase, src_height - 1) * src_stride;
		u32 *out = dst + y * dst_stride;

		for (x = 0; x < vector_width; x += 4)
			vst1q_u32(out + x, ms912x_downscale_neon_four(
				heavy + offset_x + x / 2 * 3, light + offset_x + x / 2 * 3));
		if (x + 2 <= pairs) {
			vst1_u32(out + x, ms912x_downscale_neon_two(
				heavy + offset_x + x / 2 * 3, light + offset_x + x / 2 * 3));
			x += 2;
		}
		for (; x < dst_width; x++)
			out[x] = ms912x_downscale_area_pixel(heavy, light, src_width, x, offset_x);
	}
}
