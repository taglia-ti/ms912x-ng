// SPDX-License-Identifier: GPL-2.0-only

#include <asm/fpu/api.h>
#include <asm/simd.h>

#include <linux/kernel.h>
#include <linux/unaligned.h>

#define _MM_MALLOC_H_INCLUDED
#include <tmmintrin.h>
#undef _MM_MALLOC_H_INCLUDED

#include "ms912x.h"
#include "ms912x_color_sse2.h"

#define MS912X_SIMD_ROWS_PER_FPU 16

static __always_inline __m128i ms912x_ssse3_luma(__m128i br, __m128i g)
{
	const __m128i br_coeff = _mm_setr_epi16(6391, 16763, 6391, 16763, 6391, 16763, 6391, 16763);
	const __m128i g_coeff = _mm_set1_epi32((u16)-32632);
	__m128i y = _mm_add_epi32(_mm_madd_epi16(br, br_coeff), _mm_madd_epi16(g, g_coeff));

	/* 32904 * G = -32632 * G + (G << 16), exactly in Q16. */
	y = _mm_add_epi32(y, _mm_slli_epi32(_mm_add_epi32(g, _mm_set1_epi32(16)), 16));
	return _mm_srli_epi32(y, 16);
}

/* Each dword contains one pair's B/R or G sums; return U/V in 16-bit slots. */
static __always_inline __m128i ms912x_ssse3_chroma(__m128i br, __m128i g)
{
	const __m128i u_br = _mm_setr_epi16(28672, -9676, 28672, -9676, 28672, -9676, 28672, -9676);
	const __m128i u_g = _mm_set1_epi32((u16)-18996);
	const __m128i v_br = _mm_setr_epi16(-4663, 28672, -4663, 28672, -4663, 28672, -4663, 28672);
	const __m128i v_g = _mm_set1_epi32((u16)-24009);
	const __m128i bias = _mm_set1_epi32(128 << 17);
	__m128i u = _mm_add_epi32(_mm_madd_epi16(br, u_br), _mm_madd_epi16(g, u_g));
	__m128i v = _mm_add_epi32(_mm_madd_epi16(br, v_br), _mm_madd_epi16(g, v_g));

	u = _mm_srli_epi32(_mm_add_epi32(u, bias), 17);
	v = _mm_srli_epi32(_mm_add_epi32(v, bias), 17);
	return _mm_or_si128(u, _mm_slli_epi32(v, 16));
}

static __always_inline void ms912x_ssse3_convert_eight(u8 *dst, const __le32 *pixels)
{
	const __m128i br_mask = _mm_set1_epi32(0x00ff00ff);
	const __m128i g_mask = _mm_set1_epi32(0xff);
	__m128i a = _mm_loadu_si128((const __m128i *)pixels);
	__m128i b = _mm_loadu_si128((const __m128i *)(pixels + 4));
	__m128i br0 = _mm_and_si128(a, br_mask);
	__m128i br1 = _mm_and_si128(b, br_mask);
	__m128i g0 = _mm_and_si128(_mm_srli_epi32(a, 8), g_mask);
	__m128i g1 = _mm_and_si128(_mm_srli_epi32(b, 8), g_mask);
	__m128i y = _mm_packs_epi32(ms912x_ssse3_luma(br0, g0), ms912x_ssse3_luma(br1, g1));
	/* Pair sums cannot carry from B to R: each 16-bit sum is at most 510. */
	__m128i uv = ms912x_ssse3_chroma(_mm_hadd_epi32(br0, br1), _mm_hadd_epi32(g0, g1));
	__m128i packed = _mm_packus_epi16(uv, y);

	packed = _mm_shuffle_epi8(
	    packed, _mm_setr_epi8(0, 8, 1, 9, 2, 10, 3, 11, 4, 12, 5, 13, 6, 14, 7, 15));
	_mm_storeu_si128((__m128i *)dst, packed);
}

/* Keep compiler-generated SIMD inside the FPU-protected batch call. */
static noinline void ms912x_ssse3_convert_rows(void *dst, const __le32 *src,
					       const struct drm_rect *rect, unsigned int first_row,
					       unsigned int end_row, unsigned int dst_stride)
{
	unsigned int width = drm_rect_width(rect);
	unsigned int row;

	for (row = first_row; row < end_row; row++) {
		const __le32 *pixels = src + row * MS912X_MAX_WIDTH + rect->x1;
		u8 *line_dst = dst + (row - rect->y1) * dst_stride;
		unsigned int x = 0;

		for (; x + 8 <= width; x += 8)
			ms912x_ssse3_convert_eight(line_dst + x * 2, pixels + x);
		if (x + 4 <= width) {
			ms912x_sse2_convert_four(line_dst + x * 2, pixels + x);
			x += 4;
		}
		if (x < width) {
			u32 pair = ms912x_xrgb_pair_to_uyvy(pixels + x);

			put_unaligned_le32(pair, line_dst + x * 2);
		}
	}
}

bool ms912x_convert_xrgb8888_to_uyvy_ssse3_strided(void *dst, const __le32 *src,
						   const struct drm_rect *rect,
						   unsigned int dst_stride)
{
	unsigned int row = rect->y1;
	unsigned int chunk_end;
	unsigned int end = rect->y2;

	/* No full SSSE3 output register: avoid its worker setup for tiny rows. */
	if (drm_rect_width(rect) < 8)
		return ms912x_convert_xrgb8888_to_uyvy_sse2_strided(dst, src, rect, dst_stride);

	if (!may_use_simd())
		return false;

	while (row < end) {
		chunk_end = min(row + MS912X_SIMD_ROWS_PER_FPU, end);
		kernel_fpu_begin();
		ms912x_ssse3_convert_rows(dst, src, rect, row, chunk_end, dst_stride);
		kernel_fpu_end();
		row = chunk_end;
	}

	return true;
}

bool ms912x_convert_xrgb8888_to_uyvy_ssse3(void *dst, const __le32 *src,
					   const struct drm_rect *rect)
{
	return ms912x_convert_xrgb8888_to_uyvy_ssse3_strided(dst, src, rect,
							     drm_rect_width(rect) * 2);
}
