/* SPDX-License-Identifier: GPL-2.0-only */
#include "ms912x_scan_backend.h"

#if !defined(__KERNEL__) && defined(__SSE2__)
#include "../ms912x_scan_sse.h"
#include "../ms912x_scan_avx_core.h"
static __attribute__((target("avx"))) void
scan_rows_avx(const u32 *old, const u32 *new, u32 *next,
	      unsigned int width, size_t stride, unsigned int count, u32 mask,
	      struct ms912x_scan_row_result *results)
{
	unsigned int row;

	for (row = 0; row < count; row++)
		ms912x_scan_row_avx_masked(old + row * stride, new + row * stride,
			next ? next + row * stride : NULL, width, mask, &results[row]);
	_mm256_zeroupper();
}
#endif
#if !defined(__KERNEL__) && (defined(__ARM_NEON) || defined(__aarch64__))
#include "../ms912x_scan_neon_core.h"
#endif

int ms912x_core_scan_backend_supported(enum ms912x_scan_backend backend)
{
	switch (backend) {
	case MS912X_SCAN_AUTO:
	case MS912X_SCAN_SCALAR:
	case MS912X_SCAN_MEMCMP:
		return 1;
#ifdef __KERNEL__
	case MS912X_SCAN_AVX: return ms912x_scan_avx_available();
	case MS912X_SCAN_SSE2: return ms912x_scan_sse2_available();
	case MS912X_SCAN_NEON: return ms912x_scan_neon_available();
#else
#if defined(__SSE2__)
	case MS912X_SCAN_SSE2: return 1;
	case MS912X_SCAN_AVX: return __builtin_cpu_supports("avx") != 0;
#endif
#if defined(__ARM_NEON) || defined(__aarch64__)
	case MS912X_SCAN_NEON: return 1;
#endif
#endif
	default: return 0;
	}
}

enum ms912x_scan_backend ms912x_core_scan_default_backend(void)
{
	if (ms912x_core_scan_backend_supported(MS912X_SCAN_AVX))
		return MS912X_SCAN_AVX;
	if (ms912x_core_scan_backend_supported(MS912X_SCAN_NEON))
		return MS912X_SCAN_NEON;
	if (ms912x_core_scan_backend_supported(MS912X_SCAN_SSE2))
		return MS912X_SCAN_SSE2;
	return MS912X_SCAN_SCALAR;
}

enum ms912x_scan_backend ms912x_core_scan_resolve_backend(enum ms912x_scan_backend backend)
{
	if (backend == MS912X_SCAN_AUTO)
		return ms912x_core_scan_default_backend();
	return ms912x_core_scan_backend_supported(backend) ? backend : MS912X_SCAN_SCALAR;
}

u32 ms912x_core_scan_backend_mask(enum ms912x_scan_backend backend, u32 mask)
{
	return backend == MS912X_SCAN_MEMCMP ? ~0U : mask;
}

unsigned int ms912x_core_scan_run_words(enum ms912x_scan_backend backend,
				      unsigned int pixel_shift)
{
	if (backend == MS912X_SCAN_MEMCMP)
		return MS912X_SCAN_MEMCMP_BLOCK_BYTES / sizeof(u32);
	return pixel_shift ? MS912X_SCAN_UYVY_RUN_WORDS : 1;
}

void ms912x_core_scan_rows(const u32 *old, const u32 *new, u32 *next,
			  unsigned int width, size_t row_stride,
			  unsigned int row_count, u32 mask,
			  struct ms912x_scan_row_result *results,
			  enum ms912x_scan_backend backend)
{
	unsigned int row;

	backend = ms912x_core_scan_resolve_backend(backend);
	/* Bound each kernel FPU section, including calls outside the damage worker. */
	while (row_count) {
		unsigned int count = min(row_count, (unsigned int)MS912X_SCAN_SIMD_ROWS_PER_BATCH);
		switch (backend) {
#if defined(__KERNEL__) && defined(CONFIG_X86_64)
		case MS912X_SCAN_AVX:
			ms912x_scan_rows_avx_relaxed_masked(old, new, next, width,
				row_stride, count, mask, results);
			break;
		case MS912X_SCAN_SSE2:
			ms912x_scan_rows_sse2_relaxed_masked(old, new, next, width,
				row_stride, count, mask, results);
			break;
#elif defined(__KERNEL__) && MS912X_HAS_NEON
		case MS912X_SCAN_NEON:
			ms912x_scan_rows_neon_relaxed_masked(old, new, next, width,
				row_stride, count, mask, results);
			break;
#elif !defined(__KERNEL__) && defined(__SSE2__)
		case MS912X_SCAN_AVX:
			scan_rows_avx(old, new, next, width, row_stride, count, mask, results);
			break;
		case MS912X_SCAN_SSE2:
			for (row = 0; row < count; row++)
				ms912x_scan_row_sse_masked(old + row * row_stride, new + row * row_stride,
					next ? next + row * row_stride : NULL, width, mask, &results[row]);
			break;
#elif !defined(__KERNEL__) && (defined(__ARM_NEON) || defined(__aarch64__))
		case MS912X_SCAN_NEON:
			for (row = 0; row < count; row++)
				ms912x_scan_row_neon_masked(old + row * row_stride, new + row * row_stride,
					next ? next + row * row_stride : NULL, width, mask, &results[row]);
			break;
#endif
		default:
			for (row = 0; row < count; row++) {
				const u32 *a = old + row * row_stride, *b = new + row * row_stride;
				u32 *dst = next ? next + row * row_stride : NULL;
				if (backend == MS912X_SCAN_MEMCMP) {
					ms912x_scan_row_memcmp(a, b, width, &results[row]);
					if (dst) memcpy(dst, b, width * sizeof(*dst));
				} else {
					ms912x_scan_row_scalar_masked(a, b, dst, width, mask, &results[row]);
				}
			}
			break;
		}
		row_count -= count;
		/* Do not form a pointer beyond the last row of a minimally sized buffer. */
		if (!row_count) break;
		old += count * row_stride;
		new += count * row_stride;
		if (next) next += count * row_stride;
		results += count;
	}
}

void ms912x_core_scan_row(const u32 *old, const u32 *new, unsigned int width,
			 u32 mask, struct ms912x_scan_row_result *result,
			 enum ms912x_scan_backend backend)
{
	ms912x_core_scan_rows(old, new, NULL, width, width, 1, mask, result, backend);
}
