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
#include "ms912x_color.h"

const char *ms912x_fast_chroma_selected_backend_name(void)
{
	enum ms912x_fast_chroma_backend mode =
		READ_ONCE(ms912x_parameters.fast_chroma);

	if (mode == MS912X_FAST_CHROMA_AUTO) {
#ifdef CONFIG_X86_64
		if (may_use_simd() && boot_cpu_has(X86_FEATURE_AVX) &&
		    boot_cpu_has(X86_FEATURE_AVX2))
			mode = MS912X_FAST_CHROMA_AVX2;
		else
			mode = MS912X_FAST_CHROMA_SSE2;
#elif defined(CONFIG_ARM64)
		mode = MS912X_FAST_CHROMA_NEON;
#else
		mode = MS912X_FAST_CHROMA_SCALAR;
#endif
	}

	if (mode == MS912X_FAST_CHROMA_AVX2) {
#ifdef CONFIG_X86_64
		if (!may_use_simd() || !boot_cpu_has(X86_FEATURE_AVX) ||
		    !boot_cpu_has(X86_FEATURE_AVX2))
			mode = MS912X_FAST_CHROMA_SCALAR;
#else
		mode = MS912X_FAST_CHROMA_SCALAR;
#endif
	} else if (mode == MS912X_FAST_CHROMA_SSE2) {
#ifndef CONFIG_X86_64
		mode = MS912X_FAST_CHROMA_SCALAR;
#else
		if (!may_use_simd())
			mode = MS912X_FAST_CHROMA_SCALAR;
#endif
	} else if (mode == MS912X_FAST_CHROMA_NEON) {
#ifndef CONFIG_ARM64
		mode = MS912X_FAST_CHROMA_SCALAR;
#else
		if (!may_use_simd())
			mode = MS912X_FAST_CHROMA_SCALAR;
#endif
	}

	switch (mode) {
	case MS912X_FAST_CHROMA_AVX2:
		return "avx2";
	case MS912X_FAST_CHROMA_NEON:
		return "neon";
	case MS912X_FAST_CHROMA_SSE2:
		return "sse2";
	default:
		return "scalar";
	}
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

static u8 ms912x_blend_u8(u8 background, u8 foreground, unsigned int alpha)
{
	return (background * (255 - alpha) + foreground * alpha + 127) / 255;
}

static u8 ms912x_blend_pair_chroma(u8 background, unsigned int first,
				   unsigned int second, unsigned int alpha0,
				   unsigned int alpha1)
{
	int delta = alpha0 * ((int)first - background) +
		alpha1 * ((int)second - background);
	int blended = background;

	if (delta >= 0)
		blended += (delta + 255) / 510;
	else
		blended -= (-delta + 255) / 510;
	return clamp_t(int, blended, 0, 255);
}

u64 ms912x_cursor_composite_uyvy(void *transfer_buffer,
				 const struct drm_rect *rect,
				 const u32 *cursor_pixels, int cursor_x, int cursor_y,
				 unsigned int cursor_width,
				 unsigned int cursor_height,
				 bool premultiplied)
{
	u8 *payload = (u8 *)transfer_buffer +
		sizeof(struct ms912x_frame_update_header);
	unsigned int width = drm_rect_width(rect);
	u64 blended_pixels = 0;
	int y;

	if (!cursor_pixels || !cursor_width || !cursor_height ||
	    cursor_width > MS912X_CURSOR_MAX_WIDTH ||
	    cursor_height > MS912X_CURSOR_MAX_HEIGHT)
		return 0;

	for (y = max(rect->y1, cursor_y);
	     y < min(rect->y2, cursor_y + (int)cursor_height); y++) {
		u8 *line = payload + (y - rect->y1) * width * 2;
		unsigned int pair_x;

		for (pair_x = 0; pair_x < width; pair_x += 2) {
			int x0 = rect->x1 + pair_x;
			int x1 = x0 + 1;
			unsigned int src_y = y - cursor_y;
			unsigned int src_x0 = x0 - cursor_x;
			unsigned int src_x1 = x1 - cursor_x;
			u32 pixel0 = 0, pixel1 = 0;
			unsigned int alpha0 = 0, alpha1 = 0;
			unsigned int y0 = 16, y1 = 16, u0 = 128, u1 = 128;
			unsigned int v0 = 128, v1 = 128;
			u8 *pair;

			if (x0 >= cursor_x && src_x0 < cursor_width) {
				pixel0 = cursor_pixels[src_y *
					MS912X_CURSOR_MAX_WIDTH + src_x0];
				alpha0 = pixel0 >> 24;
			}
			if (x1 >= cursor_x && src_x1 < cursor_width) {
				pixel1 = cursor_pixels[src_y *
					MS912X_CURSOR_MAX_WIDTH + src_x1];
				alpha1 = pixel1 >> 24;
			}
			if (!alpha0 && !alpha1)
				continue;
			blended_pixels += !!alpha0 + !!alpha1;

			if (alpha0) {
				unsigned int r = (pixel0 >> 16) & 0xff;
				unsigned int g = (pixel0 >> 8) & 0xff;
				unsigned int b = pixel0 & 0xff;

				if (premultiplied) {
					r = min(255U, (r * 255 + alpha0 / 2) /
						 alpha0);
					g = min(255U, (g * 255 + alpha0 / 2) /
						 alpha0);
					b = min(255U, (b * 255 + alpha0 / 2) /
						 alpha0);
				}

				y0 = ms912x_rgb_to_y(r, g, b);
				u0 = ms912x_rgb_pair_to_u(2 * r, 2 * g, 2 * b);
				v0 = ms912x_rgb_pair_to_v(2 * r, 2 * g, 2 * b);
			}
			if (alpha1) {
				unsigned int r = (pixel1 >> 16) & 0xff;
				unsigned int g = (pixel1 >> 8) & 0xff;
				unsigned int b = pixel1 & 0xff;

				if (premultiplied) {
					r = min(255U, (r * 255 + alpha1 / 2) /
						 alpha1);
					g = min(255U, (g * 255 + alpha1 / 2) /
						 alpha1);
					b = min(255U, (b * 255 + alpha1 / 2) /
						 alpha1);
				}

				y1 = ms912x_rgb_to_y(r, g, b);
				u1 = ms912x_rgb_pair_to_u(2 * r, 2 * g, 2 * b);
				v1 = ms912x_rgb_pair_to_v(2 * r, 2 * g, 2 * b);
			}

			pair = line + pair_x * 2;
			pair[1] = ms912x_blend_u8(pair[1], y0, alpha0);
			pair[3] = ms912x_blend_u8(pair[3], y1, alpha1);
			pair[0] = ms912x_blend_pair_chroma(
				pair[0], u0, u1, alpha0, alpha1);
			pair[2] = ms912x_blend_pair_chroma(
				pair[2], v0, v1, alpha0, alpha1);
		}
	}
	return blended_pixels;
}

void ms912x_debug_draw_uyvy_border(void *transfer_buffer,
				   const struct drm_rect *transfer_rect,
				   const struct drm_rect *new_damage_rect)
{
	u8 *payload = (u8 *)transfer_buffer +
		sizeof(struct ms912x_frame_update_header);
	unsigned int transfer_width = drm_rect_width(transfer_rect);
	int x1 = max(transfer_rect->x1, new_damage_rect->x1);
	int y1 = max(transfer_rect->y1, new_damage_rect->y1);
	int x2 = min(transfer_rect->x2, new_damage_rect->x2);
	int y2 = min(transfer_rect->y2, new_damage_rect->y2);
	int y;

	if (x1 >= x2 || y1 >= y2)
		return;

	for (y = y1; y < y2; y++) {
		int x;
		u8 *line = payload + (y - transfer_rect->y1) *
			transfer_width * 2;
		bool horizontal_edge = y < new_damage_rect->y1 + 2 ||
			y >= new_damage_rect->y2 - 2;

		if (horizontal_edge) {
			for (x = ALIGN_DOWN(x1, 2); x < x2; x += 2) {
				u8 *pair = line + (x - transfer_rect->x1) * 2;

				pair[0] ^= 0xff;
				pair[1] ^= 0xff;
				pair[2] ^= 0xff;
				pair[3] ^= 0xff;
			}
		} else {
			u8 *left = line + (x1 - transfer_rect->x1) * 2;
			u8 *right = line + (x2 - transfer_rect->x1 - 2) * 2;

			left[0] ^= 0xff;
			left[1] ^= 0xff;
			left[2] ^= 0xff;
			left[3] ^= 0xff;
			if (x2 - x1 > 2) {
				right[0] ^= 0xff;
				right[1] ^= 0xff;
				right[2] ^= 0xff;
				right[3] ^= 0xff;
			}
		}
	}
}

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
	enum ms912x_fast_chroma_backend fast_chroma =
		READ_ONCE(ms912x_parameters.fast_chroma);
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

	if (fast_chroma == MS912X_FAST_CHROMA_AUTO) {
#ifdef CONFIG_X86_64
		if (may_use_simd() && boot_cpu_has(X86_FEATURE_AVX) &&
		    boot_cpu_has(X86_FEATURE_AVX2))
			fast_chroma = MS912X_FAST_CHROMA_AVX2;
		else
			fast_chroma = MS912X_FAST_CHROMA_SSE2;
#elif defined(CONFIG_ARM64)
		fast_chroma = MS912X_FAST_CHROMA_NEON;
#else
		fast_chroma = MS912X_FAST_CHROMA_SCALAR;
#endif
	}

	if (fast_chroma == MS912X_FAST_CHROMA_AVX2) {
#ifdef CONFIG_X86_64
		/* AVX is exposed only when the OS has enabled the YMM state. */
		if (may_use_simd() && boot_cpu_has(X86_FEATURE_AVX) &&
		    boot_cpu_has(X86_FEATURE_AVX2)) {
			if (profile)
				simd_start_ns = ktime_get_ns();
			ms912x_convert_xrgb8888_to_uyvy_avx2(dst, src, rect);
			used_simd = true;
			if (profile)
				color_math_ns += ktime_get_ns() - simd_start_ns;
		}
#endif
	} else if (fast_chroma == MS912X_FAST_CHROMA_NEON) {
#ifdef CONFIG_ARM64
		if (profile)
			simd_start_ns = ktime_get_ns();
		used_simd = ms912x_convert_xrgb8888_to_uyvy_neon(dst, src, rect);
#endif
	} else if (fast_chroma == MS912X_FAST_CHROMA_SSE2) {
#ifdef CONFIG_X86_64
		if (profile)
			simd_start_ns = ktime_get_ns();
		used_simd = ms912x_convert_xrgb8888_to_uyvy_sse2(dst, src, rect);
#endif
	}

	if (used_simd) {
		if (profile && fast_chroma != MS912X_FAST_CHROMA_AVX2)
			color_math_ns += ktime_get_ns() - simd_start_ns;
		dst += width * 2 * drm_rect_height(rect);
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
