/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SIMD_COMPAT_H
#define SIMD_COMPAT_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#undef __always_inline
#define __always_inline inline __attribute__((always_inline))
#define unlikely(x) __builtin_expect(!!(x), 0)
#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#define noinline __attribute__((noinline))
#define MS912X_H
#define MS912X_MAX_WIDTH 1920
#define READ_ONCE(x) (x)
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define min(a, b)                                                                                  \
    ({                                                                                             \
        __typeof__(a) _a = (a);                                                                    \
        __typeof__(b) _b = (b);                                                                    \
        _a < _b ? _a : _b;                                                                         \
    })
#define max(a, b)                                                                                  \
    ({                                                                                             \
        __typeof__(a) _a = (a);                                                                    \
        __typeof__(b) _b = (b);                                                                    \
        _a > _b ? _a : _b;                                                                         \
    })
#define max3(a, b, c) max(max(a, b), c)
#define __ffs(x) ((unsigned long)__builtin_ctzl((unsigned long)(x)))
#define fls(x) ((x) ? 32 - __builtin_clz((unsigned int)(x)) : 0)
#define hweight8(x) __builtin_popcount((unsigned int)(x) & 255)
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef uint32_t __le32;
struct drm_rect {
    int x1, y1, x2, y2;
};
static inline int drm_rect_width(const struct drm_rect *r) { return r->x2 - r->x1; }
static inline int drm_rect_height(const struct drm_rect *r) { return r->y2 - r->y1; }
bool ms912x_convert_xrgb8888_to_uyvy_sse2(void *, const __le32 *, const struct drm_rect *);
extern bool bench_simd_allowed;
extern bool bench_neon_present;
static inline bool cpu_has_neon(void) { return bench_neon_present; }
static inline bool may_use_simd(void) { return bench_simd_allowed; }
static inline void kernel_fpu_begin(void) { __asm__ volatile("" ::: "memory"); }
static inline void kernel_fpu_end(void) { __asm__ volatile("" ::: "memory"); }
static inline void kernel_neon_begin(void) { __asm__ volatile("" ::: "memory"); }
static inline void kernel_neon_end(void) { __asm__ volatile("" ::: "memory"); }
static inline u32 le32_to_cpup(const __le32 *p) {
    u32 v;
    memcpy(&v, p, 4);
    return v;
}
static inline void put_unaligned_le32(u32 v, void *p) { memcpy(p, &v, 4); }
static inline void put_unaligned_le64(u64 v, void *p) { memcpy(p, &v, 8); }
#ifndef clamp
#define clamp(v, lo, hi) min(max((v), (lo)), (hi))
#endif
#endif
