// SPDX-License-Identifier: GPL-2.0-only

#include <asm/neon.h>
#include <asm/simd.h>

#include <linux/kernel.h>
#include <linux/unaligned.h>

#include <arm_neon.h>

#include "ms912x.h"
#include "ms912x_color.h"

#define MS912X_NEON_ROWS_PER_FPU 16

/* Byte deinterleaving also handles little-endian XRGB on big-endian hosts. */
static __always_inline void
ms912x_neon_convert_eight(u8 *dst, const __le32 *pixels)
{
	uint8x8x4_t channels = vld4_u8((const u8 *)pixels);
	uint16x8_t b = vmovl_u8(channels.val[0]);
	uint16x8_t g = vmovl_u8(channels.val[1]);
	uint16x8_t r = vmovl_u8(channels.val[2]);
	uint16x4_t b_sum = vpaddl_u8(channels.val[0]);
	uint16x4_t g_sum = vpaddl_u8(channels.val[1]);
	uint16x4_t r_sum = vpaddl_u8(channels.val[2]);
	uint32x4_t y_low, y_high, u, v;
	uint16x4x2_t uv;
	uint8x8x2_t uyvy;

	y_low = vmull_n_u16(vget_low_u16(b), 6391);
	y_low = vmlal_n_u16(y_low, vget_low_u16(g), 32904);
	y_low = vmlal_n_u16(y_low, vget_low_u16(r), 16763);
	y_high = vmull_n_u16(vget_high_u16(b), 6391);
	y_high = vmlal_n_u16(y_high, vget_high_u16(g), 32904);
	y_high = vmlal_n_u16(y_high, vget_high_u16(r), 16763);

	u = vmlal_n_u16(vdupq_n_u32(128 << 17), b_sum, 28672);
	u = vmlsl_n_u16(u, g_sum, 18996);
	u = vmlsl_n_u16(u, r_sum, 9676);
	v = vmlal_n_u16(vdupq_n_u32(128 << 17), r_sum, 28672);
	v = vmlsl_n_u16(v, g_sum, 24009);
	v = vmlsl_n_u16(v, b_sum, 4663);

	uv = vzip_u16(vmovn_u32(vshrq_n_u32(u, 17)),
		      vmovn_u32(vshrq_n_u32(v, 17)));
	uyvy.val[0] = vmovn_u16(vcombine_u16(uv.val[0], uv.val[1]));
	uyvy.val[1] = vmovn_u16(vaddq_u16(
		vcombine_u16(vshrn_n_u32(y_low, 16),
			     vshrn_n_u32(y_high, 16)), vdupq_n_u16(16)));
	/* ST2 interleaves UV and Y bytes directly into the 16-byte payload. */
	vst2_u8(dst, uyvy);
}

static __always_inline void ms912x_neon_convert_four(u8 *dst,
						     const __le32 *pixels)
{
	uint32x4_t packed = vld1q_u32((const u32 *)pixels);
	uint32x4_t mask = vdupq_n_u32(0xff);
	uint32x4_t r = vandq_u32(vshrq_n_u32(packed, 16), mask);
	uint32x4_t g = vandq_u32(vshrq_n_u32(packed, 8), mask);
	uint32x4_t b = vandq_u32(packed, mask);
	uint32x4_t y = vdupq_n_u32(16 << 16);
	uint32x4_t r_sum, g_sum, b_sum, u, v;
	uint16x4_t uv;
	uint8x8_t uv_bytes, y_bytes;

#ifdef CONFIG_CPU_BIG_ENDIAN
	/* The framebuffer words are little-endian regardless of host byte order. */
	packed = vreinterpretq_u32_u8(vrev32q_u8(vreinterpretq_u8_u32(packed)));
	r = vandq_u32(vshrq_n_u32(packed, 16), mask);
	g = vandq_u32(vshrq_n_u32(packed, 8), mask);
	b = vandq_u32(packed, mask);
#endif

	y = vmlaq_n_u32(y, r, 16763);
	y = vmlaq_n_u32(y, g, 32904);
	y = vmlaq_n_u32(y, b, 6391);
	y = vshrq_n_u32(y, 16);

	r_sum = vpaddq_u32(r, r);
	g_sum = vpaddq_u32(g, g);
	b_sum = vpaddq_u32(b, b);
	u = vdupq_n_u32(128 << 17);
	u = vmlsq_n_u32(u, r_sum, 9676);
	u = vmlsq_n_u32(u, g_sum, 18996);
	u = vmlaq_n_u32(u, b_sum, 28672);
	u = vshrq_n_u32(u, 17);
	v = vdupq_n_u32(128 << 17);
	v = vmlaq_n_u32(v, r_sum, 28672);
	v = vmlsq_n_u32(v, g_sum, 24009);
	v = vmlsq_n_u32(v, b_sum, 4663);
	v = vshrq_n_u32(v, 17);

	uv = vzip_u16(vmovn_u32(u), vmovn_u32(v)).val[0];
	uv_bytes = vmovn_u16(vcombine_u16(uv, vdup_n_u16(0)));
	y_bytes = vmovn_u16(vcombine_u16(vmovn_u32(y), vdup_n_u16(0)));
	vst1_u8(dst, vzip_u8(uv_bytes, y_bytes).val[0]);
}

/* Keep compiler-generated SIMD inside the FPU-protected batch call. */
static noinline void
ms912x_neon_convert_rows(void *dst, const __le32 *src,
			 const struct drm_rect *rect,
			 unsigned int first_row, unsigned int end_row)
{
	unsigned int width = drm_rect_width(rect);
	unsigned int row;

	for (row = first_row; row < end_row; row++) {
		const __le32 *pixels = src + row * MS912X_MAX_WIDTH + rect->x1;
		u8 *line_dst = dst + (row - rect->y1) * width * 2;
		unsigned int x = 0;

		for (; x + 8 <= width; x += 8)
			ms912x_neon_convert_eight(line_dst + x * 2, pixels + x);
		if (x + 4 <= width) {
			ms912x_neon_convert_four(line_dst + x * 2, pixels + x);
			x += 4;
		}
		if (x < width) {
			u32 pair = ms912x_xrgb_pair_to_uyvy(pixels + x);

			put_unaligned_le32(pair, line_dst + x * 2);
		}
	}
}

bool ms912x_convert_xrgb8888_to_uyvy_neon(void *dst, const __le32 *src,
					    const struct drm_rect *rect)
{
	unsigned int row = rect->y1;
	unsigned int chunk_end;
	unsigned int end = rect->y2;

	if (!may_use_simd())
		return false;

	while (row < end) {
		chunk_end = min(row + MS912X_NEON_ROWS_PER_FPU, end);
		kernel_neon_begin();
		ms912x_neon_convert_rows(dst, src, rect, row, chunk_end);
		kernel_neon_end();
		row = chunk_end;
	}

	return true;
}
