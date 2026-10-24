// SPDX-License-Identifier: GPL-2.0-only
/* Linux ARM entry and an independent area oracle, with no target C library. */
#include "ms912x_downscale_5to4.h"

bool bench_simd_allowed = true, bench_neon_present = true;
unsigned int fpu_entries;
enum { STRIDE = 80, HEIGHT = 40 };
static u32 src[STRIDE * HEIGHT], out[STRIDE * HEIGHT], expected[STRIDE * HEIGHT];
static u32 work[STRIDE * HEIGHT], cropped[STRIDE * HEIGHT];
static unsigned int random_state = 0x912580;

void test_fail(unsigned int status)
{
    register unsigned int r0 __asm__("r0") = status;
    register unsigned int r7 __asm__("r7") = 1;
    __asm__ volatile("svc 0" : : "r"(r0), "r"(r7) : "memory");
    __builtin_unreachable();
}
void *memcpy(void *dst, const void *src, size_t n)
{ u8 *d = dst; const u8 *s = src; for (size_t i = 0; i < n; i++) d[i] = s[i]; return dst; }
void *memset(void *dst, int value, size_t n)
{ u8 *d = dst; for (size_t i = 0; i < n; i++) d[i] = value; return dst; }
int memcmp(const void *a, const void *b, size_t n)
{ const u8 *x = a, *y = b; for (size_t i = 0; i < n; i++) if (x[i] != y[i]) return x[i] - y[i]; return 0; }
int abs(int value) { return value < 0 ? -value : value; }
static u32 random32(void)
{ random_state ^= random_state << 13; random_state ^= random_state >> 17; random_state ^= random_state << 5; return random_state; }
static u32 reference(unsigned int sw, unsigned int sh, unsigned int x, unsigned int y)
{
    u32 result = 0;
    for (unsigned int shift = 0; shift < 32; shift += 8) {
        unsigned int sum = 0;
        for (unsigned int j = 0; j < 2; j++) for (unsigned int i = 0; i < 2; i++) {
            unsigned int wx = i ? x % 4 + 1 : 4 - x % 4;
            unsigned int wy = j ? y % 4 + 1 : 4 - y % 4;
            sum += wx * wy * ((src[min(y * 5 / 4 + j, sh - 1) * STRIDE +
                                       min(x * 5 / 4 + i, sw - 1)] >> shift) & 255);
        }
        result |= (sum + 12) / 25 << shift;
    }
    return result;
}
typedef void (*filter_fn)(const u32 *, u32 *, unsigned, unsigned, unsigned,
                          unsigned, unsigned, unsigned);
typedef void (*region_fn)(const u32 *, u32 *, unsigned, unsigned, unsigned,
                          unsigned, unsigned, unsigned, unsigned, unsigned);
/* Full-image test calls enter the same region functions as capture. */
#define ADAPT_REGION(name) \
static void run_##name(const u32 *s,u32 *d,unsigned sw,unsigned sh,unsigned dw, \
    unsigned dh,unsigned ss,unsigned ds) { \
    ms912x_downscale_xrgb8888_5to4_##name##_region(s,d,sw,sh,dw,dh,ss,ds,0,0); \
}
ADAPT_REGION(scalar)
#ifdef CONFIG_X86_64
ADAPT_REGION(sse2)
ADAPT_REGION(ssse3)
ADAPT_REGION(avx2)
#endif
#if MS912X_HAS_NEON
ADAPT_REGION(neon)
#endif
static const filter_fn filters[] = {
    run_scalar,
#if MS912X_HAS_NEON
    run_neon,
#endif
    ms912x_downscale_xrgb8888_5to4,
};
static const region_fn regions[] = {
    ms912x_downscale_xrgb8888_5to4_scalar_region,
#if MS912X_HAS_NEON
    ms912x_downscale_xrgb8888_5to4_neon_region,
#endif
    ms912x_downscale_xrgb8888_5to4_region,
};

void _start(void)
{
    const unsigned int heights[] = {1, 4, 5, 17, 33};
    for (unsigned int sw = 1; sw <= 65; sw++)
    for (unsigned int h = 0; h < ARRAY_SIZE(heights); h++) {
        unsigned int sh = heights[h], dw = ((4 * sw + 4) / 5 + (h & 1) + 3) & ~3U;
        unsigned int dh = (4 * sh + 4) / 5;
        for (unsigned int i = 0; i < ARRAY_SIZE(src); i++) src[i] = random32();
        for (unsigned int y = 0; y < dh; y++) for (unsigned int x = 0; x < dw; x++)
            expected[y * STRIDE + x] = reference(sw, sh, x, y);
        for (unsigned int f = 0; f < ARRAY_SIZE(filters); f++) {
            for (unsigned int layout = 0; layout < 3; layout++) {
                unsigned int ds = layout == 2 ? dw : STRIDE;
                u32 *dst = layout ? work : out;
                memcpy(work, src, sizeof(src));
                filters[f](layout ? work : src, dst, sw, sh, dw, dh, STRIDE, ds);
                for (unsigned int y = 0; y < dh; y++) for (unsigned int x = 0; x < dw; x++)
                    if (dst[y * ds + x] != expected[y * STRIDE + x]) test_fail(10 + f);
            }
            for (unsigned int y1 = 0; y1 < dh; y1 += 4)
            for (unsigned int x1 = 0; x1 < dw; x1 += 4) {
                unsigned int cx = x1 / 4 * 5, cy = y1 / 4 * 5;
                /* Extra synthetic padding can place a crop beyond the input. */
                if (cx >= sw || cy >= sh) continue;
                unsigned int sx = cx ? cx - 1 : 0, sy = cy ? cy - 1 : 0;
                unsigned int cw = sw - sx, ch = sh - sy;
                for (unsigned int y = 0; y < ch; y++)
                    memcpy(cropped + y * STRIDE, src + (sy + y) * STRIDE + sx, cw * 4);
                regions[f](cropped, out, cw, ch, dw - x1, dh - y1,
                           STRIDE, STRIDE, cx - sx, cy - sy);
                for (unsigned int y = 0; y < dh - y1; y++) for (unsigned int x = 0; x < dw - x1; x++)
                    if (out[y * STRIDE + x] != expected[(y1 + y) * STRIDE + x1 + x]) test_fail(20 + f);
            }
        }
    }
#if MS912X_HAS_NEON
    unsigned int before = fpu_entries;
    bench_neon_present = false;
    run_neon(src, out, 65, 33, 52, 27, STRIDE, STRIDE);
    bench_neon_present = true; bench_simd_allowed = false;
    run_neon(src, out, 65, 33, 52, 27, STRIDE, STRIDE);
    if (fpu_entries != before) test_fail(40);
    bench_simd_allowed = true;
#endif
    for (unsigned int f = 0; f < ARRAY_SIZE(filters); f++)
        for (unsigned int i = 0; i < 4; i++) {
            unsigned int dims[] = {1, 1, 1, 1}; dims[i] = 0;
            filters[f](NULL, NULL, dims[0], dims[1], dims[2], dims[3], 1, 1);
        }
    test_fail(0);
}
