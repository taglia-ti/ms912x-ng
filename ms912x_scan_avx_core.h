/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MS912X_SCAN_AVX_CORE_H
#define MS912X_SCAN_AVX_CORE_H
#define _MM_MALLOC_H_INCLUDED
#include <immintrin.h>
#undef _MM_MALLOC_H_INCLUDED
#include "ms912x_scan_methods.h"
#ifdef __KERNEL__
#define MS912X_SCAN_AVX_TARGET
#else
#define MS912X_SCAN_AVX_TARGET __attribute__((target("avx")))
#endif

static MS912X_SCAN_AVX_TARGET inline __attribute__((always_inline)) __m256i
ms912x_scan_avx_difference(const u32 *old, const u32 *new)
{
	/* These are bitwise operations; the casts perform no numeric conversion. */
	return _mm256_castps_si256(_mm256_xor_ps(
		_mm256_castsi256_ps(_mm256_loadu_si256((const __m256i *)old)),
		_mm256_castsi256_ps(_mm256_loadu_si256((const __m256i *)new))));
}

static MS912X_SCAN_AVX_TARGET inline __attribute__((always_inline)) __m256i ms912x_scan_avx_or(__m256i a, __m256i b)
{
	return _mm256_castps_si256(_mm256_or_ps(_mm256_castsi256_ps(a),
					       _mm256_castsi256_ps(b)));
}

#define MS912X_MASKED_ATTR MS912X_SCAN_AVX_TARGET
#define MS912X_MASKED_TYPE __m256i
#define MS912X_MASKED_PIXELS 8
#define MS912X_MASKED_ZERO() _mm256_setzero_si256()
#define MS912X_MASKED_OR(a, b) ms912x_scan_avx_or(a, b)
#define MS912X_MASKED_DIFF(a, b) ms912x_scan_avx_difference(a, b)
#define MS912X_MASKED_CHANGED(diff, mask) (!_mm256_testz_si256(diff, _mm256_set1_epi32(mask)))
#define MS912X_MASKED_COPY(dst, src) \
	_mm256_storeu_si256((__m256i *)(dst), _mm256_loadu_si256((const __m256i *)(src)))
#define MS912X_MASKED_BLOCK ms912x_scan_avx_masked_block
#define MS912X_MASKED_PAIR ms912x_scan_avx_masked_pair
#define MS912X_MASKED_ROW ms912x_scan_row_avx_masked
#define MS912X_MASKED_PREFIX ms912x_scan_avx_masked_prefix
#include "ms912x_scan_masked_template.h"

#undef MS912X_SCAN_AVX_TARGET
#endif
