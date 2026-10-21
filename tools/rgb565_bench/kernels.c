// SPDX-License-Identifier: GPL-2.0-only
/* Native packed candidates: no widening, narrowing or pixel shuffles. */
#include "bench.h"
#if defined(BACKEND_NEON)
#include <arm_neon.h>
#elif !defined(BACKEND_SCALAR_ROW)
#include <immintrin.h>
#endif
static inline void mask_row(u8 *out, const u16 *src, unsigned int width)
{
	unsigned int x = 0;
#ifdef BACKEND_AVX2
	__m256i mask = _mm256_set1_epi16(MS912X_RGB565_WIRE_MASK);
	for (; x + 16 <= width; x += 16)
		_mm256_storeu_si256(
			(__m256i *)(out + 2 * x),
			_mm256_and_si256(
				_mm256_loadu_si256((const __m256i *)(src + x)),
				mask));
#elif defined(BACKEND_SSE2)
	__m128i mask = _mm_set1_epi16(MS912X_RGB565_WIRE_MASK);
	for (; x + 8 <= width; x += 8)
		_mm_storeu_si128(
			(__m128i *)(out + 2 * x),
			_mm_and_si128(
				_mm_loadu_si128((const __m128i *)(src + x)),
				mask));
#elif defined(BACKEND_NEON)
	uint16x8_t mask = vdupq_n_u16(MS912X_RGB565_WIRE_MASK);
	for (; x + 8 <= width; x += 8)
		vst1q_u16((u16 *)(out + 2 * x),
			  vandq_u16(vld1q_u16(src + x), mask));
#endif
	for (; x < width; x++)
		put_unaligned_le16(src[x] & MS912X_RGB565_WIRE_MASK,
				   out + 2 * x);
}
#include "frame-template.h"
