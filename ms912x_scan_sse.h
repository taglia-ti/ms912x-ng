/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MS912X_SCAN_SSE_H
#define MS912X_SCAN_SSE_H

#define _MM_MALLOC_H_INCLUDED
#include <emmintrin.h>
#undef _MM_MALLOC_H_INCLUDED

#include "ms912x_scan_methods.h"

static inline __attribute__((always_inline)) unsigned int ms912x_scan_sse_nonzero(__m128i diff)
{
	return _mm_movemask_ps(_mm_castsi128_ps(_mm_cmpeq_epi32(diff, _mm_setzero_si128()))) != 0xf;
}

static inline __attribute__((always_inline)) __m128i ms912x_scan_sse_difference(const u32 *old, const u32 *new)
{
	return _mm_xor_si128(_mm_loadu_si128((const __m128i *)old),
			     _mm_loadu_si128((const __m128i *)new));
}

#define MS912X_MASKED_TYPE __m128i
#define MS912X_MASKED_PIXELS 4
#define MS912X_MASKED_ZERO() _mm_setzero_si128()
#define MS912X_MASKED_OR(a, b) _mm_or_si128(a, b)
#define MS912X_MASKED_DIFF(a, b) ms912x_scan_sse_difference(a, b)
#define MS912X_MASKED_CHANGED(diff, mask) \
	ms912x_scan_sse_nonzero(_mm_and_si128(diff, _mm_set1_epi32(mask)))
#define MS912X_MASKED_COPY(dst, src) \
	_mm_storeu_si128((__m128i *)(dst), _mm_loadu_si128((const __m128i *)(src)))
#define MS912X_MASKED_BLOCK ms912x_scan_sse_masked_block
#define MS912X_MASKED_ROW ms912x_scan_row_sse_masked
#define MS912X_MASKED_PREFIX ms912x_scan_sse_masked_prefix
#include "ms912x_scan_masked_template.h"

#endif /* MS912X_SCAN_SSE_H */
