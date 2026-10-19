/* SPDX-License-Identifier: GPL-2.0-only */
#include <stdio.h>
#include <assert.h>
#include <time.h>
#include <sched.h>
#include <sys/mman.h>
#include <unistd.h>
#ifdef CONFIG_ARM
#include <sys/auxv.h>
#include <asm/hwcap.h>
#endif
#include "ms912x_scan_methods.h"
#include "ms912x_color.h"
#include "ms912x_cursor.h"
#include "ms912x_mode3.h"

bool bench_simd_allowed = true;
bool bench_neon_present = true;
extern void dispatch_tests(void);

typedef void (*block_fn)(const u32 *, const u32 *, unsigned, unsigned, unsigned,
                         struct ms912x_scan_row_result *);
typedef void (*relax_fn)(const u32 *, const u32 *, u32 *, unsigned, unsigned, unsigned, unsigned,
                         struct ms912x_scan_row_result *);
typedef void (*convert_fn)(void *, const __le32 *, const struct drm_rect *);
#define DECL_SCAN(b)                                                                               \
    extern void ms912x_scan_rows_##b##_blocks(const u32 *, const u32 *, unsigned, unsigned,        \
                                              unsigned, struct ms912x_scan_row_result *);          \
    extern void old_ms912x_scan_rows_##b##_blocks(const u32 *, const u32 *, unsigned, unsigned,    \
                                                  unsigned, struct ms912x_scan_row_result *);      \
    extern void ms912x_scan_rows_##b##_relaxed(const u32 *, const u32 *, u32 *, unsigned,          \
                                               unsigned, unsigned, unsigned,                       \
                                               struct ms912x_scan_row_result *);                   \
    extern void old_ms912x_scan_rows_##b##_relaxed(const u32 *, const u32 *, u32 *, unsigned,      \
                                                   unsigned, unsigned, unsigned,                   \
                                                   struct ms912x_scan_row_result *);
struct backend {
    const char *name;
    int block_size;
    block_fn before_block, after_block;
    relax_fn before_relax, after_relax;
    convert_fn before_convert, after_convert;
};
extern void baseline_memcmp(const u32 *, const u32 *, unsigned, unsigned, unsigned,
                            struct ms912x_scan_row_result *);
extern void current_memcmp(const u32 *, const u32 *, unsigned, unsigned, unsigned,
                           struct ms912x_scan_row_result *);
extern void scalar_blocks(const u32 *, const u32 *, unsigned, unsigned, unsigned,
                          struct ms912x_scan_row_result *);
extern void scalar_relaxed(const u32 *, const u32 *, u32 *, unsigned, unsigned, unsigned, unsigned,
                           struct ms912x_scan_row_result *);
extern void scalar_convert(void *, const __le32 *, const struct drm_rect *);
#ifdef CONFIG_X86_64
DECL_SCAN(avx2)
DECL_SCAN(sse2)
extern void ms912x_scan_rows_sse41_blocks(const u32 *, const u32 *, unsigned, unsigned, unsigned,
                                          struct ms912x_scan_row_result *);
extern void ms912x_scan_rows_sse41_relaxed(const u32 *, const u32 *, u32 *, unsigned, unsigned,
                                           unsigned, unsigned, struct ms912x_scan_row_result *);
extern void ms912x_convert_xrgb8888_to_uyvy_avx2(void *, const __le32 *, const struct drm_rect *);
extern void old_ms912x_convert_xrgb8888_to_uyvy_avx2(void *, const __le32 *,
                                                     const struct drm_rect *);
extern bool ms912x_convert_xrgb8888_to_uyvy_sse2(void *, const __le32 *, const struct drm_rect *);
extern bool old_ms912x_convert_xrgb8888_to_uyvy_sse2(void *, const __le32 *,
                                                     const struct drm_rect *);
extern bool ms912x_convert_xrgb8888_to_uyvy_ssse3(void *, const __le32 *, const struct drm_rect *);
static void new_sse(void *d, const __le32 *s, const struct drm_rect *r) {
    assert(ms912x_convert_xrgb8888_to_uyvy_sse2(d, s, r));
}
static void old_sse(void *d, const __le32 *s, const struct drm_rect *r) {
    assert(old_ms912x_convert_xrgb8888_to_uyvy_sse2(d, s, r));
}
static void ssse3(void *d, const __le32 *s, const struct drm_rect *r) {
    assert(ms912x_convert_xrgb8888_to_uyvy_ssse3(d, s, r));
}
static struct backend backends[] = {
    {"AVX2", 64, old_ms912x_scan_rows_avx2_blocks, ms912x_scan_rows_avx2_blocks,
     old_ms912x_scan_rows_avx2_relaxed, ms912x_scan_rows_avx2_relaxed,
     old_ms912x_convert_xrgb8888_to_uyvy_avx2, ms912x_convert_xrgb8888_to_uyvy_avx2},
    {"SSE2", 16, old_ms912x_scan_rows_sse2_blocks, ms912x_scan_rows_sse2_blocks,
     old_ms912x_scan_rows_sse2_relaxed, ms912x_scan_rows_sse2_relaxed, old_sse, new_sse},
    {"SSSE3", 0, NULL, NULL, NULL, NULL, old_sse, ssse3},
    {"SSE4.1", 16, old_ms912x_scan_rows_sse2_blocks, ms912x_scan_rows_sse41_blocks,
     old_ms912x_scan_rows_sse2_relaxed, ms912x_scan_rows_sse41_relaxed, NULL, NULL},
    {"SSE4.1-vs-SSE2", 16, ms912x_scan_rows_sse2_blocks, ms912x_scan_rows_sse41_blocks,
     ms912x_scan_rows_sse2_relaxed, ms912x_scan_rows_sse41_relaxed, NULL, NULL},
#else
DECL_SCAN(neon)
extern bool ms912x_convert_xrgb8888_to_uyvy_neon(void *, const __le32 *, const struct drm_rect *);
extern bool old_ms912x_convert_xrgb8888_to_uyvy_neon(void *, const __le32 *,
                                                     const struct drm_rect *);
static void new_neon(void *d, const __le32 *s, const struct drm_rect *r) {
    assert(ms912x_convert_xrgb8888_to_uyvy_neon(d, s, r));
}
static void old_neon(void *d, const __le32 *s, const struct drm_rect *r) {
    assert(old_ms912x_convert_xrgb8888_to_uyvy_neon(d, s, r));
}
static struct backend backends[] = {
    {"NEON", 16, old_ms912x_scan_rows_neon_blocks, ms912x_scan_rows_neon_blocks,
     old_ms912x_scan_rows_neon_relaxed, ms912x_scan_rows_neon_relaxed, old_neon, new_neon},
#endif
    {"scalar", 0, scalar_blocks, scalar_blocks, scalar_relaxed, scalar_relaxed, scalar_convert,
     scalar_convert},
    {"memcmp", -32, baseline_memcmp, current_memcmp, NULL, NULL, NULL, NULL}};
static bool available(struct backend *b) {
    (void)b;
#ifdef CONFIG_X86_64
    if (!strcmp(b->name, "AVX2"))
        return __builtin_cpu_supports("avx2");
    if (!strcmp(b->name, "SSSE3"))
        return __builtin_cpu_supports("ssse3");
    if (!strncmp(b->name, "SSE4.1", 6))
        return __builtin_cpu_supports("sse4.1");
#endif
#ifdef CONFIG_ARM
    if (!strcmp(b->name, "NEON"))
        return !!(getauxval(AT_HWCAP) & HWCAP_NEON);
#endif
    return true;
}

static u32 random_state = 0xf314ac23;
static u32 random32(void) {
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    return random_state ^= random_state << 5;
}
static void fail(const char *what, struct backend *b, unsigned w, unsigned t, unsigned p) {
    fprintf(stderr, "FAIL %s %s width=%u tolerance=%u pattern=%u\n", b->name, what, w, t, p);
    exit(1);
}
static bool changed_ref(u32 a, u32 b, unsigned t) {
    for (unsigned c = 0; c < 24; c += 8)
        if (abs((int)((a >> c) & 255) - (int)((b >> c) & 255)) > (int)t)
            return true;
    return false;
}
static void scan_run_tests(void) {
    u32 old[40] = {0}, cur[40] = {0};
    unsigned cursor, first, end;

    cur[2] = 0x00010101;
    cur[5] = 0xff000000;
    cur[10] = 0x00040404;
    cursor = 0;
    assert(ms912x_scan_row_next_run(old, cur, 40, 1, 0, &cursor, &first, &end));
    assert(first == 2 && end == 3);
    assert(ms912x_scan_row_next_run(old, cur, 40, 1, 0, &cursor, &first, &end));
    assert(first == 10 && end == 11);
    assert(!ms912x_scan_row_next_run(old, cur, 40, 1, 0, &cursor, &first, &end));

    memset(cur, 0, sizeof(cur));
    cur[4] = 0x00020202;
    cur[7] = 0x00050505;
    cursor = 0;
    assert(ms912x_scan_row_next_run(old, cur, 40, 1, 2, &cursor, &first, &end));
    assert(first == 7 && end == 8);
    assert(!ms912x_scan_row_next_run(old, cur, 40, 1, 2, &cursor, &first, &end));

    memset(cur, 0, sizeof(cur));
    cur[3] = 1;
    cur[34] = 1;
    cursor = 0;
    assert(ms912x_scan_row_next_run(old, cur, 35, 16, 0, &cursor, &first, &end));
    assert(first == 0 && end == 16);
    assert(ms912x_scan_row_next_run(old, cur, 35, 16, 0, &cursor, &first, &end));
    assert(first == 32 && end == 35);
    assert(!ms912x_scan_row_next_run(old, cur, 35, 16, 0, &cursor, &first, &end));

    memset(cur, 0, sizeof(cur));
    cur[1] = 0x00010101;
    cur[2] = 0xff000000;
    cursor = 0;
    assert(!ms912x_scan_row_next_run(old, cur, 40, 8, 2, &cursor, &first, &end));

    cur[3] = 0x00030303;
    cur[18] = 0x00030303;
    cursor = 0;
    assert(ms912x_scan_row_next_run(old, cur, 40, 8, 2, &cursor, &first, &end));
    assert(first == 0 && end == 8);
    assert(ms912x_scan_row_next_run(old, cur, 40, 8, 2, &cursor, &first, &end));
    assert(first == 16 && end == 24);
    assert(!ms912x_scan_row_next_run(old, cur, 40, 8, 2, &cursor, &first, &end));
    puts("scan runs: RGB, tolerance, block granularity, gaps, and tail pass");
}
static void cursor_alpha_bounds_tests(void) {
    u32 *pixels = calloc(MS912X_CURSOR_MAX_WIDTH * MS912X_CURSOR_MAX_HEIGHT,
                         sizeof(*pixels));
    unsigned offset_x, offset_y, width, height;

    assert(pixels);
    pixels[10 * MS912X_CURSOR_MAX_WIDTH + 20] = 0xff112233;
    pixels[12 * MS912X_CURSOR_MAX_WIDTH + 23] = 0x80112233;
    assert(ms912x_cursor_alpha_bounds(pixels, 64, 64, &offset_x, &offset_y,
                                      &width, &height));
    assert(offset_x == 20 && offset_y == 10);
    assert(width == 4 && height == 3);
    assert(pixels[10 * MS912X_CURSOR_MAX_WIDTH + 20] == 0xff112233);
    assert(pixels[12 * MS912X_CURSOR_MAX_WIDTH + 23] == 0x80112233);

    memset(pixels, 0, MS912X_CURSOR_MAX_WIDTH * MS912X_CURSOR_MAX_HEIGHT *
           sizeof(*pixels));
    assert(!ms912x_cursor_alpha_bounds(pixels, 128, 128, &offset_x,
                                       &offset_y, &width, &height));
    assert(offset_x == 0 && offset_y == 0 && width == 0 && height == 0);

    pixels[0] = 0xff000000;
    pixels[MS912X_CURSOR_MAX_WIDTH * MS912X_CURSOR_MAX_HEIGHT - 1] =
        0xff000000;
    assert(ms912x_cursor_alpha_bounds(pixels, 128, 128, &offset_x,
                                      &offset_y, &width, &height));
    assert(offset_x == 0 && offset_y == 0 && width == 128 && height == 128);
    free(pixels);
    puts("cursor alpha bounds: sparse, transparent, full 128x128 pass");
}
static void cursor_alpha_offset_index_test(void) {
    u32 *pixels = calloc(MS912X_CURSOR_MAX_WIDTH * MS912X_CURSOR_MAX_HEIGHT,
                         sizeof(*pixels));
    unsigned offset_x, offset_y, width, height;

    assert(pixels);
    pixels[20 * MS912X_CURSOR_MAX_WIDTH + 10] = 0xffff0000;
    pixels[20 * MS912X_CURSOR_MAX_WIDTH + 11] = 0xff00ff00;
    assert(ms912x_cursor_alpha_bounds(pixels, 128, 128, &offset_x,
                                      &offset_y, &width, &height));
    assert(offset_x == 10 && offset_y == 20 && width == 2 && height == 1);
        assert(pixels[ms912x_cursor_source_index(0, 0, offset_x, offset_y)] ==
            0xffff0000);
        assert(pixels[ms912x_cursor_source_index(1, 0, offset_x, offset_y)] ==
            0xff00ff00);
    free(pixels);
        puts("cursor source indexing: original-buffer alpha offsets pass");
}
static void mode3_batch_tests(void) {
    u8 trailer[MS912X_MODE3_TRAILER_BYTES];
    struct drm_rect batch[4] = {{1, 2, 3, 4}, {5, 6, 7, 8}};
    const struct drm_rect previous[] = {{5, 6, 7, 8}, {1, 2, 3, 4}};
    unsigned int count = 2;

    assert(ms912x_mode3_rect_encoded_bytes(32, 32) == 2064);
    ms912x_mode3_write_trailer(trailer, false);
    assert(trailer[0] == 0xff && trailer[1] == 0x80);
    for (unsigned int i = 2; i < sizeof(trailer); i++)
        assert(trailer[i] == 0);
    ms912x_mode3_write_trailer(trailer, true);
    assert(trailer[0] == 0xff && trailer[1] == 0xc0);
    assert(ms912x_mode3_append_rects(batch, ARRAY_SIZE(batch), &count,
                                     previous, ARRAY_SIZE(previous)));
    assert(count == 4);
    assert(batch[2].x1 == 5 && batch[2].x2 == 7);
    assert(batch[3].x1 == 1 && batch[3].x2 == 3);
    assert(!ms912x_mode3_append_rects(batch, ARRAY_SIZE(batch), &count,
                                      previous, 1));
    assert(count == 4 && batch[3].x1 == 1 && batch[3].x2 == 3);
    puts("mode-3 framing, byte sizing, and separate replay ordering pass");
}
static void scan_ref(const u32 *a, const u32 *b, u32 *next, unsigned w, unsigned stride,
                     unsigned rows, unsigned tol, int block,
                     struct ms912x_scan_row_result *results) {
    for (unsigned y = 0; y < rows; y++) {
        struct ms912x_scan_row_result *r = &results[y];
        memset(r, 0, sizeof(*r));
        r->min_x = w;
        bool memcmp_mode = block < 0;
        unsigned step = abs(block);
        if (memcmp_mode && !memcmp(a + y * stride, b + y * stride, w * 4)) {
            r->equal_row = true;
            continue;
        }
        for (unsigned x = 0; x < w;) {
            unsigned end = block ? min(w, x + step) : x + 1, changed = 0;
            for (unsigned i = x; i < end; i++)
                changed |= block ? a[y * stride + i] != b[y * stride + i]
                                 : changed_ref(a[y * stride + i], b[y * stride + i], tol);
            if (memcmp_mode) {
                r->memcmp_blocks++;
                r->memcmp_equal_blocks += !changed;
            } else if (block) {
                r->avx2_blocks++;
                r->avx2_equal_blocks += !changed;
            }
            if (changed) {
                r->changed_pixels += end - x;
                r->min_x = min(r->min_x, x);
                r->max_x = end - 1;
            }
            if (!block)
                next[y * stride + x] = changed ? b[y * stride + x] : a[y * stride + x];
            x = end;
        }
        r->equal_row = !r->changed_pixels;
    }
}
static void wide_scan_tests(struct backend *b) {
    if (!b->after_relax)
        return;
    const unsigned widths[] = {32767, 32768, 32769, 65535};
    u32 *a = malloc(65535 * 4), *c = malloc(65535 * 4), *n = malloc(65535 * 4),
        *ref = malloc(65535 * 4);
    for (unsigned k = 0; k < ARRAY_SIZE(widths); k++)
        for (unsigned p = 0; p < 3; p++) {
            unsigned w = widths[k];
            for (unsigned x = 0; x < w; x++) {
                a[x] = random32();
                c[x] = (p == 2 || (p == 1 && x == w - 1)) ? a[x] ^ 0xffffff : a[x];
            }
            struct ms912x_scan_row_result want, got;
            scan_ref(a, c, ref, w, w, 1, 64, 0, &want);
            b->after_relax(a, c, n, w, w, 1, 64, &got);
            if (memcmp(&want, &got, sizeof(want)) || memcmp(n, ref, w * 4))
                fail("wide relaxed", b, w, 64, p);
        }
    free(a);
    free(c);
    free(n);
    free(ref);
    printf("%s wide rows: 32767/32768/32769/65535 bounds and scalar fallback match\n", b->name);
}
static void scan_tests(struct backend *b) {
    size_t capacity = 16 * 2048 + 32;
    u32 *aa = malloc(capacity * 4), *bb = malloc(capacity * 4);
    u32 *nn = malloc(capacity * 4), *oo = malloc(capacity * 4), *rr = malloc(capacity * 4);
    unsigned tests = 0;
    const unsigned widths[] = {0,  1,  2,  3,   4,   7,   8,   9,   15,  16,   17,   31,   32,  33,
                               63, 64, 65, 127, 128, 129, 255, 257, 639, 1280, 1918, 1919, 1920};
    for (unsigned t = 0; t < 256; t++)
        for (unsigned p = 0; p < 7; p++) {
            unsigned w = widths[(t + p) % ARRAY_SIZE(widths)];
            unsigned rows = 1 + (t % 16), stride = w + 7, offset = t % 8;
            size_t count = (size_t)rows * stride + offset + 8;
            u32 *a = aa + offset, *c = bb + offset;
            for (size_t i = 0; i < count; i++) {
                aa[i] = random32();
                bb[i] = aa[i];
            }
            for (unsigned y = 0; y < rows; y++)
                for (unsigned x = 0; x < w; x++) {
                    unsigned i = y * stride + x;
                    switch (p) {
                    case 1:
                        c[i] ^= 0xff000000;
                        break;
                    case 2:
                        c[i] = random32();
                        break;
                    case 3:
                        if (!(x % 67))
                            c[i] ^= 0x00ffffff;
                        break;
                    case 4:
                        c[i] ^= 0x00010203;
                        break;
                    case 5:
                        a[i] &= 0xff000000;
                        c[i] = (a[i] ^ 0xff000000) | (t * 0x010101);
                        break;
                    case 6:
                        a[i] &= 0xff000000;
                        c[i] = a[i] | (min(t + 1, 255U) * 0x010101);
                        break;
                    }
                }
            struct ms912x_scan_row_result old[16], now[16], ref[16];
            scan_ref(a, c, rr + offset, w, stride, rows, b->block_size ? t : 0, b->block_size, ref);
            b->before_block(a, c, w, stride, rows, old);
            b->after_block(a, c, w, stride, rows, now);
            if (memcmp(ref, old, rows * sizeof(*ref)) || memcmp(ref, now, rows * sizeof(*ref)))
                fail("blocks", b, w, t, p);
            if (b->after_relax) {
                memset(nn, 0xa5, count * 4);
                memset(oo, 0xa5, count * 4);
                memset(rr, 0xa5, count * 4);
                scan_ref(a, c, rr + offset, w, stride, rows, t, 0, ref);
                b->before_relax(a, c, oo + offset, w, stride, rows, t, old);
                b->after_relax(a, c, nn + offset, w, stride, rows, t, now);
                if (memcmp(ref, old, rows * sizeof(*ref)) ||
                    memcmp(ref, now, rows * sizeof(*ref)) || memcmp(rr, nn, count * 4) ||
                    memcmp(rr, oo, count * 4))
                    fail("relaxed", b, w, t, p);
            }
            tests++;
        }
    free(aa);
    free(bb);
    free(nn);
    free(oo);
    free(rr);
    printf("%s scan: %u cases, all tolerances, bounds/counts/baseline match scalar + original\n",
           b->name, tests);
}
static u32 color_ref(u32 a, u32 b) {
    unsigned r0 = (a >> 16) & 255, g0 = (a >> 8) & 255, b0 = a & 255;
    unsigned r1 = (b >> 16) & 255, g1 = (b >> 8) & 255, b1 = b & 255;
    unsigned y0 = 16 + (16763 * r0 + 32904 * g0 + 6391 * b0) / 65536;
    unsigned y1 = 16 + (16763 * r1 + 32904 * g1 + 6391 * b1) / 65536;
    int64_t u = (128LL << 17) - 9676 * (r0 + r1) - 18996 * (g0 + g1) + 28672 * (b0 + b1);
    int64_t v = (128LL << 17) + 28672 * (r0 + r1) - 24009 * (g0 + g1) - 4663 * (b0 + b1);
    return (u / 131072) | ((u32)y0 << 8) | ((u32)(v / 131072) << 16) | ((u32)y1 << 24);
}
static void conversion_tests(struct backend *b) {
    u32 *src = malloc(40 * 1920 * 4);
    u8 *old = malloc(40 * 1920 * 2 + 128), *now = malloc(40 * 1920 * 2 + 128),
       *ref = malloc(40 * 1920 * 2 + 128);
    unsigned tests = 0;
    for (unsigned i = 0; i < 40 * 1920; i++)
        src[i] = random32();
    for (unsigned w = 2; w <= 1920; w += 2) {
        unsigned h = 1 + w % 31, off = w % 31, x = ((1920 - w) / 2) & ~1;
        struct drm_rect r = {(int)x, 2, (int)(x + w), (int)(2 + h)};
        size_t len = w * h * 2, total = len + 64;
        memset(old, 0xa5, total);
        memset(now, 0xa5, total);
        memset(ref, 0xa5, total);
        for (unsigned y = 0; y < h; y++)
            for (unsigned i = 0; i < w; i += 2) {
                u32 pair = color_ref(src[(y + 2) * 1920 + x + i], src[(y + 2) * 1920 + x + i + 1]);
                memcpy(ref + off + (y * w + i) * 2, &pair, 4);
            }
        b->before_convert(old + off, src, &r);
        b->after_convert(now + off, src, &r);
        if (memcmp(ref, old, total) || memcmp(ref, now, total))
            fail("conversion", b, w, 0, 0);
        tests++;
    }
    /* Walk every RGB triple, with identical adjacent pixels and arbitrary X. */
    struct drm_rect r = {0, 0, 1920, 16};
    for (unsigned base = 0; base < (1U << 24); base += 1920 * 8) {
        for (unsigned i = 0; i < 1920 * 16; i += 2) {
            u32 rgb = (base + i / 2) & 0xffffff, p = rgb | (random32() & 0xff000000);
            src[i] = p;
            src[i + 1] = rgb | (random32() & 0xff000000);
            u32 pair = color_ref(src[i], src[i + 1]);
            memcpy(ref + i * 2, &pair, 4);
        }
        b->after_convert(now, src, &r);
        if (memcmp(ref, now, 1920 * 16 * 2))
            fail("exhaustive RGB", b, 1920, 0, base);
    }
    free(src);
    free(old);
    free(now);
    free(ref);
    printf("%s conversion: %u rectangles + all RGB triples match Q16 scalar\n", b->name, tests);
}
static void *guarded(size_t page) {
    void *p = mmap(NULL, page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(p != MAP_FAILED);
    assert(!mprotect((u8 *)p + page, page, PROT_NONE));
    return p;
}
static void guard_tests(struct backend *b) {
    size_t page = sysconf(_SC_PAGESIZE);
    u8 *ab = guarded(page), *bb = guarded(page), *nb = guarded(page);
    for (unsigned w = 1; w < 130; w++) {
        u32 *a = (u32 *)(ab + page - w * 4), *c = (u32 *)(bb + page - w * 4),
            *n = (u32 *)(nb + page - w * 4);
        for (unsigned i = 0; i < w; i++) {
            a[i] = random32();
            c[i] = random32();
        }
        struct ms912x_scan_row_result r;
        if (b->after_block)
            b->after_block(a, c, w, w, 1, &r);
        if (b->after_relax)
            b->after_relax(a, c, n, w, w, 1, 64, &r);
        if (b->after_convert && !(w & 1)) {
            struct drm_rect rect = {0, 0, (int)w, 1};
            b->after_convert(nb + page - w * 2, a, &rect);
        }
    }
    munmap(ab, page * 2);
    munmap(bb, page * 2);
    munmap(nb, page * 2);
    printf("%s guard pages: no row-tail overreads/overwrites\n", b->name);
}
static double now_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC_RAW, &t);
    return t.tv_sec * 1e9 + t.tv_nsec;
}
static int cmp_double(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}
static FILE *samples;
static void report(const char *name, const char *task, double *before, double *after,
                   double pixels) {
    if (samples) {
        fprintf(samples, "%s,%s,%.0f", name, task, pixels);
        for (unsigned i = 0; i < 7; i++)
            fprintf(samples, ",%.0f", before[i]);
        for (unsigned i = 0; i < 7; i++)
            fprintf(samples, ",%.0f", after[i]);
        fputc('\n', samples);
        fflush(samples);
    }
    qsort(before, 7, sizeof(double), cmp_double);
    qsort(after, 7, sizeof(double), cmp_double);
    printf("%s %-24s before %8.1f after %8.1f Mpix/s  %.2fx\n", name, task,
           pixels * 1000 / before[3], pixels * 1000 / after[3], before[3] / after[3]);
}
static void benchmark(struct backend *b) {
    u32 *a = malloc(1920 * 1080 * 4), *c = malloc(1920 * 1080 * 4), *n = malloc(1920 * 1080 * 4);
    u8 *dst = malloc(1920 * 1080 * 2);
    for (unsigned i = 0; i < 1920 * 1080; i++)
        a[i] = random32();
    const unsigned widths[] = {1920, 640, 33, 16};
    if (b->after_block)
        for (unsigned k = 0; k < ARRAY_SIZE(widths); k++)
            for (unsigned p = 0; p < 3; p++) {
                unsigned w = widths[k], iterations = 1024 * (1920 / w);
                for (unsigned i = 0; i < 1920 * 1080; i++)
                    c[i] = (p == 2 || (p == 1 && !(i % 67))) ? a[i] ^ 0xffffff : a[i];
                for (unsigned mode = 0; mode < (b->after_relax ? 2U : 1U); mode++) {
                    double old[7], cur[7];
                    struct ms912x_scan_row_result r[16];
                    for (unsigned rep = 0; rep < 7; rep++)
                        for (unsigned j = 0; j < 2; j++) {
                            bool use_new = (j + rep) % 2;
                            double start = now_ns();
                            for (unsigned i = 0; i < iterations; i++) {
                                if (mode)
                                    (use_new ? b->after_relax
                                             : b->before_relax)(a, c, n, w, 1920 * 3, 16, 64, r);
                                else
                                    (use_new ? b->after_block : b->before_block)(a, c, w, 1920 * 3,
                                                                                 16, r);
                            }
                            (use_new ? cur : old)[rep] = now_ns() - start;
                        }
                    char label[80];
                    snprintf(label, sizeof(label), "%s %s w=%u", mode ? "relaxed" : "exact",
                             p == 0   ? "unchanged"
                             : p == 1 ? "sparse"
                                      : "dense",
                             w);
                    report(b->name, label, old, cur, (double)w * 16 * iterations);
                }
            }
    const struct drm_rect rects[] = {{0, 0, 1920, 1080}, {2, 2, 642, 130}, {2, 0, 8, 64}};
    const unsigned loops[] = {32, 512, 32768};
    if (b->after_convert)
        for (unsigned k = 0; k < 3; k++) {
            double old[7], cur[7];
            for (unsigned rep = 0; rep < 7; rep++)
                for (unsigned j = 0; j < 2; j++) {
                    bool use_new = (j + rep) % 2;
                    double start = now_ns();
                    for (unsigned i = 0; i < loops[k]; i++)
                        (use_new ? b->after_convert : b->before_convert)(dst, a, &rects[k]);
                    (use_new ? cur : old)[rep] = now_ns() - start;
                }
            char label[80];
            snprintf(label, sizeof(label), "conversion %dx%d", drm_rect_width(&rects[k]),
                     drm_rect_height(&rects[k]));
            report(b->name, label, old, cur,
                   (double)drm_rect_width(&rects[k]) * drm_rect_height(&rects[k]) * loops[k]);
        }
    free(a);
    free(c);
    free(n);
    free(dst);
}
int main(int argc, char **argv) {
    setbuf(stdout, NULL);
    dispatch_tests();
    scan_run_tests();
    cursor_alpha_bounds_tests();
    cursor_alpha_offset_index_test();
    mode3_batch_tests();
#ifndef CONFIG_X86_64
    /* Unusable SIMD and absent hardware must return before touching buffers. */
    struct drm_rect guard_rect = {0, 0, 2, 1};
    bench_neon_present = false;
    assert(!ms912x_convert_xrgb8888_to_uyvy_neon(NULL, NULL, &guard_rect));
    bench_neon_present = true;
    bench_simd_allowed = false;
    assert(!ms912x_convert_xrgb8888_to_uyvy_neon(NULL, NULL, &guard_rect));
    bench_simd_allowed = true;
    puts("NEON entry guards: unsupported hardware/context returns before buffer access");
#endif
    const char *sample_path = getenv("MS912X_BENCH_SAMPLES");
    if (sample_path) {
        samples = fopen(sample_path, "w");
        assert(samples);
        fputs("backend,workload,pixels,before0_ns,before1_ns,before2_ns,before3_ns,before4_ns,"
              "before5_ns,before6_ns,after0_ns,after1_ns,after2_ns,after3_ns,after4_ns,after5_ns,"
              "after6_ns\n",
              samples);
    }
    cpu_set_t set;
    if (!sched_getaffinity(0, sizeof(set), &set))
        for (int i = 0; i < CPU_SETSIZE; i++)
            if (CPU_ISSET(i, &set)) {
                CPU_ZERO(&set);
                CPU_SET(i, &set);
                assert(!sched_setaffinity(0, sizeof(set), &set));
                printf("Pinned CPU: %d\n", i);
                break;
            }
    for (unsigned i = 0; i < ARRAY_SIZE(backends); i++) {
        struct backend *b = &backends[i];
        if (!available(b)) {
            printf("SKIP %s: unsupported CPU feature\n", b->name);
            continue;
        }
        if (b->after_block) {
            scan_tests(b);
            wide_scan_tests(b);
        }
        if (b->after_convert)
            conversion_tests(b);
        guard_tests(b);
    }
    if (argc > 1 && !strcmp(argv[1], "--bench"))
        for (unsigned i = 0; i < ARRAY_SIZE(backends); i++)
            if (available(&backends[i]) &&
                (!getenv("MS912X_BENCH_BACKEND") ||
                 !strcmp(getenv("MS912X_BENCH_BACKEND"), backends[i].name)))
                benchmark(&backends[i]);
    if (samples)
        fclose(samples);
    return 0;
}
