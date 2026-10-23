/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MS912X_COLOR_NEON_CORE_H
#define MS912X_COLOR_NEON_CORE_H
#ifdef __KERNEL__
#include <asm/neon-intrinsics.h>
#else
#include <stdbool.h>
#include <arm_neon.h>
#endif

/*
 * Compact packing retains bits 16..31, then discards bit 16. This exactly
 * reproduces >>17 while combining U/V into one output register.
 */
static inline __attribute__((always_inline)) uint8x8_t
ms912x_neon_chroma(uint16x4_t b, uint16x4_t g, uint16x4_t r, bool compact)
{
	uint32x4_t u = vmlal_n_u16(vdupq_n_u32(128 << 17), b, 28672);
	uint32x4_t v = vmlal_n_u16(vdupq_n_u32(128 << 17), r, 28672);
	uint8x8_t uv;

	u = vmlsl_n_u16(u, g, 18996);
	v = vmlsl_n_u16(v, g, 24009);
	u = vmlsl_n_u16(u, r, 9676);
	v = vmlsl_n_u16(v, b, 4663);
	/* ST2's eight-pixel path benefits from halfword interleaving on A53. */
	if (!compact) {
		uint16x4x2_t zipped = vzip_u16(vmovn_u32(vshrq_n_u32(u, 17)),
					      vmovn_u32(vshrq_n_u32(v, 17)));

		return vmovn_u16(vcombine_u16(zipped.val[0], zipped.val[1]));
	}
	uv = vshrn_n_u16(vcombine_u16(vshrn_n_u32(u, 16),
				     vshrn_n_u32(v, 16)), 1);
	return vzip_u8(uv, vext_u8(uv, uv, 4)).val[0];
}

static inline __attribute__((always_inline)) uint32x4_t
ms912x_neon_luma(uint16x4_t b, uint16x4_t g, uint16x4_t r)
{
	uint32x4_t y = vmull_n_u16(b, 6391);

	y = vmlal_n_u16(y, g, 32904);
	return vmlal_n_u16(y, r, 16763);
}

/* Byte deinterleaving also handles little-endian XRGB on big-endian hosts. */
static inline __attribute__((always_inline)) void
ms912x_color_neon_eight(unsigned char *dst, const void *pixels, unsigned int red)
{
	uint8x8x4_t channels = vld4_u8((const unsigned char *)pixels);
	uint16x8_t b = vmovl_u8(channels.val[2U - red]);
	uint16x8_t g = vmovl_u8(channels.val[1]);
	uint16x8_t r = vmovl_u8(channels.val[red]);
	uint32x4_t y_low = ms912x_neon_luma(vget_low_u16(b), vget_low_u16(g),
					  vget_low_u16(r));
	uint32x4_t y_high = ms912x_neon_luma(vget_high_u16(b), vget_high_u16(g),
					   vget_high_u16(r));
	uint8x8x2_t uyvy;

	uyvy.val[0] = ms912x_neon_chroma(vpaddl_u8(channels.val[2U - red]),
					 vpaddl_u8(channels.val[1]),
					 vpaddl_u8(channels.val[red]), false);
	uyvy.val[1] = vmovn_u16(vaddq_u16(
		vcombine_u16(vshrn_n_u32(y_low, 16), vshrn_n_u32(y_high, 16)),
		vdupq_n_u16(16)));
	vst2_u8(dst, uyvy);
}

#if defined(CONFIG_CPU_BIG_ENDIAN) || __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
static inline __attribute__((always_inline)) uint32x4_t ms912x_neon_load_le_pixels(const void *pixels)
{
	return vreinterpretq_u32_u8(vrev32q_u8(vreinterpretq_u8_u32(
		vld1q_u32((const unsigned int *)pixels))));
}
#else
static inline __attribute__((always_inline)) uint32x4_t ms912x_neon_load_le_pixels(const void *pixels)
{
	return vld1q_u32((const unsigned int *)pixels);
}
#endif

/* Four pixels use the same widening MACs as the main eight-pixel loop. */
static inline __attribute__((always_inline)) void
ms912x_color_neon_four(unsigned char *dst, const void *pixels, unsigned int red)
{
	uint32x4_t packed = ms912x_neon_load_le_pixels(pixels);
	uint16x4_t mask = vdup_n_u16(0xff);
	uint16x4_t b, g, r, y;
	uint8x8_t uv, y_bytes;

	b = red == 2 ? vand_u16(vmovn_u32(packed), mask) : vand_u16(vshrn_n_u32(packed, 16), mask);
	g = vand_u16(vshrn_n_u32(packed, 8), mask);
	r = red == 2 ? vand_u16(vshrn_n_u32(packed, 16), mask) : vand_u16(vmovn_u32(packed), mask);
	y = vadd_u16(vshrn_n_u32(ms912x_neon_luma(b, g, r), 16), vdup_n_u16(16));
	uv = ms912x_neon_chroma(vpadd_u16(b, b), vpadd_u16(g, g), vpadd_u16(r, r), true);
	y_bytes = vmovn_u16(vcombine_u16(y, y));
	vst1_u8(dst, vzip_u8(uv, y_bytes).val[0]);
}


#endif
