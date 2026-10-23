/* SPDX-License-Identifier: GPL-2.0-only */
#include "ms912x_rgbx_internal.h"

#if defined(__SSE2__)
#include <emmintrin.h>

MS912X_COLOR_INLINE void
convert_four(uint8_t *dst, const uint8_t *src, unsigned int red)
{
	const int low_y = red == 0 ? 16763 : 6391;
	const int high_y = red == 0 ? 6391 : 16763;
	const int low_u = red == 0 ? -9676 : 28672;
	const int high_u = red == 0 ? 28672 : -9676;
	const int low_v = red == 0 ? 28672 : -4663;
	const int high_v = red == 0 ? -4663 : 28672;
	const __m128i y_rb = _mm_setr_epi16(low_y, high_y, low_y, high_y,
					   low_y, high_y, low_y, high_y);
	const __m128i u_rb = _mm_setr_epi16(low_u, high_u, low_u, high_u,
					   low_u, high_u, low_u, high_u);
	const __m128i v_rb = _mm_setr_epi16(low_v, high_v, low_v, high_v,
					   low_v, high_v, low_v, high_v);
	__m128i input = _mm_loadu_si128((const __m128i *)src);
	__m128i rb = _mm_and_si128(input, _mm_set1_epi32(0x00ff00ff));
	__m128i g = _mm_and_si128(_mm_srli_epi32(input, 8), _mm_set1_epi32(255));
	__m128i y, u, v, packed;

	/* 32904*G = -32632*G + (G<<16), preserving the scalar Q16 result. */
	y = _mm_add_epi32(_mm_madd_epi16(rb, y_rb),
			  _mm_madd_epi16(g, _mm_set1_epi32((uint16_t)-32632)));
	y = _mm_srli_epi32(_mm_add_epi32(y, _mm_slli_epi32(
		_mm_add_epi32(g, _mm_set1_epi32(16)), 16)), 16);
	rb = _mm_add_epi16(rb, _mm_srli_epi64(rb, 32));
	g = _mm_add_epi16(g, _mm_srli_epi64(g, 32));
	u = _mm_add_epi32(_mm_madd_epi16(rb, u_rb),
			  _mm_madd_epi16(g, _mm_set1_epi32((uint16_t)-18996)));
	v = _mm_add_epi32(_mm_madd_epi16(rb, v_rb),
			  _mm_madd_epi16(g, _mm_set1_epi32((uint16_t)-24009)));
	u = _mm_srli_epi32(_mm_add_epi32(u, _mm_set1_epi32(128 << 17)), 17);
	v = _mm_srli_epi32(_mm_add_epi32(v, _mm_set1_epi32(128 << 17)), 17);
	packed = _mm_or_si128(u, _mm_slli_epi32(v, 16));
	packed = _mm_or_si128(packed, _mm_slli_epi32(y, 8));
	packed = _mm_or_si128(packed, _mm_srli_epi64(y, 8));
	packed = _mm_shuffle_epi32(packed, _MM_SHUFFLE(2, 0, 2, 0));
	_mm_storel_epi64((__m128i *)dst, packed);
}

MS912X_COLOR_INLINE void
convert_row(uint8_t *dst, const uint8_t *src, uint32_t width, unsigned int red)
{
	uint32_t x = 0;

	for (; x + 8U <= width; x += 8U) {
		convert_four(dst + (size_t)x * 2U, src + (size_t)x * 4U, red);
		convert_four(dst + (size_t)x * 2U + 8U, src + (size_t)x * 4U + 16U, red);
	}
	if (x + 4U <= width) {
		convert_four(dst + (size_t)x * 2U, src + (size_t)x * 4U, red);
		x += 4U;
	}
	if (x < width)
		ms912x_rgbx_pair(dst + (size_t)x * 2U, src + (size_t)x * 4U, 4, red);
}

void ms912x_rgbx_row_sse2(uint8_t *dst, const uint8_t *src, uint32_t width, size_t step)
{
	(void)step;
	convert_row(dst, src, width, 0);
}

void ms912x_bgrx_row_sse2(uint8_t *dst, const uint8_t *src, uint32_t width, size_t step)
{
	(void)step;
	convert_row(dst, src, width, 2);
}
#endif
