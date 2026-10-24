/* SPDX-License-Identifier: GPL-2.0-only */
#include "ms912x_frame_damage.h"
#include "frame_pipeline.h"
#include "ms912x_color_core.h"
#include "ms912x_pacing_core.h"
#include "ms912x_scan_core.h"
#include "ms912x_scan_backend.h"
#include "../../ms912x_scan_rects.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    assert(!ms912x_core_scan_words(baseline, 64, newest, 64, 128, 1, 64, 1,
                                  UINT32_MAX, rects, 2, &count, index));
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
        for (unsigned int tolerance = 0; tolerance <= 31; tolerance = tolerance * 2 + 1) {
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
    for (int backend = 0; backend < MS912X_SCAN_BACKEND_COUNT; backend++) {
        if (!ms912x_core_scan_backend_supported(backend)) continue;
        struct ms912x_frame_damage *damage = ms912x_frame_damage_create(130, 3);
        assert(damage);
        memset(pixels, 0, sizeof(pixels));
        assert(!ms912x_frame_damage_set_scan_backend(damage, backend));
        assert(ms912x_frame_damage_prepare(damage, packet, capacity, (uint8_t *)pixels, 1) > 0);
        assert(ms912x_frame_damage_set_scan_backend(damage, backend) == -EBUSY);
        assert(!ms912x_frame_damage_finish(damage, 1, true));
        assert(ms912x_frame_damage_prepare_replay(damage, packet, capacity, 2) > 0);
        assert(!ms912x_frame_damage_finish(damage, 2, true));
        pixels[65 + 17] = 0x01000000U; /* Y1-only change: exact MEMCMP must retain it. */
        unsigned int x = backend == MS912X_SCAN_MEMCMP ? 0 : 32;
        unsigned int w = backend == MS912X_SCAN_MEMCMP ? 64 : 16;
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
        assert(ms912x_frame_damage_set_scan_backend(damage, MS912X_SCAN_BACKEND_COUNT) == -EINVAL);

        /* Explicit scan geometry uses MEMCMP's exact mask even when passed zero. */
        uint32_t baseline[65 * 3] = {0};
        struct drm_rect rect;
        size_t count;
        assert(!ms912x_core_scan_words_backend(baseline, 195, pixels, 195,
                    130, 3, 65, 1, 0, &rect, 1, &count, index, backend));
        assert(count == (backend == MS912X_SCAN_MEMCMP ? 1U : 0U));
        if (count) assert(rect.x1 == 0 && rect.x2 == 64 && rect.y1 == 1 && rect.y2 == 2);
        ms912x_frame_damage_destroy(damage);
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
    ++now;
    assert(ENCODE() == 0 && now + wait == deadline);
    {
        unsigned int slot;
        const uint8_t *pixels;
        struct ms912x_android_frame_info held;
        // Model the empty-mailbox interval while a producer fills a replacement.
        assert(!ms912x_android_frame_pipeline_acquire(pipeline, &slot, &held, &pixels));
        assert(ENCODE() == 0 && wait == 0);
        ms912x_android_frame_pipeline_release(pipeline, slot);
    }
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

int main(void)
{
    for (unsigned int pattern = 0; pattern < 3; ++pattern) fragmented_frame(pattern);
    bounded_storage();
    paced_submission();
    differential_scan_backends();
    backend_batches();
    backend_transactions();
    puts("Shared Linux scan geometry: vertical unions, horizontal blocks, pre-limit compaction and replay passed");
    return 0;
}
