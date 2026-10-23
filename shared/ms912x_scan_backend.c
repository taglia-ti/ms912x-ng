/* SPDX-License-Identifier: GPL-2.0-only */
#include "ms912x_scan_backend.h"
#if defined(__SSE2__)
#include "../ms912x_scan_sse.h"
#include "../ms912x_scan_avx_core.h"
static __attribute__((target("avx"))) void
scan_avx(const u32 *old, const u32 *new, unsigned int width, u32 mask,
         struct ms912x_scan_row_result *result)
{
    ms912x_scan_row_avx_masked(old, new, NULL, width, mask, result);
    _mm256_zeroupper();
}
#endif
#if defined(__ARM_NEON) || defined(__aarch64__)
#include "../ms912x_scan_neon_core.h"
#endif
int ms912x_core_scan_backend_supported(enum ms912x_scan_backend backend)
{
    switch (backend) {
    case MS912X_SCAN_AUTO: case MS912X_SCAN_SCALAR: return 1;
#if defined(__SSE2__)
    case MS912X_SCAN_SSE2: return 1;
    case MS912X_SCAN_AVX: return __builtin_cpu_supports("avx") != 0;
#endif
#if defined(__ARM_NEON) || defined(__aarch64__)
    case MS912X_SCAN_NEON: return 1;
#endif
    default: return 0;
    }
}
enum ms912x_scan_backend ms912x_core_scan_default_backend(void)
{
#if defined(__SSE2__)
    if (ms912x_core_scan_backend_supported(MS912X_SCAN_AVX)) return MS912X_SCAN_AVX;
    return MS912X_SCAN_SSE2;
#elif defined(__ARM_NEON) || defined(__aarch64__)
    return MS912X_SCAN_NEON;
#else
    return MS912X_SCAN_SCALAR;
#endif
}
void ms912x_core_scan_row(const u32 *old, const u32 *new, unsigned int width,
                         u32 mask, struct ms912x_scan_row_result *result,
                         enum ms912x_scan_backend backend)
{
    if (backend == MS912X_SCAN_AUTO) backend = ms912x_core_scan_default_backend();
    switch (backend) {
#if defined(__SSE2__)
    case MS912X_SCAN_AVX:
        if (ms912x_core_scan_backend_supported(backend)) {
            scan_avx(old, new, width, mask, result); return;
        }
        break;
    case MS912X_SCAN_SSE2:
        ms912x_scan_row_sse_masked(old, new, NULL, width, mask, result); return;
#endif
#if defined(__ARM_NEON) || defined(__aarch64__)
    case MS912X_SCAN_NEON:
        ms912x_scan_row_neon_masked(old, new, NULL, width, mask, result); return;
#endif
    default: break;
    }
    ms912x_scan_row_scalar_masked(old, new, NULL, width, mask, result);
}
