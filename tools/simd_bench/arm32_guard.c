/* SPDX-License-Identifier: GPL-2.0-only */
/* Freestanding: the cross toolchain's libc itself can require NEON. */
#include "ms912x_neon.h"
#include "ms912x_scan_methods.h"

bool bench_simd_allowed = true;
bool bench_neon_present;
extern bool ms912x_convert_xrgb8888_to_uyvy_neon(void *, const __le32 *,
                                               const struct drm_rect *);

static __attribute__((noreturn)) void finish(unsigned status)
{
    register unsigned r0 __asm__("r0") = status;
    register unsigned r7 __asm__("r7") = 1; /* ARM Linux exit */

    __asm__ volatile("svc 0" : : "r"(r0), "r"(r7) : "memory");
    __builtin_unreachable();
}

/* Any call into a worker while SIMD is unavailable fails the check. */
#pragma GCC diagnostic ignored "-Wunused-parameter"
void ms912x_neon_convert_rows(void *dst, const __le32 *src,
                              const struct drm_rect *rect, unsigned first, unsigned end)
{
    finish(3);
}
void ms912x_scan_rows_neon_blocks_fpu(const u32 *old, const u32 *new, unsigned width,
                                     unsigned stride, unsigned rows,
                                     struct ms912x_scan_row_result *results)
{
    finish(4);
}
void ms912x_scan_rows_neon_relaxed_fpu(const u32 *old, const u32 *new, u32 *next,
                                      unsigned width, unsigned stride, unsigned rows,
                                      unsigned tolerance,
                                      struct ms912x_scan_row_result *results)
{
    finish(5);
}

void _start(void)
{
    struct drm_rect rect = {0, 0, 2, 1};

    if (ms912x_scan_neon_available() ||
        ms912x_convert_xrgb8888_to_uyvy_neon(NULL, NULL, &rect))
        finish(1);
    bench_neon_present = true;
    bench_simd_allowed = false;
    if (ms912x_scan_neon_available() ||
        ms912x_convert_xrgb8888_to_uyvy_neon(NULL, NULL, &rect))
        finish(2);
    finish(0);
}
