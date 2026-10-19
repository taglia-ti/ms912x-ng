// SPDX-License-Identifier: GPL-2.0-only

#include <asm/fpu/api.h>

#include <linux/kernel.h>
#include <linux/unaligned.h>

/* The kernel path does not use the userspace allocation helpers. */
#define _MM_MALLOC_H_INCLUDED
#include <immintrin.h>
#undef _MM_MALLOC_H_INCLUDED

#include "ms912x.h"
#include "ms912x_color_sse2.h"

#define MS912X_AVX2_ROWS_PER_FPU 16

static __always_inline __m256i
ms912x_avx2_luma(__m256i br, __m256i g)
{
	const __m256i br_coeff = _mm256_setr_epi16(
		6391, 16763, 6391, 16763, 6391, 16763, 6391, 16763,
		6391, 16763, 6391, 16763, 6391, 16763, 6391, 16763);
	const __m256i g_coeff = _mm256_set1_epi32((u16)-32632);
	__m256i y = _mm256_add_epi32(_mm256_madd_epi16(br, br_coeff),
				     _mm256_madd_epi16(g, g_coeff));

	/* 32904 * G = -32632 * G + (G << 16), exactly in Q16. */
	y = _mm256_add_epi32(y, _mm256_slli_epi32(
		_mm256_add_epi32(g, _mm256_set1_epi32(16)), 16));
	return _mm256_srli_epi32(y, 16);
}

/* Each dword contains one pair's B/R or G sums; return U/V in 16-bit slots. */
static __always_inline __m256i
ms912x_avx2_chroma(__m256i br, __m256i g)
{
	const __m256i u_br = _mm256_setr_epi16(
		28672, -9676, 28672, -9676, 28672, -9676, 28672, -9676,
		28672, -9676, 28672, -9676, 28672, -9676, 28672, -9676);
	const __m256i u_g = _mm256_set1_epi32((u16)-18996);
	const __m256i v_br = _mm256_setr_epi16(
		-4663, 28672, -4663, 28672, -4663, 28672, -4663, 28672,
		-4663, 28672, -4663, 28672, -4663, 28672, -4663, 28672);
	const __m256i v_g = _mm256_set1_epi32((u16)-24009);
	const __m256i bias = _mm256_set1_epi32(128 << 17);
	__m256i u = _mm256_add_epi32(_mm256_madd_epi16(br, u_br),
				     _mm256_madd_epi16(g, u_g));
	__m256i v = _mm256_add_epi32(_mm256_madd_epi16(br, v_br),
				     _mm256_madd_epi16(g, v_g));

	u = _mm256_srli_epi32(_mm256_add_epi32(u, bias), 17);
	v = _mm256_srli_epi32(_mm256_add_epi32(v, bias), 17);
	return _mm256_or_si256(u, _mm256_slli_epi32(v, 16));
}

static __always_inline void
ms912x_avx2_convert_sixteen(u8 *dst, const __le32 *pixels)
{
	const __m256i br_mask = _mm256_set1_epi32(0x00ff00ff);
	const __m256i g_mask = _mm256_set1_epi32(0xff);
	__m256i a = _mm256_loadu_si256((const __m256i *)pixels);
	__m256i b = _mm256_loadu_si256((const __m256i *)(pixels + 8));
	__m256i br0 = _mm256_and_si256(a, br_mask);
	__m256i br1 = _mm256_and_si256(b, br_mask);
	__m256i g0 = _mm256_and_si256(_mm256_srli_epi32(a, 8), g_mask);
	__m256i g1 = _mm256_and_si256(_mm256_srli_epi32(b, 8), g_mask);
	__m256i y = _mm256_packs_epi32(ms912x_avx2_luma(br0, g0),
					ms912x_avx2_luma(br1, g1));
	/* Pair sums cannot carry from B to R: each 16-bit sum is at most 510. */
	__m256i uv = ms912x_avx2_chroma(_mm256_hadd_epi32(br0, br1),
					_mm256_hadd_epi32(g0, g1));
	__m256i packed = _mm256_packus_epi16(uv, y);

	packed = _mm256_shuffle_epi8(packed, _mm256_setr_epi8(
		0, 8, 1, 9, 2, 10, 3, 11, 4, 12, 5, 13, 6, 14, 7, 15,
		0, 8, 1, 9, 2, 10, 3, 11, 4, 12, 5, 13, 6, 14, 7, 15));
	/* AVX2 packs stay in 128-bit lanes; restore the four 4-pixel groups. */
	packed = _mm256_permute4x64_epi64(packed, _MM_SHUFFLE(3, 1, 2, 0));
	_mm256_storeu_si256((__m256i *)dst, packed);
}

static __always_inline void
ms912x_avx2_convert_eight(u8 *dst, const __le32 *pixels)
{
	__m256i packed = _mm256_loadu_si256((const __m256i *)pixels);
	__m256i br = _mm256_and_si256(packed, _mm256_set1_epi32(0x00ff00ff));
	__m256i g = _mm256_and_si256(_mm256_srli_epi32(packed, 8),
				     _mm256_set1_epi32(0xff));
	__m256i y = ms912x_avx2_luma(br, g);

	br = _mm256_add_epi16(br, _mm256_srli_epi64(br, 32));
	g = _mm256_add_epi16(g, _mm256_srli_epi64(g, 32));
	packed = ms912x_avx2_chroma(br, g);
	packed = _mm256_or_si256(packed, _mm256_slli_epi32(y, 8));
	packed = _mm256_or_si256(packed, _mm256_srli_epi64(y, 8));
	packed = _mm256_permutevar8x32_epi32(packed,
		_mm256_setr_epi32(0, 2, 4, 6, 0, 2, 4, 6));
	_mm_storeu_si128((__m128i *)dst, _mm256_castsi256_si128(packed));
}

/* Keep compiler-generated SIMD inside the FPU-protected batch call. */
static noinline void
ms912x_avx2_convert_rows(void *dst, const __le32 *src,
			 const struct drm_rect *rect,
			 unsigned int first_row, unsigned int end_row)
{
	unsigned int width = drm_rect_width(rect);
	unsigned int row;

	for (row = first_row; row < end_row; row++) {
		const __le32 *pixels = src + row * MS912X_MAX_WIDTH + rect->x1;
		u8 *line_dst = dst + (row - rect->y1) * width * 2;
		unsigned int x = 0;

		for (; x + 16 <= width; x += 16)
			ms912x_avx2_convert_sixteen(line_dst + x * 2, pixels + x);
		if (x + 8 <= width) {
			ms912x_avx2_convert_eight(line_dst + x * 2, pixels + x);
			x += 8;
		}
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

void ms912x_convert_xrgb8888_to_uyvy_avx2(void *dst, const __le32 *src,
					   const struct drm_rect *rect)
{
	unsigned int row = rect->y1;
	unsigned int chunk_end;
	unsigned int end = rect->y2;

	while (row < end) {
		chunk_end = min(row + MS912X_AVX2_ROWS_PER_FPU, end);
		kernel_fpu_begin();
		ms912x_avx2_convert_rows(dst, src, rect, row, chunk_end);
		_mm256_zeroupper();
		kernel_fpu_end();
		row = chunk_end;
	}
}
