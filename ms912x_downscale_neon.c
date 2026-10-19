// SPDX-License-Identifier: GPL-2.0-only

#include <linux/kernel.h>

#include <asm/neon-intrinsics.h>

#include "ms912x_downscale.h"

/* Correct the double rounding of three VRHADDs, independently per byte. */
static __always_inline uint8x16_t
ms912x_downscale_neon_avg4(uint8x16_t a, uint8x16_t b, uint8x16_t c, uint8x16_t d)
{
	uint8x16_t ab = vrhaddq_u8(a, b);
	uint8x16_t cd = vrhaddq_u8(c, d);
	uint8x16_t parity = vorrq_u8(veorq_u8(a, b), veorq_u8(c, d));
	uint8x16_t correction = vandq_u8(parity, veorq_u8(ab, cd));

	correction = vandq_u8(correction, vdupq_n_u8(1));
	return vsubq_u8(vrhaddq_u8(ab, cd), correction);
}

static __always_inline uint8x8_t
ms912x_downscale_neon_avg4_two(uint8x8_t a, uint8x8_t b, uint8x8_t c, uint8x8_t d)
{
	uint8x8_t ab = vrhadd_u8(a, b);
	uint8x8_t cd = vrhadd_u8(c, d);
	uint8x8_t parity = vorr_u8(veor_u8(a, b), veor_u8(c, d));
	uint8x8_t correction = vand_u8(parity, veor_u8(ab, cd));

	correction = vand_u8(correction, vdup_n_u8(1));
	return vsub_u8(vrhadd_u8(ab, cd), correction);
}

noinline void ms912x_downscale_xrgb8888_2x_neon_rows_fpu(
		const u32 *src, u32 *dst, unsigned int src_width,
		unsigned int src_height, unsigned int dst_width,
		unsigned int src_stride, unsigned int dst_stride,
		unsigned int first_row, unsigned int end_row)
{
	unsigned int pairs = min(dst_width, src_width / 2);
	unsigned int vector_width = pairs & ~3U;
	unsigned int y, x;

	for (y = first_row; y < end_row; y++) {
		unsigned int sy0 = min(2 * y, src_height - 1);
		unsigned int sy1 = min(2 * y + 1, src_height - 1);
		const u32 *row0 = src + sy0 * src_stride;
		const u32 *row1 = src + sy1 * src_stride;
		u32 *out = dst + y * dst_stride;

		for (x = 0; x < vector_width; x += 4) {
			uint32x4x2_t a = vld2q_u32(row0 + 2 * x);
			uint32x4x2_t b = vld2q_u32(row1 + 2 * x);
			uint8x16_t average = ms912x_downscale_neon_avg4(
				vreinterpretq_u8_u32(a.val[0]), vreinterpretq_u8_u32(b.val[0]),
				vreinterpretq_u8_u32(a.val[1]), vreinterpretq_u8_u32(b.val[1]));

			vst1q_u32(out + x, vreinterpretq_u32_u8(average));
		}
		if (x + 2 <= pairs) {
			uint32x2x2_t a = vld2_u32(row0 + 2 * x);
			uint32x2x2_t b = vld2_u32(row1 + 2 * x);
			uint8x8_t average = ms912x_downscale_neon_avg4_two(
				vreinterpret_u8_u32(a.val[0]), vreinterpret_u8_u32(b.val[0]),
				vreinterpret_u8_u32(a.val[1]), vreinterpret_u8_u32(b.val[1]));

			vst1_u32(out + x, vreinterpret_u32_u8(average));
			x += 2;
		}
		for (; x < dst_width; x++) {
			unsigned int sx0 = min(2 * x, src_width - 1);
			unsigned int sx1 = min(2 * x + 1, src_width - 1);

			out[x] = ms912x_downscale_avg4(row0[sx0], row0[sx1], row1[sx0], row1[sx1]);
		}
	}
}
