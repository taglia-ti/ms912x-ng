/* SPDX-License-Identifier: GPL-2.0-only */
#include <stdio.h>
#include <assert.h>
#include <time.h>
#include <sched.h>
#include <sys/mman.h>
#include <unistd.h>
#ifdef CONFIG_X86_64
#include <asm/cpufeature.h>
#endif
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

/* Adapters supply zero crop offsets; all filtering stays in driver entry points. */
typedef void (*downscale_fn)(const u32 *, u32 *, unsigned, unsigned, unsigned,
                             unsigned, unsigned, unsigned);
typedef void (*downscale_region_fn)(const u32 *, u32 *, unsigned, unsigned,
                                    unsigned, unsigned, unsigned, unsigned,
                                    unsigned, unsigned);
#define BACKEND_ADAPTER(name) \
static void run_##name(const u32 *s, u32 *d, unsigned sw, unsigned sh, \
    unsigned dw, unsigned dh, unsigned ss, unsigned ds) { \
    ms912x_downscale_xrgb8888_3to2_##name##_region(s,d,sw,sh,dw,dh,ss,ds,0,0); \
}
BACKEND_ADAPTER(scalar)
#ifdef CONFIG_X86_64
BACKEND_ADAPTER(sse2)
BACKEND_ADAPTER(ssse3)
BACKEND_ADAPTER(avx2)
#else
BACKEND_ADAPTER(neon)
#endif
struct downscale_backend {
    const char *name;
    downscale_fn run;
    downscale_region_fn region;
};
#define BACKEND(name, label) {label, run_##name, ms912x_downscale_xrgb8888_3to2_##name##_region}
static const struct downscale_backend downscale_backends[] = {
    BACKEND(scalar, "scalar"),
#ifdef CONFIG_X86_64
    BACKEND(sse2, "SSE2"), BACKEND(ssse3, "SSSE3"), BACKEND(avx2, "AVX2"),
#else
    BACKEND(neon, "NEON"),
#endif
};
static u32 random_state = 0xf314ac23;
static u32 random32(void) {
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    return random_state ^= random_state << 5;
}
static bool downscale_backend_available(const struct downscale_backend *backend)
{
    (void)backend;
#ifdef CONFIG_X86_64
    if (!strcmp(backend->name, "AVX2"))
        return __builtin_cpu_supports("avx2");
    if (!strcmp(backend->name, "SSE2"))
        return __builtin_cpu_supports("sse2");
    if (!strcmp(backend->name, "SSSE3"))
        return __builtin_cpu_supports("ssse3");
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
        downscale_reference(src, expected, sw, sh, dw, dh, ss, ds,
                            MS912X_TEST_DOWNSCALE_AREA);
        memset(actual, 0xa5, dc * 4);
        b->run(src, actual, sw, sh, dw, dh, ss, ds);
        assert(!memcmp(actual, expected, dc * 4));
        for (unsigned packed = 0; packed < 2; packed++) {
            unsigned stride = packed ? dw : ss;
            memcpy(inplace, src, sc * 4);
            b->run(inplace, inplace, sw, sh, dw, dh, ss, stride);
            for (unsigned y = 0; y < dh; y++)
                assert(!memcmp(inplace + y * stride, expected + y * ds, dw * 4));
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
    run_scalar(src,expected,4,4,2,2,4,2);
    bench_neon_present = false;
    run_neon(src,out,4,4,2,2,4,2);
    assert(!memcmp(out,expected,sizeof(out)));
    bench_neon_present = true; bench_simd_allowed = false;
    run_neon(src,out,4,4,2,2,4,2);
    assert(!memcmp(out,expected,sizeof(out)));
    bench_simd_allowed = true;
#endif
    downscale_rounding_tests();
    downscale_guard_tests();
    downscale_region_tests();
    downscale_extrema_tests();
    puts("SIMD 3->2 downscale: 263 geometries, odd edges, strides, in-place/packed output, empty dimensions pass");
}

#ifdef CONFIG_X86_64
static void downscale_dispatch_tests(void)
{
	u32 src[24 * 24], expected[16 * 16], actual[16 * 16];
	unsigned int saved_features = bench_cpu_features;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(src); i++)
		src[i] = random32();
	run_scalar(src, expected, 24, 24, 16, 16,
						    24, 16);
	bench_cpu_features = 1U << X86_FEATURE_SSSE3;
	ms912x_downscale_xrgb8888_3to2_region(src, actual, 24, 24, 16, 16,
						    24, 16, 0, 0);
	assert(!memcmp(actual, expected, sizeof(actual)));
	bench_cpu_features = saved_features;
	puts("SSSE3 downscale dispatch: 3:2 selector routes to the SSSE3 oracle-equivalent kernel");
}
#else
static void downscale_dispatch_tests(void) { }
#endif

static void scale_geometry_tests(void)
{
    unsigned int input_width, input_height;

    ms912x_scale_input_geometry(1920, 1080, MS912X_SCALE_MODE_DOWNSCALE_2_TO_1,
                                &input_width, &input_height);
    assert(input_width == 960 && input_height == 540);
    ms912x_scale_input_geometry(1920, 1080, MS912X_SCALE_MODE_DOWNSCALE_3_TO_2,
                                &input_width, &input_height);
    assert(input_width == 1280 && input_height == 720);
    ms912x_scale_input_geometry(1920, 1080, MS912X_SCALE_MODE_DOWNSCALE_5_TO_4,
                                &input_width, &input_height);
    assert(input_width == 1536 && input_height == 864);
    ms912x_scale_input_geometry(1920, 1080, MS912X_SCALE_MODE_NATIVE,
                                &input_width, &input_height);
    assert(input_width == 1920 && input_height == 1080);
    ms912x_scale_input_geometry(1366, 768, MS912X_SCALE_MODE_DOWNSCALE_3_TO_2,
                                &input_width, &input_height);
    assert(input_width == 912 && input_height == 512);
    puts("scale presets: 50, 67, 80, and 100 geometry pass");
}

static void damage_tolerance_curve_tests(void)
{
    assert(ms912x_damage_scan_auto_tolerance(0, 100, 200, 1, 31) == 1);
    assert(ms912x_damage_scan_auto_tolerance(124, 100, 200, 1, 31) == 1);
    assert(ms912x_damage_scan_auto_tolerance(125, 100, 200, 1, 31) == 3);
    assert(ms912x_damage_scan_auto_tolerance(150, 100, 200, 1, 31) == 7);
    assert(ms912x_damage_scan_auto_tolerance(175, 100, 200, 1, 31) == 15);
    assert(ms912x_damage_scan_auto_tolerance(200, 100, 200, 1, 31) == 31);
    assert(ms912x_damage_scan_auto_tolerance(250, 100, 200, 1, 31) == 31);
    assert(ms912x_damage_scan_auto_tolerance(1000, 100, 200, 17, 17) == 15);
    assert(ms912x_damage_scan_tolerance_bucket(255) == 31);
    for (unsigned minimum = 0; minimum <= 31; minimum++)
        for (unsigned maximum = minimum; maximum <= 31; maximum++) {
            unsigned previous = 0;
            unsigned low = ms912x_damage_scan_tolerance_bucket(minimum);
            unsigned high = ms912x_damage_scan_tolerance_bucket(maximum);
            for (unsigned pressure = 0; pressure <= 300; pressure++) {
                unsigned current = ms912x_damage_scan_auto_tolerance(
                    pressure, 100, 200, minimum, maximum);
                assert(current >= previous && current >= low && current <= high);
                assert(current == 0 || current == 1 || current == 3 ||
                       current == 7 || current == 15 || current == 31);
                previous = current;
            }
            assert(previous == high);
        }
    for (unsigned t = 0; t <= 31; t++) {
        unsigned bucket_width = 1;
        while (bucket_width * 2 <= t + 1) bucket_width *= 2;
        for (unsigned a = 0; a < 256; a++)
            for (unsigned b = 0; b < 256; b++)
                for (unsigned channel = 0; channel < 32; channel += 8) {
                    bool changed = ((a ^ b) << channel) & ms912x_scan_uyvy_tolerance_mask(t);
                    assert(changed == (a / bucket_width != b / bucket_width));
                    assert(abs((int)a - (int)b) <= (int)t || changed);
                }
    }
    puts("damage tolerance buckets: watermarks, all ranges, monotonic levels and exhaustive UYVY comparison pass");
}

static double now_ns(void) {
    struct timespec t;
    assert(!clock_gettime(CLOCK_MONOTONIC_RAW, &t));
    return t.tv_sec * 1e9 + t.tv_nsec;
}
static int cmp_double(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}
static void benchmark_downscale(void) {
    const unsigned sw=1920, sh=1080, dw=1280, dh=720, iterations=16;
    u32 *src=malloc(sw*sh*4), *dst=malloc(dw*dh*4);
    assert(src && dst);
    for (unsigned i=0;i<sw*sh;i++) src[i]=random32();
    puts("backend,workload,input_pixels,median_ns,input_mpixels_per_s");
    for (unsigned k=0;k<ARRAY_SIZE(downscale_backends);k++) {
        const struct downscale_backend *b=&downscale_backends[k];
        double samples[7];
        if (!downscale_backend_available(b)) continue;
        b->run(src,dst,sw,sh,dw,dh,sw,dw);
        for (unsigned rep=0;rep<7;rep++) {
            double start=now_ns();
            for (unsigned i=0;i<iterations;i++) b->run(src,dst,sw,sh,dw,dh,sw,dw);
            samples[rep]=(now_ns()-start)/iterations;
        }
        qsort(samples,7,sizeof(double),cmp_double);
        printf("%s,3to2,%u,%.0f,%.2f\n",b->name,sw*sh,samples[3],sw*sh*1000.0/samples[3]);
    }
    free(dst); free(src);
}
int main(int argc, char **argv) {
    dispatch_tests();
    cursor_alpha_bounds_tests();
    cursor_alpha_offset_index_test();
    downscale_backend_tests();
    downscale_dispatch_tests();
    scale_geometry_tests();
    damage_tolerance_curve_tests();
    puts("PASS current driver dispatch, scale, cursor and 3:2 region entry points");
    if (argc>1 && !strcmp(argv[1],"--bench")) benchmark_downscale();
    return 0;
}
