// SPDX-License-Identifier: GPL-2.0-only

#include <asm/byteorder.h>

#include <linux/kernel.h>
#include <linux/minmax.h>
#include <linux/unaligned.h>

#include <drm/drm_rect.h>

#ifdef CONFIG_X86_64
#include <asm/cpufeature.h>
#include <asm/simd.h>
#elif defined(CONFIG_ARM64) || defined(CONFIG_ARM)
#include <asm/simd.h>
#endif

#include "ms912x.h"
#include "ms912x_color.h"
#include "ms912x_mode3.h"
#include "ms912x_scan_methods.h"
#include "ms912x_pixels.h"

#ifdef CONFIG_X86_64
static enum ms912x_fast_chroma_backend
ms912x_fast_chroma_select_arch(enum ms912x_fast_chroma_backend mode)
{
	if (!may_use_simd())
		return MS912X_FAST_CHROMA_SCALAR;
	if (mode == MS912X_FAST_CHROMA_AUTO) {
		if (boot_cpu_has(X86_FEATURE_AVX) && boot_cpu_has(X86_FEATURE_AVX2))
			return MS912X_FAST_CHROMA_AVX2;
		if (boot_cpu_has(X86_FEATURE_SSSE3))
			return MS912X_FAST_CHROMA_SSSE3;
		return MS912X_FAST_CHROMA_SSE2;
	}
	if (mode == MS912X_FAST_CHROMA_AVX2 &&
	    (!boot_cpu_has(X86_FEATURE_AVX) || !boot_cpu_has(X86_FEATURE_AVX2)))
		return MS912X_FAST_CHROMA_SCALAR;
	if (mode == MS912X_FAST_CHROMA_SSSE3 && !boot_cpu_has(X86_FEATURE_SSSE3))
		return MS912X_FAST_CHROMA_SCALAR;
	if (mode == MS912X_FAST_CHROMA_NEON)
		return MS912X_FAST_CHROMA_SCALAR;
	return mode;
}
#elif MS912X_HAS_NEON
static enum ms912x_fast_chroma_backend
ms912x_fast_chroma_select_arch(enum ms912x_fast_chroma_backend mode)
{
	if (!ms912x_scan_neon_available())
		return MS912X_FAST_CHROMA_SCALAR;
	if (mode == MS912X_FAST_CHROMA_AUTO || mode == MS912X_FAST_CHROMA_NEON)
		return MS912X_FAST_CHROMA_NEON;
	return MS912X_FAST_CHROMA_SCALAR;
}
#else
static enum ms912x_fast_chroma_backend
ms912x_fast_chroma_select_arch(enum ms912x_fast_chroma_backend mode)
{
	return MS912X_FAST_CHROMA_SCALAR;
}
#endif

static enum ms912x_fast_chroma_backend ms912x_fast_chroma_select(void)
{
	return ms912x_fast_chroma_select_arch(READ_ONCE(ms912x_parameters.fast_chroma));
}

const char *ms912x_fast_chroma_selected_backend_name(void)
{
	switch (ms912x_fast_chroma_select()) {
	case MS912X_FAST_CHROMA_AVX2:
		return "avx2";
	case MS912X_FAST_CHROMA_NEON:
		return "neon";
	case MS912X_FAST_CHROMA_SSSE3:
		return "ssse3";
	case MS912X_FAST_CHROMA_SSE2:
		return "sse2";
	default:
		return "scalar";
	}
}

static void ms912x_xrgb_to_uyvy_line(u8 *transfer_buffer,
				       const __le32 *xrgb_line,
				       size_t x, size_t width)
{
	u8 *dst = transfer_buffer;
	unsigned int i = 0;

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
}

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
				 unsigned int cursor_alpha_x,
				 unsigned int cursor_alpha_y,
				 bool premultiplied)
{
	u8 *payload = (u8 *)transfer_buffer +
		sizeof(struct ms912x_frame_update_header);
	unsigned int width = drm_rect_width(rect);
	unsigned int pair_start, pair_end;
	u64 blended_pixels = 0;
	int cursor_left, cursor_right, y;

	if (!cursor_pixels || !cursor_width || !cursor_height ||
	    cursor_width > MS912X_CURSOR_MAX_WIDTH ||
	    cursor_height > MS912X_CURSOR_MAX_HEIGHT)
		return 0;
	cursor_left = max(rect->x1, cursor_x);
	cursor_right = min(rect->x2, cursor_x + (int)cursor_width);
	if (cursor_left >= cursor_right)
		return 0;
	pair_start = (cursor_left - rect->x1) & ~1U;
	pair_end = min_t(unsigned int, width,
			 (cursor_right - rect->x1 + 1) & ~1U);

	for (y = max(rect->y1, cursor_y);
	     y < min(rect->y2, cursor_y + (int)cursor_height); y++) {
		u8 *line = payload + (y - rect->y1) * width * 2;
		unsigned int pair_x;

		for (pair_x = pair_start; pair_x < pair_end; pair_x += 2) {
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
				pixel0 = cursor_pixels[ms912x_cursor_source_index(
					src_x0, src_y, cursor_alpha_x,
					cursor_alpha_y)];
				alpha0 = pixel0 >> 24;
			}
			if (x1 >= cursor_x && src_x1 < cursor_width) {
				pixel1 = cursor_pixels[ms912x_cursor_source_index(
					src_x1, src_y, cursor_alpha_x,
					cursor_alpha_y)];
				alpha1 = pixel1 >> 24;
			}
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

#ifdef CONFIG_X86_64
static bool ms912x_convert_simd(enum ms912x_fast_chroma_backend backend,
			       void *dst, const __le32 *src, const struct drm_rect *rect)
{
	switch (backend) {
	case MS912X_FAST_CHROMA_AVX2:
		ms912x_convert_xrgb8888_to_uyvy_avx2_strided(dst, src, rect,
							 MS912X_MAX_WIDTH * 2);
		return true;
	case MS912X_FAST_CHROMA_SSSE3:
		return ms912x_convert_xrgb8888_to_uyvy_ssse3_strided(dst, src, rect,
								 MS912X_MAX_WIDTH * 2);
	case MS912X_FAST_CHROMA_SSE2:
		return ms912x_convert_xrgb8888_to_uyvy_sse2_strided(dst, src, rect,
								MS912X_MAX_WIDTH * 2);
	default:
		return false;
	}
}
#elif MS912X_HAS_NEON
static bool ms912x_convert_simd(enum ms912x_fast_chroma_backend backend,
			       void *dst, const __le32 *src, const struct drm_rect *rect)
{
	return backend == MS912X_FAST_CHROMA_NEON &&
		ms912x_convert_xrgb8888_to_uyvy_neon_strided(dst, src, rect,
							  MS912X_MAX_WIDTH * 2);
}
#else
static bool ms912x_convert_simd(enum ms912x_fast_chroma_backend backend,
			       void *dst, const __le32 *src, const struct drm_rect *rect)
{
	return false;
}
#endif

/* Source rows are XRGB8888; destination rows are the canonical UYVY cache. */
int ms912x_convert_to_uyvy(u32 *dst, const __le32 *src, const struct drm_rect *rect)
{
	enum ms912x_fast_chroma_backend backend = ms912x_fast_chroma_select();
	unsigned int width, y;
	u8 *out;

	if (!dst || !src || !rect || rect->x1 < 0 || rect->y1 < 0 || rect->x2 > MS912X_MAX_WIDTH ||
	    rect->y2 > MS912X_MAX_HEIGHT || rect->x1 >= rect->x2 || rect->y1 >= rect->y2 ||
	    (rect->x1 & 1) || (drm_rect_width(rect) & 1))
		return -EINVAL;
	width = drm_rect_width(rect);
	out = ms912x_pixel_at(dst, rect->x1, rect->y1);
	if (!ms912x_convert_simd(backend, out, src, rect))
		for (y = rect->y1; y < rect->y2; y++) {
			ms912x_xrgb_to_uyvy_line(out, src + y * MS912X_MAX_WIDTH, rect->x1, width);
			out += MS912X_MAX_WIDTH * 2;
		}
	return 0;
}

/* Copy already converted pairs; the sole final trailer closes the logical batch. */
int ms912x_pack_uyvy_batch(void *dst, const u32 *src, unsigned int fb_width, unsigned int fb_height,
			   const struct drm_rect *rects, unsigned int rect_count)
{
	u8 *out = dst;
	unsigned int i, y;

	if (!dst || !src || !rects || !rect_count || rect_count > MS912X_MAX_BATCH_RECTS ||
	    fb_width > MS912X_MAX_WIDTH || fb_height > MS912X_MAX_HEIGHT)
		return -EINVAL;
	for (i = 0; i < rect_count; i++) {
		const struct drm_rect *rect = &rects[i];
		struct ms912x_frame_update_header *header = (void *)out;
		unsigned int width = drm_rect_width(rect);
		u32 position, dimensions;

		if (rect->x1 < 0 || rect->y1 < 0 || rect->x2 > fb_width || rect->y2 > fb_height ||
		    rect->x1 >= rect->x2 || rect->y1 >= rect->y2 || (rect->x1 & 1) || (width & 1))
			return -EINVAL;
		header->marker = cpu_to_be16(0xff00);
		position = ((rect->x1 & 0xfff) << 12) | (rect->y1 & 0xfff);
		dimensions = ((width & 0xfff) << 12) | (drm_rect_height(rect) & 0xfff);
		put_unaligned_be24(position, header->position);
		put_unaligned_be24(dimensions, header->dimensions);
		out += sizeof(*header);
		for (y = rect->y1; y < rect->y2; y++) {
			memcpy(out, ms912x_pixel_at_const(src, rect->x1, y), width * 2);
			out += width * 2;
		}
		ms912x_mode3_write_trailer(out, i + 1 == rect_count);
		out += MS912X_MODE3_TRAILER_BYTES;
	}
	return 0;
}
