/* SPDX-License-Identifier: GPL-2.0-only */
#include "ms912x_rgbx_internal.h"

#if defined(__ARM_NEON) || defined(__aarch64__)
#include <arm_neon.h>

MS912X_COLOR_INLINE uint32x4_t luma(uint16x4_t r, uint16x4_t g, uint16x4_t b)
{
	uint32x4_t y = vmull_n_u16(b, 6391);

	y = vmlal_n_u16(y, g, 32904);
	return vmlal_n_u16(y, r, 16763);
}

MS912X_COLOR_INLINE uint8x8_t chroma(uint16x4_t r, uint16x4_t g, uint16x4_t b)
{
	uint32x4_t u = vmlal_n_u16(vdupq_n_u32(128U << 17), b, 28672);
	uint32x4_t v = vmlal_n_u16(vdupq_n_u32(128U << 17), r, 28672);
	uint16x4x2_t uv;

	u = vmlsl_n_u16(vmlsl_n_u16(u, g, 18996), r, 9676);
	v = vmlsl_n_u16(vmlsl_n_u16(v, g, 24009), b, 4663);
	uv = vzip_u16(vmovn_u32(vshrq_n_u32(u, 17)), vmovn_u32(vshrq_n_u32(v, 17)));
	return vmovn_u16(vcombine_u16(uv.val[0], uv.val[1]));
}

/* LD4 consumes Android byte order directly; no intermediate channel-swap buffer. */
MS912X_COLOR_INLINE void
convert_eight(uint8_t *dst, const uint8_t *src, unsigned int red)
{
	uint8x8x4_t channels = vld4_u8(src);
	uint16x8_t r = vmovl_u8(channels.val[red]);
	uint16x8_t g = vmovl_u8(channels.val[1]);
	uint16x8_t b = vmovl_u8(channels.val[2U - red]);
	uint32x4_t low = luma(vget_low_u16(r), vget_low_u16(g), vget_low_u16(b));
	uint32x4_t high = luma(vget_high_u16(r), vget_high_u16(g), vget_high_u16(b));
	uint8x8x2_t output;

	output.val[0] = chroma(vpaddl_u8(channels.val[red]),
			       vpaddl_u8(channels.val[1]),
			       vpaddl_u8(channels.val[2U - red]));
	output.val[1] = vmovn_u16(vaddq_u16(
		vcombine_u16(vshrn_n_u32(low, 16), vshrn_n_u32(high, 16)),
		vdupq_n_u16(16)));
	vst2_u8(dst, output);
}

MS912X_COLOR_INLINE void
convert_four(uint8_t *dst, const uint8_t *src, unsigned int red)
{
	uint32x4_t packed = vreinterpretq_u32_u8(vld1q_u8(src));
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
	packed = vreinterpretq_u32_u8(vrev32q_u8(vreinterpretq_u8_u32(packed)));
#endif
	uint16x4_t mask = vdup_n_u16(255);
	uint16x4_t first = vand_u16(vmovn_u32(packed), mask);
	uint16x4_t g = vand_u16(vshrn_n_u32(packed, 8), mask);
	uint16x4_t third = vand_u16(vshrn_n_u32(packed, 16), mask);
	uint16x4_t r = red == 0 ? first : third;
	uint16x4_t b = red == 0 ? third : first;
	uint16x4_t y = vadd_u16(vshrn_n_u32(luma(r, g, b), 16), vdup_n_u16(16));
	uint8x8_t uv = chroma(vpadd_u16(r, r), vpadd_u16(g, g), vpadd_u16(b, b));
	uint8x8_t y_bytes = vmovn_u16(vcombine_u16(y, y));

	vst1_u8(dst, vzip_u8(uv, y_bytes).val[0]);
}

MS912X_COLOR_INLINE void
convert_row(uint8_t *dst, const uint8_t *src, uint32_t width, unsigned int red)
{
	uint32_t x = 0;

	for (; x + 8U <= width; x += 8U)
		convert_eight(dst + (size_t)x * 2U, src + (size_t)x * 4U, red);
	if (x + 4U <= width) {
		convert_four(dst + (size_t)x * 2U, src + (size_t)x * 4U, red);
		x += 4U;
	}
	if (x < width)
		ms912x_rgbx_pair(dst + (size_t)x * 2U, src + (size_t)x * 4U, 4, red);
}

void ms912x_rgbx_row_neon(uint8_t *dst, const uint8_t *src, uint32_t width, size_t step)
{
	(void)step;
	convert_row(dst, src, width, 0);
}

void ms912x_bgrx_row_neon(uint8_t *dst, const uint8_t *src, uint32_t width, size_t step)
{
	(void)step;
	convert_row(dst, src, width, 2);
}
#endif
