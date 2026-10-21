// SPDX-License-Identifier: GPL-2.0-only
/* Independent filter oracle plus production crop, parameter and dispatch hooks. */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <time.h>
#include <sys/mman.h>
#include <unistd.h>
#ifdef CONFIG_ARM
#include <sys/auxv.h>
#include <asm/hwcap.h>
#endif
#include "ms912x_downscale.h"
#ifdef CONFIG_X86_64
#include <asm/cpufeature.h>
#endif

#define DRM_RECT_INIT(x, y, w, h) ((struct drm_rect){(x), (y), (x) + (w), (y) + (h)})
#define min_t(type, a, b) min((type)(a), (type)(b))

bool bench_simd_allowed = true, bench_neon_present = true;
unsigned int bench_cpu_features;
static unsigned int cases, random_state = 0x912580;
typedef void (*filter_fn)(const u32 *, u32 *, unsigned, unsigned, unsigned,
                          unsigned, unsigned, unsigned);
typedef void (*region_fn)(const u32 *, u32 *, unsigned, unsigned, unsigned,
                          unsigned, unsigned, unsigned, unsigned, unsigned);
struct backend { const char *name; filter_fn run; region_fn region; };
static const struct backend backends[] = {
    {"scalar", ms912x_downscale_xrgb8888_5to4_scalar, ms912x_downscale_xrgb8888_5to4_scalar_region},
#ifdef CONFIG_X86_64
    {"SSE2", ms912x_downscale_xrgb8888_5to4_sse2, ms912x_downscale_xrgb8888_5to4_sse2_region},
    {"SSSE3", ms912x_downscale_xrgb8888_5to4_ssse3, ms912x_downscale_xrgb8888_5to4_ssse3_region},
    {"AVX2", ms912x_downscale_xrgb8888_5to4_avx2, ms912x_downscale_xrgb8888_5to4_avx2_region},
#elif MS912X_HAS_NEON
    {"NEON", ms912x_downscale_xrgb8888_5to4_neon, ms912x_downscale_xrgb8888_5to4_neon_region},
#endif
};

/* The common dispatcher must route 5:4 away from all existing ratio entries. */
void ms912x_downscale_xrgb8888_2x(const u32 *s, u32 *d, unsigned a, unsigned b,
                                unsigned c, unsigned e, unsigned f, unsigned g) { abort(); }
void ms912x_downscale_xrgb8888_3to2(const u32 *s, u32 *d, unsigned a, unsigned b,
                                  unsigned c, unsigned e, unsigned f, unsigned g) { abort(); }
void ms912x_downscale_xrgb8888_3to2_region(const u32 *s, u32 *d, unsigned a, unsigned b,
    unsigned c, unsigned e, unsigned f, unsigned g, unsigned h, unsigned i) { abort(); }

struct ms912x_device {
    enum ms912x_scale_mode scale_mode;
    u32 *damage_scan_snapshot_back, *latest_xrgb;
    int snapshot_lock;
};
#define lockdep_assert_held(lock) assert(*(lock))
static struct ms912x_device *capturing_device;
struct drm_framebuffer { unsigned int width, height, pitches[1]; };
struct iosys_map { const void *vaddr; };
static void iosys_map_memcpy_from(void *dst, const struct iosys_map *map, size_t off, size_t n)
{
    memcpy(dst, (const u8 *)map->vaddr + off, n);
    if (capturing_device) capturing_device->scale_mode = MS912X_SCALE_MODE_NATIVE;
}
static bool drm_rect_intersect(struct drm_rect *r, const struct drm_rect *b)
{
    r->x1 = max(r->x1, b->x1); r->y1 = max(r->y1, b->y1);
    r->x2 = min(r->x2, b->x2); r->y2 = min(r->y2, b->y2);
    return r->x1 < r->x2 && r->y1 < r->y2;
}
struct kernel_param { void *arg; };
static struct { unsigned int scale, scale_generation; } ms912x_parameters;
static unsigned int hotplugs;
static bool sysfs_streq(const char *a, const char *b)
{ size_t n = strlen(b); return !strncmp(a, b, n) && (!a[n] || (a[n] == '\n' && !a[n + 1])); }
static void atomic_inc(unsigned int *p) { ++*p; }
static void ms912x_notify_devices_hotplug(void) { ++hotplugs; }
#define WRITE_ONCE(x, v) ((x) = (v))
#include "scale-hooks.h"

static unsigned random32(void)
{ random_state ^= random_state << 13; random_state ^= random_state >> 17; random_state ^= random_state << 5; return random_state; }
static bool available(const struct backend *b)
{
#ifdef CONFIG_X86_64
    if (!strcmp(b->name, "AVX2")) return __builtin_cpu_supports("avx2");
    if (!strcmp(b->name, "SSE2")) return __builtin_cpu_supports("sse2");
    if (!strcmp(b->name, "SSSE3")) return __builtin_cpu_supports("ssse3");
#elif MS912X_HAS_NEON
    if (!strcmp(b->name, "NEON")) return bench_neon_present;
#endif
    return true;
}

#ifndef MS912X_5TO4_AREA_FILTER
static unsigned cubic_channel(const unsigned *v, unsigned phase)
{
    static const int coeff[4][4] = {{-6,123,12,-1}, {-9,93,50,-6},
                                   {-6,50,93,-9}, {-1,12,123,-6}};
    int sum = 64;
    for (unsigned i = 0; i < 4; i++) sum += coeff[phase][i] * (int)v[i];
    int q = sum >= 0 ? sum / 128 : -((-sum + 127) / 128);
    return q < 0 ? 0U : q > 255 ? 255U : (unsigned)q;
}
#endif

static u32 reference(const u32 *src, unsigned sw, unsigned sh, unsigned ss, unsigned x, unsigned y)
{
    unsigned sx = 5*x/4, sy = 5*y/4;
    u32 out = 0;
    for (unsigned shift = 0; shift < 32; shift += 8) {
#ifdef MS912X_5TO4_AREA_FILTER
        unsigned sum = 0;
        for (unsigned j = 0; j < 2; j++) for (unsigned i = 0; i < 2; i++) {
            unsigned wx = i ? (x%4 + 1) : (4 - x%4);
            unsigned wy = j ? (y%4 + 1) : (4 - y%4);
            sum += wx*wy*((src[min(sy+j,sh-1)*ss+min(sx+i,sw-1)]>>shift)&255);
        }
        out |= ((sum+12)/25) << shift;
#else
        unsigned horizontal[4];
        for (unsigned j = 0; j < 4; j++) {
            unsigned values[4];
            int iy = (int)sy + (int)j - 1;
            iy = max(0, min(iy, (int)sh - 1));
            for (unsigned i = 0; i < 4; i++) {
                int ix = (int)sx + (int)i - 1;
                ix = max(0, min(ix, (int)sw - 1));
                values[i] = (src[(unsigned)iy*ss+(unsigned)ix]>>shift)&255;
            }
            horizontal[j] = cubic_channel(values, x%4);
        }
        out |= cubic_channel(horizontal, y%4) << shift;
#endif
    }
    return out;
}

static void geometry(unsigned sw, unsigned sh, unsigned extra, unsigned offset)
{
    unsigned dw = ((4*sw+4)/5 + extra + 3) & ~3U, dh = (4*sh+4)/5;
    unsigned ss = max(sw, dw) + 11, ds = dw + 7;
    size_t sc = (size_t)ss*sh, dc = (size_t)ds*dh;
    u32 *allocation = malloc((sc+8)*4), *src = allocation + offset;
    u32 *expected = malloc(dc*4), *actual = malloc(dc*4), *work = malloc(sc*4);
    assert(allocation && expected && actual && work);
    for (size_t i = 0; i < sc; i++) src[i] = random32();
    memset(expected, 0xa5, dc*4);
    for (unsigned y = 0; y < dh; y++) for (unsigned x = 0; x < dw; x++)
        expected[y*ds+x] = reference(src,sw,sh,ss,x,y);
    for (unsigned k = 0; k < ARRAY_SIZE(backends); k++) {
        const struct backend *b = &backends[k];
        if (!available(b)) continue;
        memset(actual,0xa5,dc*4);
        b->run(src,actual,sw,sh,dw,dh,ss,ds);
        if (memcmp(expected,actual,dc*4)) {
            fprintf(stderr,"5:4 %s separate %ux%u -> %ux%u\n",b->name,sw,sh,dw,dh);
            abort();
        }
        for (unsigned packed = 0; packed < 2; packed++) {
            unsigned stride = packed ? dw : ss;
            memcpy(work,src,sc*4);
            b->run(work,work,sw,sh,dw,dh,ss,stride);
            for (unsigned y = 0; y < dh; y++) if (memcmp(work+y*stride,expected+y*ds,dw*4)) {
                fprintf(stderr,"5:4 %s in-place %ux%u row=%u packed=%u\n",b->name,sw,sh,y,packed);
                abort();
            }
        }
    }
    free(work); free(actual); free(expected); free(allocation); cases++;
}

static void crop_tests(void)
{
    enum {SW=67, SH=43, SS=79, DW=56, DH=35};
    u32 src[SS*SH], expected[DW*DH], out[DW*DH];
    u32 *latest = calloc(MS912X_MAX_WIDTH*SH,4), *staging = calloc(MS912X_MAX_WIDTH*SH,4);
    struct ms912x_device device = {
        .scale_mode = MS912X_SCALE_MODE_DOWNSCALE_5_TO_4,
        .damage_scan_snapshot_back = staging, .latest_xrgb = latest, .snapshot_lock = 1,
    };
    struct drm_framebuffer fb = {SW,SH,{SS*4}};
    struct iosys_map map = {src};
    assert(latest && staging);
    for (unsigned i = 0; i < ARRAY_SIZE(src); i++) src[i] = random32();
    for (unsigned y = 0; y < DH; y++) for (unsigned x = 0; x < DW; x++)
        expected[y*DW+x] = reference(src,SW,SH,SS,x,y);
    for (unsigned y1 = 0; y1 < DH; y1 += 4) for (unsigned x1 = 0; x1 < DW; x1 += 4) {
        struct drm_rect rect = DRM_RECT_INIT(x1,y1,min(11U,DW-x1),min(7U,DH-y1));
        memset(latest,0xa5,MS912X_MAX_WIDTH*SH*4);
        device.scale_mode = MS912X_SCALE_MODE_DOWNSCALE_5_TO_4;
        capturing_device = &device;
        ms912x_snapshot_capture_damage(&device,&fb,&map,&rect,device.scale_mode);
        capturing_device = NULL;
        assert(device.scale_mode == MS912X_SCALE_MODE_NATIVE);
        device.scale_mode = MS912X_SCALE_MODE_DOWNSCALE_5_TO_4;
        for (unsigned y = 0; y < DH; y++) for (unsigned x = 0; x < DW; x++) {
            bool inside = x >= x1 && x < (unsigned)rect.x2 && y >= y1 && y < (unsigned)rect.y2;
            assert(latest[y*MS912X_MAX_WIDTH+x] == (inside ? expected[y*DW+x] : 0xa5a5a5a5));
        }
        unsigned cx=x1/4*5,cy=y1/4*5,sx=cx?cx-1:0,sy=cy?cy-1:0;
        unsigned cw=min(cx+(drm_rect_width(&rect)*5+3)/4+1,(unsigned)SW)-sx;
        unsigned ch=min(cy+(drm_rect_height(&rect)*5+3)/4+1,(unsigned)SH)-sy;
        for (unsigned y=0;y<ch;y++) memcpy(staging+y*MS912X_MAX_WIDTH,src+(sy+y)*SS+sx,cw*4);
        for (unsigned k=0;k<ARRAY_SIZE(backends);k++) if (available(&backends[k])) {
            memset(out,0xa5,sizeof(out));
            backends[k].region(staging,out,cw,ch,drm_rect_width(&rect),drm_rect_height(&rect),
                               MS912X_MAX_WIDTH,DW,cx-sx,cy-sy);
            for (unsigned y=0;y<(unsigned)drm_rect_height(&rect);y++)
                assert(!memcmp(out+y*DW,expected+(y1+y)*DW+x1,drm_rect_width(&rect)*4));
        }
        cases++;
    }
    /* A one-pixel source change must be covered by the production damage mapping. */
    for (unsigned sy=0;sy<SH;sy+=3) for (unsigned sx=0;sx<SW;sx+=3) {
        u32 saved=src[sy*SS+sx];
        src[sy*SS+sx] ^= 0xffffffffU;
        struct drm_rect rect=DRM_RECT_INIT(sx,sy,1,1);
        assert(ms912x_rect_map_output_to_input(&rect,SW,SH,DW,DH,device.scale_mode));
        for (unsigned y=0;y<DH;y++) for (unsigned x=0;x<DW;x++) {
            u32 changed=reference(src,SW,SH,SS,x,y);
            if(changed!=expected[y*DW+x])
                assert((int)x>=rect.x1 && (int)x<rect.x2 && (int)y>=rect.y1 && (int)y<rect.y2);
        }
        src[sy*SS+sx]=saved; cases++;
    }
    free(staging); free(latest);
}

static void guard_tests(void)
{
    size_t page=(size_t)sysconf(_SC_PAGESIZE);
    for(unsigned w=1;w<=129;w+=2) for(unsigned h=1;h<=7;h++) {
        unsigned dw=(4*w+4)/5+1,dh=(4*h+4)/5,stride=2*page/4;
        u8 *input=mmap(NULL,page*2*h,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        u8 *output=mmap(NULL,page*2*dh,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        assert(input!=MAP_FAILED && output!=MAP_FAILED);
        for(unsigned y=0;y<h;y++) assert(!mprotect(input+2*y*page,page,PROT_READ|PROT_WRITE));
        for(unsigned y=0;y<dh;y++) assert(!mprotect(output+2*y*page,page,PROT_READ|PROT_WRITE));
        for(unsigned edge=0;edge<2;edge++) {
            u32 *src=(u32 *)(input+(edge?page-w*4:0));
            u32 *out=(u32 *)(output+page-dw*4);
            for(unsigned y=0;y<h;y++) for(unsigned x=0;x<w;x++) src[y*stride+x]=random32();
            for(unsigned k=0;k<ARRAY_SIZE(backends);k++) if(available(&backends[k])) {
                backends[k].run(src,out,w,h,dw,dh,stride,stride);
                for(unsigned y=0;y<dh;y++) for(unsigned x=0;x<dw;x++)
                    assert(out[y*stride+x]==reference(src,w,h,stride,x,y));
            }
        }
        assert(!munmap(input,page*2*h) && !munmap(output,page*2*dh)); cases++;
    }
}

static void routing_tests(void)
{
    u32 src[20*20],out[16*16],expected[16*16];
    for(unsigned i=0;i<ARRAY_SIZE(src);i++) src[i]=random32();
    ms912x_downscale_xrgb8888_5to4_scalar(src,expected,20,20,16,16,20,16);
    ms912x_downscale_xrgb8888(MS912X_SCALE_MODE_DOWNSCALE_5_TO_4,src,out,20,20,16,16,20,16);
    assert(!memcmp(out,expected,sizeof(out)));
#ifdef CONFIG_X86_64
    unsigned int saved_features=bench_cpu_features;
    bench_cpu_features=1U<<X86_FEATURE_SSSE3;
    ms912x_downscale_xrgb8888(MS912X_SCALE_MODE_DOWNSCALE_5_TO_4,src,out,20,20,16,16,20,16);
    assert(!memcmp(out,expected,sizeof(out)));
    bench_cpu_features=saved_features;
#endif
    bench_simd_allowed=false;
    ms912x_downscale_xrgb8888(MS912X_SCALE_MODE_DOWNSCALE_5_TO_4,src,out,20,20,16,16,20,16);
    assert(!memcmp(out,expected,sizeof(out)));
    bench_simd_allowed=true;
#if MS912X_HAS_NEON
    bool neon_present=bench_neon_present;
    bench_neon_present=false;
    ms912x_downscale_xrgb8888_5to4_neon(src,out,20,20,16,16,20,16);
    assert(!memcmp(out,expected,sizeof(out)));
    bench_neon_present=neon_present;
#endif
    for(unsigned k=0;k<ARRAY_SIZE(backends);k++) if(available(&backends[k])) {
        for(unsigned i=0;i<4;i++) {
            unsigned dims[4]={1,1,1,1}; dims[i]=0;
            backends[k].run(NULL,NULL,dims[0],dims[1],dims[2],dims[3],1,1);
        }
        backends[k].region(NULL,NULL,1,1,1,1,1,1,1,0);
        backends[k].region(NULL,NULL,1,1,1,1,1,1,0,1);
    }
    struct kernel_param kp={&ms912x_parameters.scale};
    assert(!ms912x_scale_set("80\n",&kp) && ms912x_parameters.scale==80 && hotplugs==1);
    assert(!ms912x_scale_set("80",&kp) && hotplugs==1);
    assert(ms912x_scale_set("79",&kp)==-EINVAL && ms912x_parameters.scale==80);
    assert(!ms912x_scale_set("auto",&kp) && ms912x_parameters.scale==MS912X_SCALE_AUTO);
    assert(ms912x_parameters.scale_generation==2 && hotplugs==2);
    unsigned int scale_snapshot;
    int generation_snapshot;
    ms912x_scale_snapshot(&scale_snapshot, &generation_snapshot);
    assert(scale_snapshot==MS912X_SCALE_AUTO && generation_snapshot==2);
    assert(ms912x_scale_mode_from_setting(80)==MS912X_SCALE_MODE_DOWNSCALE_5_TO_4);
    assert(ms912x_scale_setting_for_mode(MS912X_SCALE_MODE_DOWNSCALE_5_TO_4)==80);
    for(int x=-32;x<=32;x++) {
        int expected_floor=x>=0?x*4/5:-((-x*4+4)/5);
        assert(ms912x_scale_coordinate(x,MS912X_SCALE_MODE_DOWNSCALE_5_TO_4)==expected_floor);
    }
    for(unsigned sum=0;sum<=6375;sum++) assert(((sum+12)*5243>>17)==(sum+12)/25);
}

static void extrema_tests(void)
{
    enum {SW=20,SH=20,DW=16,DH=16};
    u32 src[SW*SH],out[DW*DH],expected[DW*DH],work[SW*SH];
    for(unsigned horizontal=0;horizontal<16;horizontal++)
    for(unsigned vertical=0;vertical<16;vertical++) {
        for(unsigned y=0;y<SH;y++) for(unsigned x=0;x<SW;x++) {
            unsigned a=(horizontal>>(x&3))&1,b=(vertical>>(y&3))&1;
            src[y*SW+x]=(a?255U:0)|(b?0xff00U:0)|((a&b)?0xff0000U:0)|((a^b)?0xff000000U:0);
        }
        for(unsigned y=0;y<DH;y++) for(unsigned x=0;x<DW;x++)
            expected[y*DW+x]=reference(src,SW,SH,SW,x,y);
        for(unsigned k=0;k<ARRAY_SIZE(backends);k++) if(available(&backends[k])) {
            backends[k].run(src,out,SW,SH,DW,DH,SW,DW);
            assert(!memcmp(out,expected,sizeof(out)));
            memcpy(work,src,sizeof(src));
            backends[k].run(work,work,SW,SH,DW,DH,SW,DW);
            assert(!memcmp(work,expected,sizeof(expected)));
        }
        cases++;
    }
}

static double now(void)
{ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec+t.tv_nsec/1e9; }
static int compare_double(const void *a,const void *b)
{ double x=*(const double *)a,y=*(const double *)b; return (x>y)-(x<y); }
static void benchmark(void)
{
    const unsigned sizes[][2]={{1920,1080},{1280,720},{640,128},{128,128}};
    puts("5to4,backend,source,output,layout,median_us,Mpixel_per_s");
    for(unsigned s=0;s<ARRAY_SIZE(sizes);s++) {
        unsigned sw=sizes[s][0],sh=sizes[s][1],dw=((sw*4+4)/5+3)&~3U,dh=(sh*4+4)/5;
        size_t count=(size_t)MS912X_MAX_WIDTH*sh;
        u32 *src=malloc(count*4),*work=malloc(count*4),*out=malloc(count*4);
        assert(src&&work&&out);
        for(size_t i=0;i<count;i++) src[i]=random32();
        for(unsigned k=0;k<ARRAY_SIZE(backends);k++) if(available(&backends[k]))
        for(unsigned inplace=0;inplace<2;inplace++) {
            double samples[7];
            for(unsigned sample=0;sample<7;sample++) {
                double elapsed=0;
                for(unsigned repeat=0;repeat<4;repeat++) {
                    if(inplace) memcpy(work,src,count*4);
                    double start=now();
                    backends[k].run(inplace?work:src,inplace?work:out,sw,sh,dw,dh,
                                    MS912X_MAX_WIDTH,MS912X_MAX_WIDTH);
                    elapsed+=now()-start;
                }
                samples[sample]=elapsed/4;
            }
            qsort(samples,7,sizeof(*samples),compare_double);
            printf("5to4,%s,%ux%u,%ux%u,%s,%.2f,%.2f\n",backends[k].name,sw,sh,dw,dh,
                   inplace?"inplace":"separate",samples[3]*1e6,(double)dw*dh/samples[3]/1e6);
        }
        free(out);free(work);free(src);
    }
}

int main(int argc,char **argv)
{
#ifdef CONFIG_ARM
    bench_neon_present=!!(getauxval(AT_HWCAP)&HWCAP_NEON);
#endif
#ifdef CONFIG_X86_64
    if(__builtin_cpu_supports("sse2")) bench_cpu_features|=1U<<X86_FEATURE_XMM2;
    if(__builtin_cpu_supports("ssse3")) bench_cpu_features|=1U<<X86_FEATURE_SSSE3;
    if(__builtin_cpu_supports("avx2")) bench_cpu_features|=(1U<<X86_FEATURE_AVX)|(1U<<X86_FEATURE_AVX2);
#endif
    const unsigned heights[]={1,3,4,5,17,33};
    for(unsigned w=1;w<=129;w++) for(unsigned h=0;h<ARRAY_SIZE(heights);h++)
        geometry(w,heights[h],h&1,(w+h)&7);
    geometry(1366,768,0,1);geometry(1920,1080,0,3);geometry(1280,720,0,7);
    crop_tests();guard_tests();routing_tests();extrema_tests();
    printf("PASS 5:4: %u geometries/crops/guards; oracle, all phases/channels, in-place, "
           "packed output, damage mapping, scale=80, dispatch and fallback\n",cases);
    if(argc>1&&!strcmp(argv[1],"--benchmark")) benchmark();
    return 0;
}
