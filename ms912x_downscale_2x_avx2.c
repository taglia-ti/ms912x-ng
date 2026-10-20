// SPDX-License-Identifier: GPL-2.0-only

#include <asm/fpu/api.h>

/* Keep the kernel AVX path independent of userspace allocation helpers. */
#define _MM_MALLOC_H_INCLUDED
#include <immintrin.h>
#undef _MM_MALLOC_H_INCLUDED

#include <linux/kernel.h>

#include "ms912x_downscale_2x.h"

#include "ms912x_downscale_2x_sse.h"

static __always_inline __m256i
ms912x_downscale_avx2_eight(const u32 *row0, const u32 *row1)
{
	__m256 r00 = _mm256_castsi256_ps(_mm256_loadu_si256((const __m256i *)row0));
	__m256 r01 = _mm256_castsi256_ps(_mm256_loadu_si256((const __m256i *)(row0 + 8)));
	__m256 r10 = _mm256_castsi256_ps(_mm256_loadu_si256((const __m256i *)row1));
	__m256 r11 = _mm256_castsi256_ps(_mm256_loadu_si256((const __m256i *)(row1 + 8)));
	__m256i even0 = _mm256_castps_si256(_mm256_shuffle_ps(r00, r01, _MM_SHUFFLE(2, 0, 2, 0)));
	__m256i odd0 = _mm256_castps_si256(_mm256_shuffle_ps(r00, r01, _MM_SHUFFLE(3, 1, 3, 1)));
	__m256i even1 = _mm256_castps_si256(_mm256_shuffle_ps(r10, r11, _MM_SHUFFLE(2, 0, 2, 0)));
	__m256i odd1 = _mm256_castps_si256(_mm256_shuffle_ps(r10, r11, _MM_SHUFFLE(3, 1, 3, 1)));
	__m256i ab = _mm256_avg_epu8(even0, even1);
	__m256i cd = _mm256_avg_epu8(odd0, odd1);
	__m256i parity = _mm256_or_si256(_mm256_xor_si256(even0, even1),
					_mm256_xor_si256(odd0, odd1));
	__m256i correction = _mm256_and_si256(parity, _mm256_xor_si256(ab, cd));
	__m256i average;

	correction = _mm256_and_si256(correction, _mm256_set1_epi8(1));
	average = _mm256_sub_epi8(_mm256_avg_epu8(ab, cd), correction);
	/* Lane-local shuffles produce output pairs 0,2,1,3; restore raster order. */
	return _mm256_permute4x64_epi64(average, _MM_SHUFFLE(3, 1, 2, 0));
}

static noinline void
ms912x_downscale_avx2_fpu(const u32 *src, u32 *dst,
		unsigned int src_width, unsigned int src_height, unsigned int dst_width,
		unsigned int src_stride, unsigned int dst_stride,
		unsigned int first_row, unsigned int end_row)
{
	unsigned int pairs = min(dst_width, src_width / 2);
	unsigned int vector_width = pairs & ~7U;
	unsigned int y, x;

	for (y = first_row; y < end_row; y++) {
		unsigned int sy0 = min(2 * y, src_height - 1);
		unsigned int sy1 = min(2 * y + 1, src_height - 1);
		const u32 *row0 = src + sy0 * src_stride;
		const u32 *row1 = src + sy1 * src_stride;
		u32 *out = dst + y * dst_stride;

		for (x = 0; x < vector_width; x += 8) {
			__m256i average = ms912x_downscale_avx2_eight(row0 + 2 * x, row1 + 2 * x);

			_mm256_storeu_si256((__m256i *)(out + x), average);
		}
		if (x + 4 <= pairs) {
			_mm_storeu_si128((__m128i *)(out + x),
				ms912x_downscale_sse_four(row0 + 2 * x, row1 + 2 * x));
			x += 4;
		}
		if (x + 2 <= pairs) {
			_mm_storel_epi64((__m128i *)(out + x),
				ms912x_downscale_sse_two(row0 + 2 * x, row1 + 2 * x));
			x += 2;
		}
		for (; x < dst_width; x++) {
			unsigned int sx0 = min(2 * x, src_width - 1);
			unsigned int sx1 = min(2 * x + 1, src_width - 1);

			out[x] = ms912x_downscale_avg4(row0[sx0], row0[sx1], row1[sx0], row1[sx1]);
		}
	}
	_mm256_zeroupper();
}

void ms912x_downscale_xrgb8888_2x_avx2(const u32 *src, u32 *dst,
		unsigned int src_width, unsigned int src_height,
		unsigned int dst_width, unsigned int dst_height,
		unsigned int src_stride, unsigned int dst_stride)
{
	unsigned int row = 0;

	if (!src_width || !src_height || !dst_width || !dst_height)
		return;
	while (row < dst_height) {
		unsigned int end = min(row + MS912X_DOWNSCALE_ROWS_PER_FPU, dst_height);

		kernel_fpu_begin();
		ms912x_downscale_avx2_fpu(src, dst, src_width, src_height, dst_width,
					 src_stride, dst_stride, row, end);
		kernel_fpu_end();
		row = end;
	}
}
