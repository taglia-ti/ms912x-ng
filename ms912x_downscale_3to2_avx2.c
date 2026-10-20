// SPDX-License-Identifier: GPL-2.0-only
#include <asm/fpu/api.h>

#define _MM_MALLOC_H_INCLUDED
#include <immintrin.h>
#undef _MM_MALLOC_H_INCLUDED

#include "ms912x_downscale_3to2.h"
#include "ms912x_downscale_3to2_sse.h"

static __always_inline __m256i
ms912x_cubic_avx_pairs(__m256i ac, __m256i bd)
{
	/* Each signed byte-pair sum fits s16; subtract the bias before adding. */
	__m256i near_outer = _mm256_maddubs_epi16(ac, _mm256_set1_epi16((short)0xf76f));
	__m256i center_outer = _mm256_maddubs_epi16(bd, _mm256_set1_epi16((short)0xfd1d));
	__m256i sum = _mm256_sub_epi16(near_outer, _mm256_set1_epi16(16384));

	sum = _mm256_add_epi16(sum, center_outer);
	sum = _mm256_srai_epi16(_mm256_add_epi16(sum, _mm256_set1_epi16(64)), 7);
	return _mm256_add_epi16(sum, _mm256_set1_epi16(128));
}

static __always_inline __m256i
ms912x_cubic_avx_filter(__m256i a, __m256i b, __m256i c, __m256i d)
{
	__m256i lo = ms912x_cubic_avx_pairs(_mm256_unpacklo_epi8(a, c),
					  _mm256_unpacklo_epi8(b, d));
	__m256i hi = ms912x_cubic_avx_pairs(_mm256_unpackhi_epi8(a, c),
					  _mm256_unpackhi_epi8(b, d));

	return _mm256_packus_epi16(lo, hi);
}

static __always_inline __m256i ms912x_cubic_avx_horizontal(const u32 *row)
{
	__m128i a0, b0, c0, d0, a1, b1, c1, d1;
	__m256i a, b, c, d;

	ms912x_cubic_sse_gather(row, &a0, &b0, &c0, &d0);
	ms912x_cubic_sse_gather(row + 6, &a1, &b1, &c1, &d1);
	a = _mm256_inserti128_si256(_mm256_castsi128_si256(a0), a1, 1);
	b = _mm256_inserti128_si256(_mm256_castsi128_si256(b0), b1, 1);
	c = _mm256_inserti128_si256(_mm256_castsi128_si256(c0), c1, 1);
	d = _mm256_inserti128_si256(_mm256_castsi128_si256(d0), d1, 1);
	return ms912x_cubic_avx_filter(a, b, c, d);
}

#define MS912X_CUBIC_SCOPE static noinline
#define MS912X_CUBIC_WORKER ms912x_downscale_avx2_fpu
#define MS912X_CUBIC_PIXELS 8
#define MS912X_CUBIC_TYPE __m256i
#define MS912X_CUBIC_LOAD(p) _mm256_loadu_si256((const __m256i *)(p))
#define MS912X_CUBIC_STORE(p, v) _mm256_storeu_si256((__m256i *)(p), (v))
#define MS912X_CUBIC_H(row, x, ox) ms912x_cubic_avx_horizontal((row) + (x) / 2 * 3 + (ox))
#define MS912X_CUBIC_FILTER ms912x_cubic_avx_filter
#define MS912X_CUBIC_FINISH _mm256_zeroupper
#include "ms912x_downscale_3to2_template.h"

#define MS912X_CUBIC_REGION ms912x_downscale_xrgb8888_3to2_avx2_region
#define MS912X_CUBIC_ENTRY ms912x_downscale_xrgb8888_3to2_avx2
#define MS912X_CUBIC_BEGIN kernel_fpu_begin
#define MS912X_CUBIC_END kernel_fpu_end
#include "ms912x_downscale_3to2_entry.h"
