// SPDX-License-Identifier: GPL-2.0-only

#include <asm/byteorder.h>

#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/minmax.h>
#include <linux/unaligned.h>

#include <drm/drm_rect.h>

#ifdef CONFIG_X86_64
#include <asm/cpufeature.h>
#include <asm/simd.h>
#elif defined(CONFIG_ARM64)
#include <asm/simd.h>
#endif

#include "ms912x.h"

static inline unsigned int ms912x_rgb_to_y(unsigned int r, unsigned int g,
					   unsigned int b)
{
	const unsigned int luma = (16 << 16) + 16763 * r +
			   32904 * g + 6391 * b;

	return luma >> 16;
}

static inline unsigned int ms912x_rgb_pair_to_u(unsigned int r_sum,
						unsigned int g_sum,
						unsigned int b_sum)
{
	const unsigned int u = (128 << 17) - 9676 * r_sum -
			       18996 * g_sum + 28672 * b_sum;

	return u >> 17;
}

static inline unsigned int ms912x_rgb_pair_to_v(unsigned int r_sum,
						unsigned int g_sum,
						unsigned int b_sum)
{
	const unsigned int v = (128 << 17) + 28672 * r_sum -
			       24009 * g_sum - 4663 * b_sum;

	return v >> 17;
}

static __always_inline u32 ms912x_xrgb_pair_to_uyvy(const __le32 *pixels)
{
	unsigned int pixel1 = le32_to_cpup(&pixels[0]);
	unsigned int pixel2 = le32_to_cpup(&pixels[1]);
	unsigned int r1, g1, b1, r2, g2, b2;
	unsigned int v, y1, u, y2;
	unsigned int r_sum, g_sum, b_sum;

	r1 = (pixel1 >> 16) & 0xff;
	g1 = (pixel1 >> 8) & 0xff;
	b1 = pixel1 & 0xff;
	r2 = (pixel2 >> 16) & 0xff;
	g2 = (pixel2 >> 8) & 0xff;
	b2 = pixel2 & 0xff;

	r_sum = r1 + r2;
	g_sum = g1 + g2;
	b_sum = b1 + b2;
	y1 = ms912x_rgb_to_y(r1, g1, b1);
	y2 = ms912x_rgb_to_y(r2, g2, b2);
	u = ms912x_rgb_pair_to_u(r_sum, g_sum, b_sum);
	v = ms912x_rgb_pair_to_v(r_sum, g_sum, b_sum);

	return (u & 0xff) | ((y1 & 0xff) << 8) |
		((v & 0xff) << 16) | ((y2 & 0xff) << 24);
}

static void ms912x_xrgb_to_uyvy_line(u8 *transfer_buffer,
				       const __le32 *xrgb_line,
				       size_t x, size_t width, bool profile,
				       u64 *color_math_ns)
{
	u8 *dst = transfer_buffer;
	unsigned int i = 0;
	u64 start_ns = profile ? ktime_get_ns() : 0;

	for (; i + 4 <= width; i += 4) {
		u32 pair0 = ms912x_xrgb_pair_to_uyvy(&xrgb_line[x + i]);
		u32 pair1 = ms912x_xrgb_pair_to_uyvy(&xrgb_line[x + i + 2]);

		put_unaligned_le64(pair0 | ((u64)pair1 << 32), dst);
		dst += sizeof(u64);
	}
	if (i < width) {
		u32 pair = ms912x_xrgb_pair_to_uyvy(&xrgb_line[x + i]);

		put_unaligned_le32(pair, dst);
	}
	if (profile)
		*color_math_ns += ktime_get_ns() - start_ns;
}

static const u8 ms912x_end_of_buffer[] = { 0xff, 0xc0, 0x00, 0x00,
				    0x00, 0x00, 0x00, 0x00 };

int ms912x_convert_xrgb8888_to_uyvy(void *dst,
					     const __le32 *src,
					     struct ms912x_device *ms912x,
					     unsigned int fb_width,
					     unsigned int fb_height,
					     const struct drm_rect *rect)
{
	struct ms912x_frame_update_header *header = dst;
	u32 position, dimensions;
	u64 color_math_ns = 0, simd_start_ns;
	int i, x, y1, y2, width;
	bool profile = ms912x_stats_enabled();
	unsigned int fast_chroma = READ_ONCE(ms912x_parameters.fast_chroma);
	bool used_simd = false;

	if (rect->x1 < 0 || rect->y1 < 0 || rect->x2 > fb_width ||
	    rect->y2 > fb_height || rect->x1 >= rect->x2 ||
	    rect->y1 >= rect->y2 || (rect->x1 & 1) ||
	    (drm_rect_width(rect) & 1))
		return -EINVAL;

	y1 = rect->y1;
	y2 = min_t(unsigned int, rect->y2, fb_height);
	x = rect->x1;
	width = drm_rect_width(rect);

	header->marker = cpu_to_be16(0xff00);
	position = ((x & 0xfff) << 12) | (y1 & 0xfff);
	dimensions = ((width & 0xfff) << 12) |
		     (drm_rect_height(rect) & 0xfff);
	put_unaligned_be24(position, header->position);
	put_unaligned_be24(dimensions, header->dimensions);
	dst += sizeof(*header);

	if (fast_chroma == 2) {
#ifdef CONFIG_X86_64
		/* AVX is exposed only when the OS has enabled the YMM state. */
		if (may_use_simd() && boot_cpu_has(X86_FEATURE_AVX) &&
		    boot_cpu_has(X86_FEATURE_AVX2)) {
			if (profile)
				simd_start_ns = ktime_get_ns();
			ms912x_convert_xrgb8888_to_uyvy_avx2(dst, src, rect);
			used_simd = true;
			dst += width * 2 * drm_rect_height(rect);
			if (profile)
				color_math_ns += ktime_get_ns() - simd_start_ns;
		}
#endif
	}

	if (!used_simd && fast_chroma >= 1) {
		if (profile)
			simd_start_ns = ktime_get_ns();
#ifdef CONFIG_ARM64
		used_simd = ms912x_convert_xrgb8888_to_uyvy_neon(dst, src, rect);
#else
		used_simd = ms912x_convert_xrgb8888_to_uyvy_sse2(dst, src, rect);
#endif
		if (used_simd) {
			if (profile)
				color_math_ns += ktime_get_ns() - simd_start_ns;
			dst += width * 2 * drm_rect_height(rect);
		}
	}

	if (!used_simd) {
		for (i = y1; i < y2; i++) {
			const __le32 *xrgb_line = src + i * MS912X_MAX_WIDTH;

			ms912x_xrgb_to_uyvy_line(dst, xrgb_line, x, width,
						   profile, &color_math_ns);
			dst += width * 2;
		}
	}

	memcpy(dst, ms912x_end_of_buffer, sizeof(ms912x_end_of_buffer));
	if (profile) {
		atomic64_inc(&ms912x->stats.conversion_profile_frames);
		atomic64_add(color_math_ns,
			     &ms912x->stats.conversion_color_ns);
	}
	return 0;
}
