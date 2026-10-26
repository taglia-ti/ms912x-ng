/* SPDX-License-Identifier: GPL-2.0-only */
#include "ms912x_frame_damage.h"
#include "frame_pipeline.h"
#include "ms912x_color_core.h"
#include "ms912x_pacing_core.h"
#include "ms912x_scan_core.h"
#include "ms912x_scan_backend.h"
#include "ms912x_mode3_core.h"
#include "../../ms912x_scan_rects.h"
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned int mode3_encodes;
int __real_ms912x_core_mode3_encode_rectangles(
	uint8_t *packet, size_t capacity, const uint8_t *pixels, size_t stride,
	uint32_t width, uint32_t height, const struct ms912x_core_rect *rects,
	size_t rect_count);
int __wrap_ms912x_core_mode3_encode_rectangles(
	uint8_t *packet, size_t capacity, const uint8_t *pixels, size_t stride,
	uint32_t width, uint32_t height, const struct ms912x_core_rect *rects,
	size_t rect_count)
{
	++mode3_encodes;
	return __real_ms912x_core_mode3_encode_rectangles(packet, capacity, pixels,
								  stride, width, height,
								  rects, rect_count);
}

/* Check geometry, newest payload, complete changed coverage and batch trailers. */
static unsigned int check_packet(const uint8_t *packet, size_t length,
                                const uint32_t *pixels, unsigned int height,
                                unsigned int pattern)
{
    unsigned int count = 0;
    size_t at = 0;
    while (at < length) {
        assert(length - at >= 16 && packet[at] == 0xff && packet[at + 1] == 0);
        unsigned int x = (packet[at + 2] << 4) | (packet[at + 3] >> 4);
        unsigned int y = ((packet[at + 3] & 15) << 8) | packet[at + 4];
        unsigned int w = (packet[at + 5] << 4) | (packet[at + 6] >> 4);
        unsigned int h = ((packet[at + 6] & 15) << 8) | packet[at + 7];
        assert(y == 0 && h == height);
        assert(x == (pattern ? count * 32U : 0));
        assert(w == (pattern ? 16U : 128U));
        at += 8;
        assert(length - at >= (size_t)w * h * 2 + 8);
        for (unsigned int row = 0; row < h; ++row) {
            assert(!memcmp(packet + at, pixels + row * 512U + x / 2U, w * 2U));
            at += w * 2U;
        }
        assert(packet[at] == 0xff);
        assert(packet[at + 1] == (at + 8 == length ? 0xc0 : 0x80));
        for (unsigned int i = 2; i < 8; ++i) assert(packet[at + i] == 0);
        at += 8;
        ++count;
    }
    assert(at == length);
    return count;
}

static void fragmented_frame(unsigned int pattern)
{
    const unsigned int height = pattern == 2 ? 512 : 256;
    size_t capacity = ms912x_frame_damage_packet_capacity(1024, height);
    uint32_t *pixels = calloc(512U * height, sizeof(*pixels));
    uint8_t *packet = malloc(capacity);
    struct ms912x_frame_damage *damage = ms912x_frame_damage_create(1024, height);
    assert(pixels && packet && damage);
    assert(ms912x_frame_damage_prepare(damage, packet, capacity, (uint8_t *)pixels, 1) > 0);
    assert(!ms912x_frame_damage_finish(damage, 1, true));
    assert(ms912x_frame_damage_prepare_replay(damage, packet, capacity, 2) > 0);
    assert(!ms912x_frame_damage_finish(damage, 2, true));
    for (unsigned int y = 0; y < height; ++y)
        for (unsigned int x = 0; x < 512; ++x) {
            bool changed = pattern == 0 ? x < ((y & 1) ? 32U : 64U) :
                pattern == 1 ? (x % 16 < ((y & 1) ? 8U : 4U)) : x % 16 == 0;
            if (changed) pixels[y * 512U + x] = 0x10203040U + x + y;
        }
    int length = ms912x_frame_damage_prepare(damage, packet, capacity, (uint8_t *)pixels, 3);
    assert(length == (pattern == 0 ? 65552 : pattern == 1 ? 262656 : 524800));
    assert(check_packet(packet, length, pixels, height, pattern) == (pattern ? 32U : 1U));
    assert(!ms912x_frame_damage_finish(damage, 3, true));
    int replay = ms912x_frame_damage_prepare_replay(damage, packet, capacity, 4);
    assert(replay == length);
    assert(check_packet(packet, replay, pixels, height, pattern) == (pattern ? 32U : 1U));
    assert(!ms912x_frame_damage_finish(damage, 4, true));
    assert(!ms912x_frame_damage_prepare(damage, packet, capacity, (uint8_t *)pixels, 5));
    assert(!ms912x_frame_damage_prepare_replay(damage, packet, capacity, 5));
    ms912x_frame_damage_destroy(damage);
    free(packet);
    free(pixels);
}

static void bounded_storage(void)
{
    uint32_t baseline[64] = {0}, newest[64] = {0};
    struct ms912x_scan_rect_index *index = ms912x_scan_rect_index_alloc();
    struct drm_rect rects[3] = {{0}, {0}, {-1, -2, -3, -4}};
    size_t count = 0;
    assert(index);
    newest[0] = newest[16] = newest[32] = 1;
    assert(!ms912x_core_scan_words_backend(baseline, 64, newest, 64, 128, 1, 64, 1,
                                  UINT32_MAX, rects, 2, &count, index, MS912X_SCAN_AUTO));
    assert(count == 1 && rects[0].x1 == 0 && rects[0].x2 == 80);
    assert(rects[0].y1 == 0 && rects[0].y2 == 1);
    assert(rects[2].x1 == -1 && rects[2].y1 == -2 && rects[2].x2 == -3 && rects[2].y2 == -4);
    ms912x_scan_rect_index_free(index);
}

static void differential_scan_backends(void)
{
    uint32_t old[513], newest[513];
    unsigned int state = 0x912c;
    for (unsigned int width = 1; width <= 513; ++width) {
        for (unsigned int i = 0; i < 513; ++i) {
            state ^= state << 13; state ^= state >> 17; state ^= state << 5;
            old[i] = state;
            newest[i] = i % 13 ? state : state ^ (1U << (i % 32));
        }
        for (unsigned int tolerance = 0; tolerance <= 127; tolerance = tolerance * 2 + 1) {
            uint32_t mask = ms912x_scan_uyvy_tolerance_mask(tolerance);
            struct ms912x_scan_row_result reference, result;
            ms912x_core_scan_row(old, newest, width, mask, &reference, MS912X_SCAN_SCALAR);
            for (int backend = 0; backend < MS912X_SCAN_BACKEND_COUNT; ++backend) {
                if (backend == MS912X_SCAN_MEMCMP || !ms912x_core_scan_backend_supported(backend)) continue;
                ms912x_core_scan_row(old, newest, width, mask, &result, backend);
                assert(!memcmp(&reference, &result, sizeof(result)));
            }
        }
    }
}

#include "../test_scan_backend_batches.h"

static void backend_transactions(void)
{
    uint32_t pixels[65 * 3] = {0};
    size_t capacity = ms912x_frame_damage_packet_capacity(130, 3);
    uint8_t *packet = malloc(capacity);
    struct ms912x_scan_rect_index *index = ms912x_scan_rect_index_alloc();
    assert(packet && index);
    {
        struct ms912x_frame_damage *damage = ms912x_frame_damage_create(130, 3);
        assert(damage);
        memset(pixels, 0, sizeof(pixels));
        assert(ms912x_frame_damage_prepare(damage, packet, capacity, (uint8_t *)pixels, 1) > 0);
        assert(!ms912x_frame_damage_finish(damage, 1, true));
        assert(ms912x_frame_damage_prepare_replay(damage, packet, capacity, 2) > 0);
        assert(!ms912x_frame_damage_finish(damage, 2, true));
        pixels[65 + 17] = 0x01000000U; /* Y1-only change: exact MEMCMP must retain it. */
        unsigned int x = 32;
        unsigned int w = 16;
        int length = ms912x_frame_damage_prepare(damage, packet, capacity, (uint8_t *)pixels, 3);
        assert(length == (int)(16 + w * 2));
        assert(((packet[2] << 4) | (packet[3] >> 4)) == (int)x);
        assert(((packet[3] & 15) << 8 | packet[4]) == 1);
        assert(((packet[5] << 4) | (packet[6] >> 4)) == (int)w);
        assert(packet[7] == 1 && packet[length - 7] == 0xc0);
        assert(!memcmp(packet + 8, pixels + 65 + x / 2, w * 2));
        assert(!ms912x_frame_damage_finish(damage, 3, true));
        assert(ms912x_frame_damage_prepare_replay(damage, packet, capacity, 4) == length);
        assert(!ms912x_frame_damage_finish(damage, 4, true));
        assert(!ms912x_frame_damage_prepare(damage, packet, capacity, (uint8_t *)pixels, 5));

        ms912x_frame_damage_destroy(damage);
    }
    for (int backend = 0; backend < MS912X_SCAN_BACKEND_COUNT; backend++) {
        if (!ms912x_core_scan_backend_supported(backend)) continue;
        /* Explicit scan geometry uses MEMCMP's exact mask even when passed zero. */
        uint32_t baseline[65 * 3] = {0};
        struct drm_rect rect;
        size_t count;
        assert(!ms912x_core_scan_words_backend(baseline, 195, pixels, 195,
                    130, 3, 65, 1, 0, &rect, 1, &count, index, backend));
        assert(count == (backend == MS912X_SCAN_MEMCMP ? 1U : 0U));
        if (count) assert(rect.x1 == 0 && rect.x2 == 64 && rect.y1 == 1 && rect.y2 == 2);
    }
    ms912x_scan_rect_index_free(index);
    free(packet);
}

static void paced_submission(void)
{
    struct ms912x_android_frame_pipeline *pipeline = ms912x_android_frame_pipeline_create(64, 4, 1);
    struct ms912x_android_frame_info info;
    uint8_t rgba[64 * 4 * 4] = {0}, packet[576];
    uint64_t now = 1000000000ULL, wait = 0, deadline;
    uint32_t red = ms912x_core_rgb_pair_to_uyvy(255, 0, 0, 0, 0, 0);
    assert(pipeline);
#define OFFER() assert(!ms912x_android_frame_pipeline_offer_rgba(pipeline, rgba, sizeof(rgba), 0, 256, 4, 0, 0, 64, 4, 1))
#define ENCODE() ms912x_android_frame_pipeline_encode_paced(pipeline, packet, sizeof(packet), &info, now, 60, &wait)
#define FINISH() assert(!ms912x_android_frame_pipeline_complete(pipeline, info.sequence, 1))
    OFFER();
    assert(ENCODE() == 528 && wait == 0); FINISH();
    assert(ENCODE() == 528 && wait == 0); FINISH();
    assert(ENCODE() == 0 && wait == 0);
    rgba[2 * 4] = 255; OFFER();
    assert(ENCODE() == 0 && wait == 8333333);
    deadline = now + wait;
    unsigned int encodes = mode3_encodes;
    ++now;
    assert(ENCODE() == 0 && now + wait == deadline);
    assert(mode3_encodes == encodes);
    rgba[20 * 4] = 255; OFFER();
    assert(ENCODE() == 0 && now + wait == deadline);
    now = deadline;
    assert(ENCODE() == 80 && wait == 0 && info.sequence == 3);
    assert(!memcmp(packet + 8 + 20 * 2, &red, sizeof(red))); FINISH();
    assert(ENCODE() == 80 && wait == 0); FINISH();
    assert(ENCODE() == 0);
    for (unsigned int i = 0; i < 10000; ++i)
        ms912x_android_frame_pipeline_record_usb(pipeline, 1, 1000000000ULL);
    for (unsigned int i = 64; i < 256; ++i) rgba[i * 4] = 255;
    OFFER();
    assert(ENCODE() == 400 && wait == 0); FINISH();
    assert(ENCODE() == 400 && wait == 0); FINISH();
    ms912x_android_frame_pipeline_record_usb(pipeline, MS912X_ANDROID_MAX_FRAME_BYTES, 1);
    rgba[4 * 4] = 255; OFFER();
    assert(ENCODE() == 0 && wait > 0);
    ms912x_android_frame_pipeline_invalidate_baseline(pipeline);
    assert(ENCODE() == 528 && wait == 0); FINISH();
    assert(ENCODE() == 528 && wait == 0); FINISH();
    ms912x_android_frame_pipeline_destroy(pipeline);
#undef OFFER
#undef ENCODE
#undef FINISH
}

static void scan_options(void)
{
    struct ms912x_android_scan_options options;
    const unsigned int expected[][4] = {{0},{1,5,0,0},{2,2,3,15},{3,1,0,0}};
    for (unsigned int profile = 1; profile < 4; profile++) {
        assert(!ms912x_android_profile_defaults(profile, &options));
        assert(options.profile == expected[profile][0] && options.stride == expected[profile][1]);
        assert(options.tolerance_min == expected[profile][2] && options.tolerance_max == expected[profile][3]);
    }
    assert(ms912x_android_profile_defaults(4, &options) < 0);
    assert(ms912x_android_profile_defaults(0, &options) < 0);
    /* Independently exercise U, Y0, V and Y1 bit buckets and selective commits. */
    for (unsigned int bucket = 1; bucket <= 127; bucket = bucket * 2 + 1)
        for (unsigned int lane = 0; lane < 4; lane++) {
            uint32_t pixels[16] = {0}; uint8_t packet[128];
            struct ms912x_frame_damage *damage = ms912x_frame_damage_create(16,2);
            assert(damage && !ms912x_frame_damage_set_scan_options(damage,1,bucket));
            assert(ms912x_frame_damage_prepare(damage,packet,sizeof(packet),(uint8_t *)pixels,1)>0);
            assert(!ms912x_frame_damage_finish(damage,1,true));
            assert(ms912x_frame_damage_prepare_replay(damage,packet,sizeof(packet),2)>0);
            assert(!ms912x_frame_damage_finish(damage,2,true));
            pixels[0] = 1U << (lane * 8);
            assert(!ms912x_frame_damage_prepare(damage,packet,sizeof(packet),(uint8_t *)pixels,3));
            assert(!ms912x_frame_damage_set_scan_options(damage,1,0));
            assert(ms912x_frame_damage_prepare(damage,packet,sizeof(packet),(uint8_t *)pixels,4)>0);
            assert(!ms912x_frame_damage_finish(damage,4,true));
            assert(ms912x_frame_damage_prepare_replay(damage,packet,sizeof(packet),5)>0);
            assert(!ms912x_frame_damage_finish(damage,5,true));
            assert(!ms912x_frame_damage_set_scan_options(damage,1,bucket));
            pixels[0] = (bucket + 1U) << (lane * 8);
            assert(ms912x_frame_damage_prepare(damage,packet,sizeof(packet),(uint8_t *)pixels,6)>0);
            assert(!ms912x_frame_damage_finish(damage,6,true));
            ms912x_frame_damage_destroy(damage);
        }
    for (unsigned int stride = 1; stride <= 16; stride++) {
        uint32_t pixels[32 * 32] = {0}; uint8_t packet[32 * (128 + 16)];
        struct ms912x_frame_damage *damage = ms912x_frame_damage_create(64,32);
        assert(damage && !ms912x_frame_damage_set_scan_options(damage,stride,0));
        assert(ms912x_frame_damage_prepare(damage,packet,sizeof(packet),(uint8_t *)pixels,1)>0);
        assert(!ms912x_frame_damage_finish(damage,1,true));
        assert(ms912x_frame_damage_prepare_replay(damage,packet,sizeof(packet),2)>0);
        assert(!ms912x_frame_damage_finish(damage,2,true));
        pixels[(stride - 1) * 32] = 0x10203040;
        for (unsigned int phase = 0; phase < stride; phase++) {
            int length = ms912x_frame_damage_prepare(damage,packet,sizeof(packet),(uint8_t *)pixels,phase+3);
            if (phase + 1 < stride) assert(!length);
            else {
                assert(length > 0);
                unsigned int y = ((packet[3] & 15) << 8) | packet[4];
                unsigned int h = ((packet[6] & 15) << 8) | packet[7];
                assert(y <= stride - 1 && y + h > stride - 1 && y + h <= 32);
                assert(!ms912x_frame_damage_finish(damage,phase+3,true));
            }
        }
        ms912x_frame_damage_destroy(damage);
    }
}

static void live_options(void)
{
    uint8_t rgba[64*4*4] = {0},packet[4*(128+16)],pinned[sizeof(packet)];
    struct ms912x_android_frame_info info;
    struct ms912x_android_frame_pipeline *pipeline=ms912x_android_frame_pipeline_create(64,4,1);
    assert(pipeline);
    assert(ms912x_android_frame_pipeline_set_options(pipeline,4,1,0,1)<0);
    assert(ms912x_android_frame_pipeline_set_options(pipeline,3,17,0,1)<0);
    assert(ms912x_android_frame_pipeline_set_options(pipeline,3,1,128,1)<0);
    assert(ms912x_android_frame_pipeline_set_tolerance_range(pipeline,3,1,15,3,1)<0);
    assert(ms912x_android_frame_pipeline_set_tolerance_range(pipeline,3,1,-1,15,1)<0);
#define LIVE_OFFER() assert(!ms912x_android_frame_pipeline_offer_rgba(pipeline,rgba,sizeof(rgba),0,256,4,0,0,64,4,1))
#define LIVE_ENCODE() ms912x_android_frame_pipeline_encode_latest(pipeline,packet,sizeof(packet),&info)
#define LIVE_FINISH() assert(!ms912x_android_frame_pipeline_complete(pipeline,info.sequence,true))
    LIVE_OFFER();assert(LIVE_ENCODE()==528);LIVE_FINISH();
    assert(LIVE_ENCODE()==528);LIVE_FINISH();
    assert(!ms912x_android_frame_pipeline_set_options(pipeline,3,1,31,1));
    assert(LIVE_ENCODE()==528);LIVE_FINISH();assert(LIVE_ENCODE()==528);LIVE_FINISH();
    for (unsigned int i=0;i<64*4;i++)rgba[i*4]=rgba[i*4+1]=rgba[i*4+2]=4;
    LIVE_OFFER();assert(!LIVE_ENCODE());
    /* No new producer buffer: switching back to exact must recover suppressed pixels. */
    assert(!ms912x_android_frame_pipeline_set_options(pipeline,3,1,0,1));
    assert(LIVE_ENCODE()==528 && packet[9]>16);
    memcpy(pinned,packet,sizeof(packet));
    assert(!ms912x_android_frame_pipeline_set_options(pipeline,1,0,-1,1));
    assert(LIVE_ENCODE()==-EBUSY && !memcmp(packet,pinned,sizeof(packet)));
    LIVE_FINISH();assert(LIVE_ENCODE()==528);LIVE_FINISH();
    assert(LIVE_ENCODE()==528);LIVE_FINISH();
    assert(!ms912x_android_frame_pipeline_set_options(pipeline,3,1,0,1));
    assert(LIVE_ENCODE()==528);LIVE_FINISH();assert(LIVE_ENCODE()==528);LIVE_FINISH();
    rgba[0]=255;LIVE_OFFER();
    uint64_t wait=0;
    assert(!ms912x_android_frame_pipeline_encode_paced(pipeline,packet,sizeof(packet),&info,1000000000,60,&wait)&&wait>0);
    assert(!ms912x_android_frame_pipeline_set_options(pipeline,2,1,0,1));
    assert(ms912x_android_frame_pipeline_encode_paced(pipeline,packet,sizeof(packet),&info,1000000001,60,&wait)==528&&wait==0);
    LIVE_FINISH();
    ms912x_android_frame_pipeline_destroy(pipeline);
#undef LIVE_OFFER
#undef LIVE_ENCODE
#undef LIVE_FINISH
}

static void adaptive_tolerance_sample(int minimum, int maximum, bool slow,
                                     unsigned int gray, unsigned int luma,
                                     unsigned int changed_bit)
{
    uint8_t rgba[64 * 4 * 4] = {0}, packet[576];
    struct ms912x_android_frame_info info;
    struct ms912x_android_frame_pipeline *pipeline = ms912x_android_frame_pipeline_create(64,4,1);
    assert(pipeline && !ms912x_android_frame_pipeline_set_tolerance_range(pipeline,2,1,minimum,maximum,1));
    /* Gray 94 has Y=96; flipping any one bit stays inside limited-range luma. */
    for (unsigned int i = 0; i < 64 * 4; ++i)
        rgba[i*4] = rgba[i*4+1] = rgba[i*4+2] = 94;
    assert(!ms912x_android_frame_pipeline_offer_rgba(pipeline,rgba,sizeof(rgba),0,256,4,0,0,64,4,1));
    for (unsigned int i = 0; i < 2; ++i) {
        assert(ms912x_android_frame_pipeline_encode_latest(pipeline,packet,sizeof(packet),&info)==528);
        assert(!ms912x_android_frame_pipeline_complete(pipeline,info.sequence,true));
    }
    /* The shared USB estimator is a cumulative mean seeded at 30 MB/s.
     * Supply enough slow completions to reach the high-pressure threshold. */
    for (unsigned int i = 0; i < 10000; ++i)
        ms912x_android_frame_pipeline_record_usb(pipeline,slow?1:MS912X_ANDROID_MAX_FRAME_BYTES,slow?1000000000:1);
    for (unsigned int i = 0; i < 64 * 4; ++i)
        rgba[i*4] = rgba[i*4+1] = rgba[i*4+2] = gray;
    assert(!ms912x_android_frame_pipeline_offer_rgba(pipeline,rgba,sizeof(rgba),0,256,4,0,0,64,4,1));
    int length = ms912x_android_frame_pipeline_encode_latest(pipeline,packet,sizeof(packet),&info);
    assert(length == ((unsigned int)(slow ? maximum : minimum) < changed_bit ? 528 : 0));
    if (length) {
        const uint8_t header[] = {0xff,0,0,0,0,4,0,4};
        const uint8_t trailer[] = {0xff,0xc0,0,0,0,0,0,0};
        const uint8_t expected[] = {128,luma,128,luma};
        assert(!memcmp(packet,header,sizeof(header)));
        for (unsigned int i = 8; i < 520; i += 4)
            assert(!memcmp(packet+i,expected,sizeof(expected)));
        assert(!memcmp(packet+520,trailer,sizeof(trailer)));
        assert(!ms912x_android_frame_pipeline_complete(pipeline,info.sequence,true));
    }
    ms912x_android_frame_pipeline_destroy(pipeline);
}

static void adaptive_tolerance(void)
{
    static const int buckets[] = {0, 1, 3, 7, 15, 31, 63, 127};
    /* Independent RGB-gray / expected-Y / lowest changed-bit fixtures. */
    static const unsigned int samples[][3] = {
        {95,97,1}, {96,98,2}, {99,100,4}, {103,104,8},
        {113,112,16}, {57,64,32}, {19,32,64}, {244,224,128}
    };
    for (unsigned int slow = 0; slow < 2; ++slow)
    for (unsigned int min = 0; min < sizeof(buckets) / sizeof(buckets[0]); ++min)
    for (unsigned int max = min; max < sizeof(buckets) / sizeof(buckets[0]); ++max)
    for (unsigned int sample = 0; sample < sizeof(samples) / sizeof(samples[0]); ++sample)
        adaptive_tolerance_sample(buckets[min],buckets[max],slow,
                                  samples[sample][0],samples[sample][1],samples[sample][2]);
    puts("Adaptive tolerance: 576 range/pressure/bit-boundary packet cases passed");
}

static void rejected_tolerance_ranges_preserve_submission(void)
{
    uint8_t rgba[64*4*4] = {0}, packet[576], pinned[sizeof(packet)];
    struct ms912x_android_frame_info info;
    struct ms912x_android_frame_pipeline *pipeline = ms912x_android_frame_pipeline_create(64,4,1);
    assert(pipeline && !ms912x_android_frame_pipeline_set_options(pipeline,3,1,31,1));
    assert(!ms912x_android_frame_pipeline_offer_rgba(pipeline,rgba,sizeof(rgba),0,256,4,0,0,64,4,1));
    memset(packet,0xa5,sizeof(packet));
    assert(ms912x_android_frame_pipeline_encode_latest(pipeline,packet,sizeof(packet),&info)==528);
    memcpy(pinned,packet,sizeof(packet));
    for (int min = -2; min <= 128; ++min)
    for (int max = -2; max <= 128; ++max) {
        const int buckets[] = {0,1,3,7,15,31,63,127};
        bool min_valid = false, max_valid = false;
        for (unsigned int i = 0; i < sizeof(buckets)/sizeof(buckets[0]); ++i) {
            min_valid |= min == buckets[i];
            max_valid |= max == buckets[i];
        }
        if ((min == -1 && max == -1) || (min_valid && max_valid && min <= max)) continue;
        assert(ms912x_android_frame_pipeline_set_tolerance_range(pipeline,3,1,min,max,1)==-EINVAL);
    }
    const unsigned int invalid_profiles[] = {0,4,UINT_MAX};
    for (unsigned int i = 0; i < sizeof(invalid_profiles)/sizeof(invalid_profiles[0]); ++i)
        assert(ms912x_android_frame_pipeline_set_tolerance_range(pipeline,invalid_profiles[i],1,0,31,1)==-EINVAL);
    assert(ms912x_android_frame_pipeline_set_tolerance_range(pipeline,3,17,0,31,1)==-EINVAL);
    assert(ms912x_android_frame_pipeline_set_tolerance_range(pipeline,3,UINT_MAX,0,31,1)==-EINVAL);
    assert(ms912x_android_frame_pipeline_set_tolerance_range(pipeline,3,1,0,31,-1)==-EINVAL);
    assert(ms912x_android_frame_pipeline_set_tolerance_range(pipeline,3,1,0,31,2)==-EINVAL);
    assert(ms912x_android_frame_pipeline_set_tolerance_range(NULL,3,1,0,31,1)==-EINVAL);
    assert(ms912x_android_frame_pipeline_encode_latest(pipeline,packet,sizeof(packet),&info)==-EBUSY);
    assert(!memcmp(packet,pinned,sizeof(packet)));
    assert(!ms912x_android_frame_pipeline_complete(pipeline,info.sequence,true));
    assert(ms912x_android_frame_pipeline_encode_latest(pipeline,packet,sizeof(packet),&info)==528);
    assert(!ms912x_android_frame_pipeline_complete(pipeline,info.sequence,true));
    assert(!ms912x_android_frame_pipeline_encode_latest(pipeline,packet,sizeof(packet),&info));
    /* Rejection must preserve the previous fixed-31 policy, not just the packet. */
    for (unsigned int i = 0; i < 64*4; ++i) rgba[i*4]=rgba[i*4+1]=rgba[i*4+2]=8;
    assert(!ms912x_android_frame_pipeline_offer_rgba(pipeline,rgba,sizeof(rgba),0,256,4,0,0,64,4,1));
    assert(!ms912x_android_frame_pipeline_encode_latest(pipeline,packet,sizeof(packet),&info));
    ms912x_android_frame_pipeline_destroy(pipeline);
}

static void scan_toggle(void)
{
    struct ms912x_android_frame_pipeline *pipeline=ms912x_android_frame_pipeline_create(64,4,1);
    struct ms912x_android_frame_info info;
    uint8_t rgba[64*4*4]={0},packet[576],pinned[528];
    assert(pipeline);
    assert(ms912x_android_frame_pipeline_set_options(pipeline,0,1,0,1)<0);
    assert(ms912x_android_frame_pipeline_set_options(pipeline,3,1,0,2)<0);
#define SCAN_OFFER(gen) assert(!ms912x_android_frame_pipeline_offer_rgba(pipeline,rgba,sizeof(rgba),0,256,4,0,0,64,4,gen))
#define SCAN_ENCODE() ms912x_android_frame_pipeline_encode_latest(pipeline,packet,sizeof(packet),&info)
#define SCAN_FINISH() assert(!ms912x_android_frame_pipeline_complete(pipeline,info.sequence,true))
    SCAN_OFFER(1);assert(SCAN_ENCODE()==528);SCAN_FINISH();
    assert(SCAN_ENCODE()==528);SCAN_FINISH();
    SCAN_OFFER(1);assert(!SCAN_ENCODE());
    /* Off resends the retained static snapshot, bypassing stride/tolerance. */
    assert(!ms912x_android_frame_pipeline_set_options(pipeline,3,16,31,0));
    assert(SCAN_ENCODE()==528);SCAN_FINISH();
    assert(SCAN_ENCODE()==528);SCAN_FINISH();assert(!SCAN_ENCODE());
    for(unsigned int i=0;i<3;i++){
        SCAN_OFFER(1);assert(SCAN_ENCODE()==528);SCAN_FINISH();
    }
    SCAN_OFFER(1);assert(SCAN_ENCODE()==528);memcpy(pinned,packet,sizeof(pinned));
    /* Reenable while USB owns the packet, then publish a newer image. */
    assert(!ms912x_android_frame_pipeline_set_options(pipeline,3,1,0,1));
    rgba[0]=rgba[1]=rgba[2]=128;SCAN_OFFER(1);
    assert(SCAN_ENCODE()==-EBUSY&&!memcmp(packet,pinned,sizeof(pinned)));
    SCAN_FINISH();assert(SCAN_ENCODE()==528&&packet[9]>16);SCAN_FINISH();
    assert(SCAN_ENCODE()==528);SCAN_FINISH();
    SCAN_OFFER(1);assert(!SCAN_ENCODE());
    assert(!ms912x_android_frame_pipeline_set_options(pipeline,3,1,0,0));
    assert(SCAN_ENCODE()==528);SCAN_FINISH();assert(SCAN_ENCODE()==528);SCAN_FINISH();
    assert(!ms912x_android_frame_pipeline_reconfigure(pipeline,64,4,2));
    SCAN_OFFER(2);assert(SCAN_ENCODE()==528);SCAN_FINISH();
    assert(SCAN_ENCODE()==528);SCAN_FINISH();
    SCAN_OFFER(2);assert(SCAN_ENCODE()==528);SCAN_FINISH();
    assert(SCAN_ENCODE()==528);SCAN_FINISH();assert(!SCAN_ENCODE());
    ms912x_android_frame_pipeline_destroy(pipeline);
#undef SCAN_OFFER
#undef SCAN_ENCODE
#undef SCAN_FINISH
}

int main(void)
{
    for (unsigned int pattern = 0; pattern < 3; ++pattern) fragmented_frame(pattern);
    bounded_storage();
    paced_submission();
    differential_scan_backends();
    backend_batches();
    backend_transactions();
    scan_options();
    live_options();
    adaptive_tolerance();
    rejected_tolerance_ranges_preserve_submission();
    scan_toggle();
    puts("Shared Linux scan geometry: vertical unions, horizontal blocks, pre-limit compaction and replay passed");
    return 0;
}
