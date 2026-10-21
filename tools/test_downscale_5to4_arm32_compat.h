/* SPDX-License-Identifier: GPL-2.0-only */
/* Freestanding shims: validate ARM32 without depending on a target libc. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MS912X_H
#define noinline __attribute__((noinline))
#define __always_inline inline __attribute__((always_inline))
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#define min(a, b) ({ __typeof__(a) _a = (a); __typeof__(b) _b = (b); _a < _b ? _a : _b; })
#define max(a, b) ({ __typeof__(a) _a = (a); __typeof__(b) _b = (b); _a > _b ? _a : _b; })
#define max3(a, b, c) max(max(a, b), c)
#define clamp(v, lo, hi) min(max(v, lo), hi)
#define unlikely(v) (v)
#define __ffs(v) ((unsigned int)__builtin_ctz(v))
#define fls(v) ((v) ? 32 - __builtin_clz(v) : 0)
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef uint32_t __le32;

void *memcpy(void *dst, const void *src, size_t n);
void *memset(void *dst, int value, size_t n);
int memcmp(const void *a, const void *b, size_t n);
int abs(int value);
void test_fail(unsigned int status) __attribute__((noreturn));
extern bool bench_simd_allowed, bench_neon_present;
extern unsigned int fpu_entries;
static inline bool cpu_has_neon(void) { return bench_neon_present; }
static inline bool may_use_simd(void) { return bench_simd_allowed; }
static inline void kernel_fpu_begin(void)
{
    if (!bench_simd_allowed || !bench_neon_present) test_fail(50);
    fpu_entries++;
    __asm__ volatile("" ::: "memory");
}
static inline void kernel_fpu_end(void) { __asm__ volatile("" ::: "memory"); }
