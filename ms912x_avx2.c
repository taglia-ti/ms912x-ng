// SPDX-License-Identifier: GPL-2.0-only

#include <asm/fpu/api.h>

#include <linux/kernel.h>
#include <linux/unaligned.h>

/* The kernel path does not use the userspace allocation helpers. */
#define _MM_MALLOC_H_INCLUDED
#include <immintrin.h>
#undef _MM_MALLOC_H_INCLUDED

#include "ms912x.h"

#define MS912X_AVX2_ROWS_PER_FPU 16

static __always_inline u32
ms912x_avx2_scalar_pair(const __le32 *pixels)
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

static __always_inline void
ms912x_avx2_convert_eight(u8 *dst, const __le32 *pixels)
{
	const __m256i zero = _mm256_setzero_si256();
	const __m256i mask = _mm256_set1_epi32(0xff);
	const __m256i one = _mm256_set1_epi16(1);
	const __m256i y_rg_coeff = _mm256_setr_epi16(
		16763, 16452, 16763, 16452, 16763, 16452, 16763, 16452,
		16763, 16452, 16763, 16452, 16763, 16452, 16763, 16452);
	const __m256i y_g_coeff = _mm256_setr_epi16(
		0, 16452, 0, 16452, 0, 16452, 0, 16452,
		0, 16452, 0, 16452, 0, 16452, 0, 16452);
	const __m256i y_b_coeff = _mm256_setr_epi16(
		6391, 0, 6391, 0, 6391, 0, 6391, 0,
		6391, 0, 6391, 0, 6391, 0, 6391, 0);
	const __m256i u_rg_coeff = _mm256_setr_epi16(
		-9676, -9498, -9676, -9498, -9676, -9498, -9676, -9498,
		-9676, -9498, -9676, -9498, -9676, -9498, -9676, -9498);
	const __m256i u_g_coeff = _mm256_setr_epi16(
		0, -9498, 0, -9498, 0, -9498, 0, -9498,
		0, -9498, 0, -9498, 0, -9498, 0, -9498);
	const __m256i u_b_coeff = _mm256_setr_epi16(
		28672, 0, 28672, 0, 28672, 0, 28672, 0,
		28672, 0, 28672, 0, 28672, 0, 28672, 0);
	const __m256i v_rg_coeff = _mm256_setr_epi16(
		28672, -24009, 28672, -24009, 28672, -24009, 28672, -24009,
		28672, -24009, 28672, -24009, 28672, -24009, 28672, -24009);
	const __m256i v_b_coeff = _mm256_setr_epi16(
		-4663, 0, -4663, 0, -4663, 0, -4663, 0,
		-4663, 0, -4663, 0, -4663, 0, -4663, 0);
	__m256i packed, r32, g32, b32, r16, g16, b16;
	__m256i y32, rsum32, gsum32, bsum32, rsum16, gsum16, bsum16;
	__m256i rg, g_interleaved, b_interleaved;
	__m256i u_vector, v_vector;
	u32 y[8], u[8], v[8];
	u32 pair[4];
	unsigned int i, chroma_index;

	packed = _mm256_loadu_si256((const __m256i *)pixels);
	b32 = _mm256_and_si256(packed, mask);
	g32 = _mm256_and_si256(_mm256_srli_epi32(packed, 8), mask);
	r32 = _mm256_and_si256(_mm256_srli_epi32(packed, 16), mask);
	r16 = _mm256_packs_epi32(r32, zero);
	g16 = _mm256_packs_epi32(g32, zero);
	b16 = _mm256_packs_epi32(b32, zero);

	/* Q16 luma. 32904 * G is split into two exact 16452 * G terms. */
	rg = _mm256_unpacklo_epi16(r16, g16);
	g_interleaved = _mm256_unpacklo_epi16(zero, g16);
	b_interleaved = _mm256_unpacklo_epi16(b16, zero);
	y32 = _mm256_madd_epi16(rg, y_rg_coeff);
	y32 = _mm256_add_epi32(y32,
			       _mm256_madd_epi16(g_interleaved, y_g_coeff));
	y32 = _mm256_add_epi32(y32,
			       _mm256_madd_epi16(b_interleaved, y_b_coeff));
	y32 = _mm256_add_epi32(y32, _mm256_set1_epi32(16 << 16));
	y32 = _mm256_srli_epi32(y32, 16);

	/* Pairwise sums are calculated independently in each 128-bit lane. */
	rsum32 = _mm256_madd_epi16(r16, one);
	gsum32 = _mm256_madd_epi16(g16, one);
	bsum32 = _mm256_madd_epi16(b16, one);
	rsum16 = _mm256_packs_epi32(rsum32, zero);
	gsum16 = _mm256_packs_epi32(gsum32, zero);
	bsum16 = _mm256_packs_epi32(bsum32, zero);
	rg = _mm256_unpacklo_epi16(rsum16, gsum16);
	g_interleaved = _mm256_unpacklo_epi16(zero, gsum16);
	b_interleaved = _mm256_unpacklo_epi16(bsum16, zero);
	u_vector = _mm256_madd_epi16(rg, u_rg_coeff);
	u_vector = _mm256_add_epi32(u_vector,
				     _mm256_madd_epi16(g_interleaved,
						       u_g_coeff));
	u_vector = _mm256_add_epi32(u_vector,
				     _mm256_madd_epi16(b_interleaved,
						       u_b_coeff));
	u_vector = _mm256_add_epi32(u_vector,
				     _mm256_set1_epi32(128 << 17));
	u_vector = _mm256_srli_epi32(u_vector, 17);
	v_vector = _mm256_madd_epi16(rg, v_rg_coeff);
	v_vector = _mm256_add_epi32(v_vector,
				     _mm256_madd_epi16(b_interleaved,
						       v_b_coeff));
	v_vector = _mm256_add_epi32(v_vector,
				     _mm256_set1_epi32(128 << 17));
	v_vector = _mm256_srli_epi32(v_vector, 17);

	_mm256_storeu_si256((__m256i *)y, y32);
	_mm256_storeu_si256((__m256i *)u, u_vector);
	_mm256_storeu_si256((__m256i *)v, v_vector);
	for (i = 0; i < ARRAY_SIZE(pair); i++) {
		chroma_index = i + (i >> 1) * 2;
		pair[i] = (u[chroma_index] & 0xff) |
			((y[2 * i] & 0xff) << 8) |
			((v[chroma_index] & 0xff) << 16) |
			((y[2 * i + 1] & 0xff) << 24);
	}
	put_unaligned_le64(pair[0] | ((u64)pair[1] << 32), dst);
	put_unaligned_le64(pair[2] | ((u64)pair[3] << 32), dst + 8);
}

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

		for (; x + 8 <= width; x += 8)
			ms912x_avx2_convert_eight(line_dst + x * 2, pixels + x);
		for (; x < width; x += 2) {
			u32 pair = ms912x_avx2_scalar_pair(pixels + x);

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
