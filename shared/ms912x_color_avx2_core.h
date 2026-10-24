/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MS912X_COLOR_AVX2_CORE_H
#define MS912X_COLOR_AVX2_CORE_H
#define _MM_MALLOC_H_INCLUDED
#include <immintrin.h>
#undef _MM_MALLOC_H_INCLUDED

#ifdef __KERNEL__
#define MS912X_COLOR_AVX2_TARGET
#else
#define MS912X_COLOR_AVX2_TARGET __attribute__((target("avx2")))
#endif

/* red is a compile-time byte index: RGBX=0, little-endian XRGB/BGRX=2. */
static MS912X_COLOR_AVX2_TARGET inline __attribute__((always_inline)) __m256i
ms912x_color_avx2_luma(__m256i br, __m256i g, unsigned int red)
{
	const __m256i br_coeff = _mm256_setr_epi16(
		red == 2 ? 6391 : 16763, red == 2 ? 16763 : 6391,
		red == 2 ? 6391 : 16763, red == 2 ? 16763 : 6391,
		red == 2 ? 6391 : 16763, red == 2 ? 16763 : 6391,
		red == 2 ? 6391 : 16763, red == 2 ? 16763 : 6391,
		red == 2 ? 6391 : 16763, red == 2 ? 16763 : 6391,
		red == 2 ? 6391 : 16763, red == 2 ? 16763 : 6391,
		red == 2 ? 6391 : 16763, red == 2 ? 16763 : 6391,
		red == 2 ? 6391 : 16763, red == 2 ? 16763 : 6391);
	const __m256i g_coeff = _mm256_set1_epi32((unsigned short)-32632);
	__m256i y = _mm256_add_epi32(_mm256_madd_epi16(br, br_coeff),
				     _mm256_madd_epi16(g, g_coeff));

	/* 32904 * G = -32632 * G + (G << 16), exactly in Q16. */
	y = _mm256_add_epi32(y, _mm256_slli_epi32(
		_mm256_add_epi32(g, _mm256_set1_epi32(16)), 16));
	return _mm256_srli_epi32(y, 16);
}

/* Each used dword contains one pair's channel sums; return U/V in 16-bit slots. */
static MS912X_COLOR_AVX2_TARGET inline __attribute__((always_inline)) __m256i
ms912x_color_avx2_chroma(__m256i br, __m256i g, unsigned int red)
{
	const __m256i u_br = _mm256_setr_epi16(
		red == 2 ? 28672 : -9676, red == 2 ? -9676 : 28672,
		red == 2 ? 28672 : -9676, red == 2 ? -9676 : 28672,
		red == 2 ? 28672 : -9676, red == 2 ? -9676 : 28672,
		red == 2 ? 28672 : -9676, red == 2 ? -9676 : 28672,
		red == 2 ? 28672 : -9676, red == 2 ? -9676 : 28672,
		red == 2 ? 28672 : -9676, red == 2 ? -9676 : 28672,
		red == 2 ? 28672 : -9676, red == 2 ? -9676 : 28672,
		red == 2 ? 28672 : -9676, red == 2 ? -9676 : 28672);
	const __m256i u_g = _mm256_set1_epi32((unsigned short)-18996);
	const __m256i v_br = _mm256_setr_epi16(
		red == 2 ? -4663 : 28672, red == 2 ? 28672 : -4663,
		red == 2 ? -4663 : 28672, red == 2 ? 28672 : -4663,
		red == 2 ? -4663 : 28672, red == 2 ? 28672 : -4663,
		red == 2 ? -4663 : 28672, red == 2 ? 28672 : -4663,
		red == 2 ? -4663 : 28672, red == 2 ? 28672 : -4663,
		red == 2 ? -4663 : 28672, red == 2 ? 28672 : -4663,
		red == 2 ? -4663 : 28672, red == 2 ? 28672 : -4663,
		red == 2 ? -4663 : 28672, red == 2 ? 28672 : -4663);
	const __m256i v_g = _mm256_set1_epi32((unsigned short)-24009);
	const __m256i bias = _mm256_set1_epi32(128 << 17);
	__m256i u = _mm256_add_epi32(_mm256_madd_epi16(br, u_br),
				     _mm256_madd_epi16(g, u_g));
	__m256i v = _mm256_add_epi32(_mm256_madd_epi16(br, v_br),
				     _mm256_madd_epi16(g, v_g));

	u = _mm256_srli_epi32(_mm256_add_epi32(u, bias), 17);
	v = _mm256_srli_epi32(_mm256_add_epi32(v, bias), 17);
	return _mm256_or_si256(u, _mm256_slli_epi32(v, 16));
}

static MS912X_COLOR_AVX2_TARGET inline __attribute__((always_inline)) void
ms912x_color_avx2_sixteen(unsigned char *dst, const void *pixels, unsigned int red)
{
	const __m256i br_mask = _mm256_set1_epi32(0x00ff00ff);
	const __m256i g_mask = _mm256_set1_epi32(0xff);
	const unsigned char *src = pixels;
	__m256i a = _mm256_loadu_si256((const __m256i *)src);
	__m256i b = _mm256_loadu_si256((const __m256i *)(src + 32));
	__m256i br0 = _mm256_and_si256(a, br_mask);
	__m256i br1 = _mm256_and_si256(b, br_mask);
	__m256i g0 = _mm256_and_si256(_mm256_srli_epi32(a, 8), g_mask);
	__m256i g1 = _mm256_and_si256(_mm256_srli_epi32(b, 8), g_mask);
	__m256i y = _mm256_packs_epi32(ms912x_color_avx2_luma(br0, g0, red),
					ms912x_color_avx2_luma(br1, g1, red));
	/* Pair sums cannot carry between channels: each 16-bit sum is at most 510. */
	__m256i uv = ms912x_color_avx2_chroma(_mm256_hadd_epi32(br0, br1),
					    _mm256_hadd_epi32(g0, g1), red);
	__m256i packed = _mm256_packus_epi16(uv, y);

	packed = _mm256_shuffle_epi8(packed, _mm256_setr_epi8(
		0, 8, 1, 9, 2, 10, 3, 11, 4, 12, 5, 13, 6, 14, 7, 15,
		0, 8, 1, 9, 2, 10, 3, 11, 4, 12, 5, 13, 6, 14, 7, 15));
	/* AVX2 packs stay in 128-bit lanes; restore the four 4-pixel groups. */
	packed = _mm256_permute4x64_epi64(packed, _MM_SHUFFLE(3, 1, 2, 0));
	_mm256_storeu_si256((__m256i *)dst, packed);
}

static MS912X_COLOR_AVX2_TARGET inline __attribute__((always_inline)) void
ms912x_color_avx2_eight(unsigned char *dst, const void *pixels, unsigned int red)
{
	__m256i packed = _mm256_loadu_si256((const __m256i *)pixels);
	__m256i br = _mm256_and_si256(packed, _mm256_set1_epi32(0x00ff00ff));
	__m256i g = _mm256_and_si256(_mm256_srli_epi32(packed, 8),
				     _mm256_set1_epi32(0xff));
	__m256i y = ms912x_color_avx2_luma(br, g, red);

	br = _mm256_add_epi16(br, _mm256_srli_epi64(br, 32));
	g = _mm256_add_epi16(g, _mm256_srli_epi64(g, 32));
	packed = ms912x_color_avx2_chroma(br, g, red);
	packed = _mm256_or_si256(packed, _mm256_slli_epi32(y, 8));
	packed = _mm256_or_si256(packed, _mm256_srli_epi64(y, 8));
	packed = _mm256_permutevar8x32_epi32(packed,
		_mm256_setr_epi32(0, 2, 4, 6, 0, 2, 4, 6));
	_mm_storeu_si128((__m128i *)dst, _mm256_castsi256_si128(packed));
}

#endif
