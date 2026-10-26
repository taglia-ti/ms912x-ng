// SPDX-License-Identifier: GPL-2.0-only
#include <asm/fpu/api.h>

#define _MM_MALLOC_H_INCLUDED
#include <immintrin.h>
#undef _MM_MALLOC_H_INCLUDED

#include "ms912x_downscale_5to4.h"
#include "ms912x_downscale_5to4_sse.h"

static __always_inline __m256i ms912x_5to4_avx_pairs(__m256i ac, __m256i bd,
						   __m256i no, __m256i cf)
{
	__m256i sum = _mm256_sub_epi16(_mm256_maddubs_epi16(ac, no),
					_mm256_set1_epi16(16384));

	sum = _mm256_add_epi16(sum, _mm256_maddubs_epi16(bd, cf));
	sum = _mm256_srai_epi16(_mm256_add_epi16(sum, _mm256_set1_epi16(64)), 7);
	return _mm256_add_epi16(sum, _mm256_set1_epi16(128));
}

static __always_inline __m256i ms912x_5to4_avx_filter(__m256i a, __m256i b,
						 __m256i c, __m256i d, unsigned int inner)
{
	unsigned int n = 123 - 30 * inner, w = 12 + 38 * inner;
	unsigned int o = 6 + 3 * inner, f = 1 + 5 * inner;
	__m256i no = _mm256_set1_epi16((short)(n | ((256 - o) << 8)));
	__m256i cf = _mm256_set1_epi16((short)(w | ((256 - f) << 8)));
	__m256i lo = ms912x_5to4_avx_pairs(_mm256_unpacklo_epi8(a, c),
					       _mm256_unpacklo_epi8(b, d), no, cf);
	__m256i hi = ms912x_5to4_avx_pairs(_mm256_unpackhi_epi8(a, c),
					       _mm256_unpackhi_epi8(b, d), no, cf);

	return _mm256_packus_epi16(lo, hi);
}

static __always_inline __m256i ms912x_5to4_avx_horizontal(const u32 *row)
{
	__m128i a0, b0, c0, d0, a1, b1, c1, d1;
	__m256i a, b, c, d, lo, hi;
	__m256i no = _mm256_broadcastsi128_si256(_mm_set_epi32(
		(int)0xf75df75d, (int)0xf75df75d, (int)0xfa7bfa7b, (int)0xfa7bfa7b));
	__m256i cf = _mm256_broadcastsi128_si256(_mm_set_epi32(
		(int)0xfa32fa32, (int)0xfa32fa32, (int)0xff0cff0c, (int)0xff0cff0c));

	ms912x_5to4_sse_gather(row, &a0, &b0, &c0, &d0);
	ms912x_5to4_sse_gather(row + 5, &a1, &b1, &c1, &d1);
	a = _mm256_inserti128_si256(_mm256_castsi128_si256(a0), a1, 1);
	b = _mm256_inserti128_si256(_mm256_castsi128_si256(b0), b1, 1);
	c = _mm256_inserti128_si256(_mm256_castsi128_si256(c0), c1, 1);
	d = _mm256_inserti128_si256(_mm256_castsi128_si256(d0), d1, 1);
	lo = ms912x_5to4_avx_pairs(_mm256_unpacklo_epi8(a, c),
		_mm256_unpacklo_epi8(b, d), no, cf);
	hi = ms912x_5to4_avx_pairs(_mm256_unpackhi_epi8(a, c),
		_mm256_unpackhi_epi8(b, d),
		_mm256_shuffle_epi32(no, _MM_SHUFFLE(1, 0, 3, 2)),
		_mm256_shuffle_epi32(cf, _MM_SHUFFLE(1, 0, 3, 2)));
	return _mm256_packus_epi16(lo, hi);
}

#define MS912X_5TO4_SCOPE static noinline
#define MS912X_5TO4_WORKER ms912x_5to4_avx2_rows
#define MS912X_5TO4_PIXELS 8
#define MS912X_5TO4_TYPE __m256i
#define MS912X_5TO4_LOAD(p) _mm256_loadu_si256((const __m256i *)(p))
#define MS912X_5TO4_STORE(p, v) _mm256_storeu_si256((__m256i *)(p), (v))
#define MS912X_5TO4_H(row, x, ox) ms912x_5to4_avx_horizontal((row) + (x) / 4 * 5 + (ox))
#define MS912X_5TO4_A(a, b, c, d) ms912x_5to4_avx_filter(a, b, c, d, 0)
#define MS912X_5TO4_B(a, b, c, d) ms912x_5to4_avx_filter(a, b, c, d, 1)
#define MS912X_5TO4_FINISH _mm256_zeroupper
#include "ms912x_downscale_5to4_template.h"

#define MS912X_5TO4_REGION ms912x_downscale_xrgb8888_5to4_avx2_region
#define MS912X_5TO4_BEGIN kernel_fpu_begin
#define MS912X_5TO4_END kernel_fpu_end
#include "ms912x_downscale_5to4_entry.h"
