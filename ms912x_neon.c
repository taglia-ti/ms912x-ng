// SPDX-License-Identifier: GPL-2.0-only

#include <asm/neon.h>
#include <asm/simd.h>

#include <linux/kernel.h>
#include <linux/unaligned.h>

#include <arm_neon.h>

#include "ms912x.h"

#define MS912X_NEON_ROWS_PER_FPU 16

static __always_inline u32 ms912x_neon_scalar_pair(const __le32 *pixels)
{
	unsigned int pixel0 = le32_to_cpup(&pixels[0]);
	unsigned int pixel1 = le32_to_cpup(&pixels[1]);
	unsigned int r0 = (pixel0 >> 16) & 0xff;
	unsigned int g0 = (pixel0 >> 8) & 0xff;
	unsigned int b0 = pixel0 & 0xff;
	unsigned int r1 = (pixel1 >> 16) & 0xff;
	unsigned int g1 = (pixel1 >> 8) & 0xff;
	unsigned int b1 = pixel1 & 0xff;
	unsigned int y0 = ((16 << 16) + 16763 * r0 + 32904 * g0 +
			    6391 * b0) >> 16;
	unsigned int y1 = ((16 << 16) + 16763 * r1 + 32904 * g1 +
			    6391 * b1) >> 16;
	unsigned int r_sum = r0 + r1;
	unsigned int g_sum = g0 + g1;
	unsigned int b_sum = b0 + b1;
	unsigned int u = ((128 << 17) - 9676 * r_sum -
			   18996 * g_sum + 28672 * b_sum) >> 17;
	unsigned int v = ((128 << 17) + 28672 * r_sum -
			   24009 * g_sum - 4663 * b_sum) >> 17;

	return (u & 0xff) | ((y0 & 0xff) << 8) |
		((v & 0xff) << 16) | ((y1 & 0xff) << 24);
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
	u32 y_values[4], u_values[4], v_values[4];
	u32 pair0, pair1;

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

	vst1q_u32(y_values, y);
	vst1q_u32(u_values, u);
	vst1q_u32(v_values, v);
	pair0 = (u_values[0] & 0xff) | ((y_values[0] & 0xff) << 8) |
		((v_values[0] & 0xff) << 16) |
		((y_values[1] & 0xff) << 24);
	pair1 = (u_values[1] & 0xff) | ((y_values[2] & 0xff) << 8) |
		((v_values[1] & 0xff) << 16) |
		((y_values[3] & 0xff) << 24);
	put_unaligned_le64(pair0 | ((u64)pair1 << 32), dst);
}

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

		for (; x + 4 <= width; x += 4)
			ms912x_neon_convert_four(line_dst + x * 2, pixels + x);
		if (x < width) {
			u32 pair = ms912x_neon_scalar_pair(pixels + x);

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
