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
#include "ms912x_downscale.h"
#ifdef MS912X_DOWNSCALE_AREA_FILTER
#define MS912X_TEST_DOWNSCALE_AREA true
#else
#define MS912X_TEST_DOWNSCALE_AREA false
#endif
#include "ms912x_damage_policy.h"
#include "ms912x_scale.h"
#include "ms912x_mode3.h"

bool bench_simd_allowed = true;
bool bench_neon_present = true;
extern void dispatch_tests(void);

typedef void (*block_fn)(const u32 *, const u32 *, unsigned, unsigned, unsigned,
                         struct ms912x_scan_row_result *);
typedef void (*relax_fn)(const u32 *, const u32 *, u32 *, unsigned, unsigned, unsigned, unsigned,
                         struct ms912x_scan_row_result *);
typedef void (*convert_fn)(void *, const __le32 *, const struct drm_rect *);
typedef void (*downscale_fn)(const u32 *, u32 *, unsigned, unsigned, unsigned,
                             unsigned, unsigned, unsigned);
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
extern void ms912x_downscale_xrgb8888_3to2_scalar(const u32 *, u32 *, unsigned,
                                                unsigned, unsigned, unsigned,
                                                unsigned, unsigned);
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
extern void ms912x_downscale_xrgb8888_3to2_sse2(const u32 *, u32 *, unsigned,
                                              unsigned, unsigned, unsigned,
                                              unsigned, unsigned);
extern void ms912x_downscale_xrgb8888_3to2_avx2(const u32 *, u32 *, unsigned,
                                              unsigned, unsigned, unsigned,
                                              unsigned, unsigned);
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
extern void ms912x_downscale_xrgb8888_3to2_neon(const u32 *, u32 *, unsigned,
                                               unsigned, unsigned, unsigned,
                                               unsigned, unsigned);
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
static u32 random32(void);

#define DECL_OLD_DOWNSCALE(name) \
    extern void old_ms912x_downscale_xrgb8888_3to2_##name(const u32 *, u32 *, unsigned, \
        unsigned, unsigned, unsigned, unsigned, unsigned);
DECL_OLD_DOWNSCALE(scalar)
#ifdef CONFIG_X86_64
DECL_OLD_DOWNSCALE(sse2)
DECL_OLD_DOWNSCALE(avx2)
#else
DECL_OLD_DOWNSCALE(neon)
#endif
#ifdef MS912X_DOWNSCALE_SCALAR_BASELINE
#define OLD_DOWNSCALE(name) old_ms912x_downscale_xrgb8888_3to2_scalar
#else
#define OLD_DOWNSCALE(name) old_ms912x_downscale_xrgb8888_3to2_##name
#endif

typedef void (*downscale_region_fn)(const u32 *, u32 *, unsigned, unsigned,
                                    unsigned, unsigned, unsigned, unsigned,
                                    unsigned, unsigned);
struct downscale_backend {
    const char *name;
    downscale_fn before;
    downscale_fn run;
    downscale_region_fn region;
};
static const struct downscale_backend downscale_backends[] = {
    {"scalar", OLD_DOWNSCALE(scalar), ms912x_downscale_xrgb8888_3to2_scalar,
     ms912x_downscale_xrgb8888_3to2_scalar_region},
#ifdef CONFIG_X86_64
    {"SSE2", OLD_DOWNSCALE(sse2), ms912x_downscale_xrgb8888_3to2_sse2,
     ms912x_downscale_xrgb8888_3to2_sse2_region},
    {"AVX2", OLD_DOWNSCALE(avx2), ms912x_downscale_xrgb8888_3to2_avx2,
     ms912x_downscale_xrgb8888_3to2_avx2_region},
#else
    {"NEON", OLD_DOWNSCALE(neon), ms912x_downscale_xrgb8888_3to2_neon,
     ms912x_downscale_xrgb8888_3to2_neon_region},
#endif
};

static bool downscale_backend_available(const struct downscale_backend *backend)
{
    (void)backend;
#ifdef CONFIG_X86_64
    if (!strcmp(backend->name, "AVX2"))
        return __builtin_cpu_supports("avx2");
    if (!strcmp(backend->name, "SSE2"))
        return __builtin_cpu_supports("sse2");
#endif
#ifdef CONFIG_ARM
    if (!strcmp(backend->name, "NEON"))
        return !!(getauxval(AT_HWCAP) & HWCAP_NEON);
#endif
    return true;
}

static void cursor_alpha_bounds_tests(void)
{
    u32 pixels[MS912X_CURSOR_MAX_WIDTH * MS912X_CURSOR_MAX_HEIGHT] = {0};
    unsigned offset_x, offset_y, width, height;

    assert(!ms912x_cursor_alpha_bounds(pixels, 8, 6, &offset_x, &offset_y,
                                      &width, &height));
    assert(offset_x == 0 && offset_y == 0 && width == 0 && height == 0);
    pixels[2 * MS912X_CURSOR_MAX_WIDTH + 3] = 0xff010203;
    pixels[4 * MS912X_CURSOR_MAX_WIDTH + 6] = 0x80040506;
    assert(ms912x_cursor_alpha_bounds(pixels, 8, 6, &offset_x, &offset_y,
                                      &width, &height));
    assert(offset_x == 3 && offset_y == 2 && width == 4 && height == 3);
}

static void cursor_alpha_offset_index_test(void)
{
    assert(ms912x_cursor_source_index(2, 3, 5, 7) ==
           10 * MS912X_CURSOR_MAX_WIDTH + 7);
}

static u32 downscale_pixel_ref(u32 p00, u32 p01, u32 p10, u32 p11,
                               unsigned wx, unsigned wy)
{
    u32 result = 0;

    for (unsigned shift = 0; shift < 32; shift += 8) {
        unsigned sum = wx * wy * ((p00 >> shift) & 255) +
                       (3 - wx) * wy * ((p01 >> shift) & 255) +
                       wx * (3 - wy) * ((p10 >> shift) & 255) +
                       (3 - wx) * (3 - wy) * ((p11 >> shift) & 255);

        result |= ((sum + 4) / 9) << shift;
    }
    return result;
}

static unsigned cubic_ref_channel(unsigned a, unsigned b, unsigned c, unsigned d,
                                  unsigned phase) {
    int coeff[2][4] = {{-9,111,29,-3},{-3,29,111,-9}};
    int sum = coeff[phase][0]*(int)a + coeff[phase][1]*(int)b +
              coeff[phase][2]*(int)c + coeff[phase][3]*(int)d;
    /* Independent signed floor division: avoid the production shift/bias trick. */
    int rounded = sum + 64;
    rounded = rounded >= 0 ? rounded / 128 : -((-rounded + 127) / 128);
    return rounded < 0 ? 0U : rounded > 255 ? 255U : (unsigned)rounded;
}

static u32 cubic_ref(const u32 *src, unsigned sw, unsigned sh,
                     unsigned stride, unsigned x, unsigned y) {
    int sx = (int)(3*x/2), sy = (int)(3*y/2);
    u32 pixel = 0;
    for (unsigned shift = 0; shift < 32; shift += 8) {
        unsigned horizontal[4];
        for (int j = 0; j < 4; j++) {
            unsigned values[4];
            int iy = sy + j - 1;
            if (iy < 0) iy = 0;
            if ((unsigned)iy >= sh) iy = (int)sh - 1;
            for (int i = 0; i < 4; i++) {
                int ix = sx + i - 1;
                if (ix < 0) ix = 0;
                if ((unsigned)ix >= sw) ix = (int)sw - 1;
                values[i] = (src[iy*stride+ix] >> shift) & 255;
            }
            horizontal[j] = cubic_ref_channel(values[0],values[1],values[2],values[3],x&1);
        }
        pixel |= cubic_ref_channel(horizontal[0],horizontal[1],horizontal[2],
                                   horizontal[3],y&1) << shift;
    }
    return pixel;
}

static u32 downscale_reference_pixel(const u32 *src, unsigned sw, unsigned sh,
                                     unsigned ss, unsigned x, unsigned y, bool area) {
    if (!area) return cubic_ref(src,sw,sh,ss,x,y);
    return downscale_pixel_ref(
        src[min(3*y/2,sh-1)*ss+min(3*x/2,sw-1)],
        src[min(3*y/2,sh-1)*ss+min(3*x/2+1,sw-1)],
        src[min(3*y/2+1,sh-1)*ss+min(3*x/2,sw-1)],
        src[min(3*y/2+1,sh-1)*ss+min(3*x/2+1,sw-1)],
        (x&1)?1:2,(y&1)?1:2);
}

static void downscale_reference(const u32 *src, u32 *dst, unsigned sw, unsigned sh,
                                unsigned dw, unsigned dh, unsigned ss, unsigned ds,
                                bool area) {
    memset(dst,0xa5,(size_t)ds*dh*4);
    for (unsigned y=0;y<dh;y++) for (unsigned x=0;x<dw;x++)
        dst[y*ds+x] = downscale_reference_pixel(src,sw,sh,ss,x,y,area);
}

static void downscale_check_geometry(unsigned sw, unsigned sh, unsigned dw, unsigned dh,
                                     unsigned offset) {
    unsigned ss = sw + 7, ds = dw + 5;
    size_t sc = (size_t)ss * sh, dc = (size_t)ds * dh;
    u32 *allocation = malloc((sc + 16) * 4), *src = allocation + offset;
    u32 *expected = malloc(dc * 4), *actual = malloc(dc * 4), *inplace = malloc(sc * 4);

    assert(allocation && expected && actual && inplace);
    for (size_t i = 0; i < sc; i++) src[i] = random32();
    for (unsigned k = 0; k < ARRAY_SIZE(downscale_backends); k++) {
        const struct downscale_backend *b = &downscale_backends[k];
        if (!downscale_backend_available(b)) continue;
        for (unsigned old = 0; old < 2; old++) {
            downscale_fn fn = old ? b->before : b->run;
            bool area = old ? MS912X_BENCH_DOWNSCALE_BASELINE_AREA :
                              MS912X_TEST_DOWNSCALE_AREA;
            downscale_reference(src, expected, sw, sh, dw, dh, ss, ds, area);
            memset(actual, 0xa5, dc * 4);
            fn(src, actual, sw, sh, dw, dh, ss, ds);
            if (memcmp(actual, expected, dc * 4)) {
                fprintf(stderr, "FAIL %s downscale %ux%u->%ux%u offset=%u old=%u\n",
                        b->name, sw, sh, dw, dh, offset, old);
                abort();
            }
            /* Both the driver's fixed stride and packed output must work in-place. */
            for (unsigned packed = 0; packed < 2; packed++) {
                unsigned stride = packed ? dw : ss;
                memcpy(inplace, src, sc * 4);
                fn(inplace, inplace, sw, sh, dw, dh, ss, stride);
                for (unsigned y = 0; y < dh; y++)
                    assert(!memcmp(inplace + y * stride, expected + y * ds, dw * 4));
            }
        }
    }
    free(inplace); free(actual); free(expected); free(allocation);
}

static void downscale_rounding_tests(void) {
    enum { W = 511, STRIDE = 2 * W };
    u32 src[2 * STRIDE], expected[W], actual[W];

    /* Retain the channel patterns, evaluated with the weighted 3->2 oracle. */
    for (unsigned a = 0; a <= 510; a++) {
        for (unsigned b = 0; b <= 510; b++) {
            unsigned sums[4][2] = {{a,b}, {510-a,510-b},
                                  {(a+37)%511,(b+113)%511}, {(a*3)%511,(b*7)%511}};
            u32 p[4] = {0};
            for (unsigned c = 0; c < 4; c++)
                for (unsigned j = 0; j < 2; j++) {
                    unsigned sum = sums[c][j], first = min(sum, 255U);
                    p[j] |= first << (c * 8);
                    p[2+j] |= (sum - first) << (c * 8);
                }
            src[2*b] = p[0]; src[2*b+1] = p[1];
            src[STRIDE+2*b] = p[2]; src[STRIDE+2*b+1] = p[3];
        }
        for (unsigned x = 0; x < W; x++)
            expected[x] = downscale_reference_pixel(src,STRIDE,2,STRIDE,x,0,
                                                   MS912X_TEST_DOWNSCALE_AREA);
        for (unsigned k = 0; k < ARRAY_SIZE(downscale_backends); k++) {
            const struct downscale_backend *d = &downscale_backends[k];
            if (!downscale_backend_available(d)) continue;
            d->run(src, actual, STRIDE, 2, W, 1, STRIDE, W);
            assert(!memcmp(expected, actual, sizeof(actual)));
        }
    }
    puts("downscale rounding: 261121 output patterns, all byte channels pass");
}

static void downscale_guard_tests(void) {
    size_t page = (size_t)sysconf(_SC_PAGESIZE);
    u8 *mapping = mmap(NULL,page*10,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    u8 *output = mmap(NULL,page*8,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    assert(mapping!=MAP_FAILED && output!=MAP_FAILED);
    for (unsigned y=0;y<5;y++) assert(!mprotect(mapping+2*y*page,page,PROT_READ|PROT_WRITE));
    for (unsigned y=0;y<4;y++) assert(!mprotect(output+2*y*page,page,PROT_READ|PROT_WRITE));
    unsigned stride=2*page/4;
    for (unsigned w=1;w<=129;w++) for (unsigned h=1;h<=5;h++) {
        unsigned dw=(w*2+2)/3+1, dh=(h*2+2)/3;
        u32 *src=(u32 *)(mapping+page-w*4), *out=(u32 *)(output+page-dw*4);
        for (unsigned y=0;y<h;y++) for (unsigned x=0;x<w;x++) src[y*stride+x]=random32();
        for (unsigned k=0;k<ARRAY_SIZE(downscale_backends);k++) {
            const struct downscale_backend *d=&downscale_backends[k];
            if (!downscale_backend_available(d)) continue;
            d->run(src,out,w,h,dw,dh,stride,stride);
            for (unsigned y=0;y<dh;y++) for (unsigned x=0;x<dw;x++)
                assert(out[y*stride+x]==downscale_reference_pixel(src,w,h,stride,x,y,MS912X_TEST_DOWNSCALE_AREA));
        }
    }
    assert(!munmap(output,page*8)); assert(!munmap(mapping,page*10));
    puts("downscale guard pages: all filter taps, individual row ends, widths 1..129/heights 1..5 pass");
}

static void downscale_extrema_tests(void) {
    enum {SW=18,SH=6,DW=12,DH=4};
    u32 src[SW*SH],out[DW*DH],expected[DW*DH],working[SW*SH];
    for(unsigned horizontal=0;horizontal<16;horizontal++)
    for(unsigned vertical=0;vertical<16;vertical++) {
        for(unsigned y=0;y<SH;y++) for(unsigned x=0;x<SW;x++) {
            unsigned h=(horizontal>>(x&3))&1,v=(vertical>>(y&3))&1;
            src[y*SW+x]=(h?255U:0)|(v?0xff00U:0)|((h&v)?0xff0000U:0)|
                         ((h^v)?0xff000000U:0);
        }
        downscale_reference(src,expected,SW,SH,DW,DH,SW,DW,MS912X_TEST_DOWNSCALE_AREA);
        for(unsigned k=0;k<ARRAY_SIZE(downscale_backends);k++) {
            const struct downscale_backend *b=&downscale_backends[k];
            if(!downscale_backend_available(b)) continue;
            b->run(src,out,SW,SH,DW,DH,SW,DW);
            assert(!memcmp(out,expected,sizeof(out)));
            memcpy(working,src,sizeof(src));
            b->run(working,working,SW,SH,DW,DH,SW,DW);
            assert(!memcmp(working,expected,sizeof(expected)));
        }
    }
    puts("downscale extrema: 256 binary patterns, all channels and packed in-place pass");
}

static void downscale_region_tests(void) {
    enum {SW=67,SH=35,SS=74,DW=48,DH=24};
    u32 src[SS*SH], cropped[SS*SH], full[DW*DH], out[DW*DH];
    for (unsigned i=0;i<ARRAY_SIZE(src);i++) src[i]=random32();
    downscale_reference(src,full,SW,SH,DW,DH,SS,DW,MS912X_TEST_DOWNSCALE_AREA);
    for (unsigned x1=0;x1<DW;x1+=4) for (unsigned y1=0;y1<DH;y1+=2) {
        unsigned w=min(7U,DW-x1),h=min(5U,DH-y1);
        unsigned cx=x1/2*3,cy=y1/2*3;
        unsigned sx=cx?cx-1:0,sy=cy?cy-1:0;
        unsigned cw=min(cx+(w*3+1)/2+1,(unsigned)SW)-sx;
        unsigned ch=min(cy+(h*3+1)/2+1,(unsigned)SH)-sy;
        memset(cropped,0xa5,sizeof(cropped));
        for(unsigned y=0;y<ch;y++) memcpy(cropped+y*SS,src+(sy+y)*SS+sx,cw*4);
        for(unsigned k=0;k<ARRAY_SIZE(downscale_backends);k++) {
            const struct downscale_backend *b=&downscale_backends[k];
            if(!downscale_backend_available(b)) continue;
            memset(out,0xa5,sizeof(out));
            b->region(cropped,out,cw,ch,w,h,SS,DW,cx-sx,cy-sy);
            for(unsigned y=0;y<h;y++) {
                assert(!memcmp(out+y*DW,full+(y1+y)*DW+x1,w*4));
                for(unsigned x=w;x<DW;x++) assert(out[y*DW+x]==0xa5a5a5a5);
            }
        }
    }
    puts("downscale staged regions: 144 crops, leading/trailing halo, odd extents and replicated padding match full frame");
}

static void downscale_backend_tests(void) {
    static const unsigned large[][4] = {
        {1366,768,912,512}, {1920,1080,1280,720}, {1280,720,856,480},
    };
    for (unsigned k = 0; k < ARRAY_SIZE(downscale_backends); k++) {
        const struct downscale_backend *d = &downscale_backends[k];
        if (!downscale_backend_available(d)) continue;
        /* Empty dimensions must return before touching even an invalid pointer. */
        for (unsigned i = 0; i < 4; i++) {
            unsigned dims[4] = {1,1,1,1}; dims[i] = 0;
            d->run(NULL,NULL,dims[0],dims[1],dims[2],dims[3],1,1);
        }
    }
    for (unsigned w = 1; w <= 65; w++)
        for (unsigned j = 0; j < 4; j++) {
            unsigned heights[] = {1,3,33,65};
            downscale_check_geometry(w,heights[j],(w*2+2)/3+((j&1) && w>2),
                                     (heights[j]*2+2)/3,j);
        }
    for (unsigned k = 0; k < ARRAY_SIZE(large); k++)
        downscale_check_geometry(large[k][0],large[k][1],large[k][2],large[k][3],k);
#ifndef CONFIG_X86_64
    u32 src[16], out[4], expected[4];
    for (unsigned i = 0; i < ARRAY_SIZE(src); i++) src[i] = random32();
    ms912x_downscale_xrgb8888_3to2_scalar(src,expected,4,4,2,2,4,2);
    bench_neon_present = false;
    ms912x_downscale_xrgb8888_3to2_neon(src,out,4,4,2,2,4,2);
    assert(!memcmp(out,expected,sizeof(out)));
    bench_neon_present = true; bench_simd_allowed = false;
    ms912x_downscale_xrgb8888_3to2_neon(src,out,4,4,2,2,4,2);
    assert(!memcmp(out,expected,sizeof(out)));
    bench_simd_allowed = true;
#endif
    downscale_rounding_tests();
    downscale_guard_tests();
    downscale_region_tests();
    downscale_extrema_tests();
    puts("SIMD 3->2 downscale: 263 geometries, odd edges, strides, in-place/packed output, empty dimensions pass");
}

static void scale_geometry_tests(void)
{
    unsigned int input_width, input_height;

    ms912x_scale_input_geometry(1920, 1080, MS912X_SCALE_MODE_DOWNSCALE_2_TO_1,
                                &input_width, &input_height);
    assert(input_width == 960 && input_height == 540);
    ms912x_scale_input_geometry(1920, 1080, MS912X_SCALE_MODE_DOWNSCALE_3_TO_2,
                                &input_width, &input_height);
    assert(input_width == 1280 && input_height == 720);
    ms912x_scale_input_geometry(1920, 1080, MS912X_SCALE_MODE_NATIVE,
                                &input_width, &input_height);
    assert(input_width == 1920 && input_height == 1080);
    ms912x_scale_input_geometry(1366, 768, MS912X_SCALE_MODE_DOWNSCALE_3_TO_2,
                                &input_width, &input_height);
    assert(input_width == 912 && input_height == 512);
    puts("scale presets: 50, 67, and 100 geometry pass");
}

static void damage_tolerance_curve_tests(void)
{
    unsigned previous = 1;

    assert(ms912x_damage_scan_auto_tolerance(0, 100, 200, 1, 64) == 1);
    assert(ms912x_damage_scan_auto_tolerance(100, 100, 200, 1, 64) == 1);
    assert(ms912x_damage_scan_auto_tolerance(150, 100, 200, 1, 64) == 32);
    assert(ms912x_damage_scan_auto_tolerance(199, 100, 200, 1, 64) == 63);
    assert(ms912x_damage_scan_auto_tolerance(200, 100, 200, 1, 64) == 64);
    assert(ms912x_damage_scan_auto_tolerance(250, 100, 200, 1, 64) == 64);
    assert(ms912x_damage_scan_auto_tolerance(150, 100, 200, 8, 48) == 28);
    assert(ms912x_damage_scan_auto_tolerance(1000, 100, 200, 17, 17) == 17);
    for (unsigned pressure = 100; pressure <= 200; pressure++) {
        unsigned current = ms912x_damage_scan_auto_tolerance(
            pressure, 100, 200, 1, 64);

        assert(current >= previous && current >= 1 && current <= 64);
        previous = current;
    }
    puts("damage tolerance curve: fixed/ranged bounds, endpoints and proportional monotonic mapping pass");
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
    assert(ms912x_scan_row_next_run(old, cur, 40, 39, 1, 0, &cursor, &first, &end));
    assert(first == 2 && end == 3);
    assert(ms912x_scan_row_next_run(old, cur, 40, 39, 1, 0, &cursor, &first, &end));
    assert(first == 10 && end == 11);
    assert(!ms912x_scan_row_next_run(old, cur, 40, 39, 1, 0, &cursor, &first, &end));

    memset(cur, 0, sizeof(cur));
    cur[4] = 0x00020202;
    cur[7] = 0x00050505;
    cursor = 0;
    assert(ms912x_scan_row_next_run(old, cur, 40, 39, 1, 2, &cursor, &first, &end));
    assert(first == 7 && end == 8);
    assert(!ms912x_scan_row_next_run(old, cur, 40, 39, 1, 2, &cursor, &first, &end));

    memset(cur, 0, sizeof(cur));
    cur[3] = 1;
    cur[34] = 1;
    cursor = 0;
    assert(ms912x_scan_row_next_run(old, cur, 35, 34, 16, 0, &cursor, &first, &end));
    assert(first == 0 && end == 16);
    assert(ms912x_scan_row_next_run(old, cur, 35, 34, 16, 0, &cursor, &first, &end));
    assert(first == 32 && end == 35);
    assert(!ms912x_scan_row_next_run(old, cur, 35, 34, 16, 0, &cursor, &first, &end));

    memset(cur, 0, sizeof(cur));
    cur[1] = 0x00010101;
    cur[2] = 0xff000000;
    cursor = 0;
    assert(!ms912x_scan_row_next_run(old, cur, 40, 39, 8, 2, &cursor, &first, &end));

    cur[3] = 0x00030303;
    cur[18] = 0x00030303;
    cursor = 0;
    assert(ms912x_scan_row_next_run(old, cur, 40, 39, 8, 2, &cursor, &first, &end));
    assert(first == 0 && end == 8);
    assert(ms912x_scan_row_next_run(old, cur, 40, 39, 8, 2, &cursor, &first, &end));
    assert(first == 16 && end == 24);
    assert(!ms912x_scan_row_next_run(old, cur, 40, 39, 8, 2, &cursor, &first, &end));

    memset(cur, 0, sizeof(cur));
    cur[18] = 1;
    cur[23] = 1;
    cursor = 16;
    assert(ms912x_scan_row_next_run(old, cur, 40, 23, 8, 0,
                                    &cursor, &first, &end));
    assert(first == 16 && end == 24);
    assert(!ms912x_scan_row_next_run(old, cur, 40, 23, 8, 0,
                                     &cursor, &first, &end));
    puts("scan runs: RGB, tolerance, block granularity, gaps, and tail pass");
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
static void benchmark_downscale(void) {
    static const unsigned dimensions[][4] = {
        {1920, 1080, 1280, 720}, {1366, 768, 912, 512}, {1280, 720, 856, 480},
        {128, 128, 88, 86}, {33, 35, 24, 24}, {7, 5, 5, 4},
    };
    u32 *src = malloc((size_t)1920 * 1080 * sizeof(*src));
    u32 *dst = malloc((size_t)1920 * 1080 * sizeof(*dst));
    u32 *working = malloc((size_t)1920 * 1080 * sizeof(*working));
    const char *filter = getenv("MS912X_BENCH_BACKEND");

    assert(src && dst && working);
    for (unsigned i = 0; i < 1920 * 1080; i++)
        src[i] = random32();
    for (unsigned test = 0; test < ARRAY_SIZE(dimensions); test++) {
        unsigned sw = dimensions[test][0], sh = dimensions[test][1];
        unsigned dw = dimensions[test][2], dh = dimensions[test][3];
        unsigned iterations = test < 3 ? (test ? 64 : 24) : (test == 3 ? 2048 : 32768);

        for (unsigned alias = 0; alias < 2; alias++)
        for (unsigned k = 0; k < ARRAY_SIZE(downscale_backends); k++) {
            const struct downscale_backend *b = &downscale_backends[k];
            double before[7], after[7];
            char label[96];

            if (!downscale_backend_available(b) || (filter && strcmp(filter, b->name)))
                continue;
            /* Warm both functions, then alternate order to limit cache/frequency bias. */
            b->before(src, dst, sw, sh, dw, dh, 1920, 1920);
            b->run(src, dst, sw, sh, dw, dh, 1920, 1920);
            for (unsigned rep = 0; rep < 7; rep++)
                for (unsigned j = 0; j < 2; j++) {
                    bool current = (rep + j) & 1;
                    downscale_fn fn = current ? b->run : b->before;
                    const u32 *input = src;
                    u32 *output = dst;
                    if (alias) {
                        memcpy(working, src, (size_t)1920 * sh * 4);
                        input = output = working;
                    }
                    double start = now_ns();

                    for (unsigned i = 0; i < iterations; i++)
                        fn(input, output, sw, sh, dw, dh, 1920, 1920);
                    (current ? after : before)[rep] = now_ns() - start;
                }
            snprintf(label, sizeof(label), "downscale%s %ux%u->%ux%u",
                     alias ? "-inplace" : "", sw, sh, dw, dh);
            report(b->name, label, before, after, (double)sw * sh * iterations);
            printf("  %s %.3f -> %.3f ms/frame (source pixels); "
                   "%.2f%% of one core at 60 updates/s\n", b->name,
                   before[3] / iterations / 1e6, after[3] / iterations / 1e6,
                   after[3] / iterations * 60.0 / 1e7);
        }
    }
    free(working);
    free(dst);
    free(src);
}

int main(int argc, char **argv) {
    setbuf(stdout, NULL);
    dispatch_tests();
    scan_run_tests();
    cursor_alpha_bounds_tests();
    cursor_alpha_offset_index_test();
    downscale_backend_tests();
    scale_geometry_tests();
    damage_tolerance_curve_tests();
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
    if (argc > 1 && !strcmp(argv[1], "--bench")) {
        if (!getenv("MS912X_BENCH_DOWNSCALE_ONLY"))
            for (unsigned i = 0; i < ARRAY_SIZE(backends); i++)
                if (available(&backends[i]) &&
                    (!getenv("MS912X_BENCH_BACKEND") ||
                     !strcmp(getenv("MS912X_BENCH_BACKEND"), backends[i].name)))
                    benchmark(&backends[i]);
        benchmark_downscale();
    }
    if (samples)
        fclose(samples);
    return 0;
}
