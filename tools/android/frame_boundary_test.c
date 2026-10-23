/* SPDX-License-Identifier: GPL-2.0-only */
#include "frame_pipeline.h"
#include "ms912x_frame_damage.h"
#include "ms912x_edid_core.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

static void damage_boundaries(void)
{
    uint32_t pixels[4] = {0};
    uint8_t packet[64];
    assert(!ms912x_frame_damage_create(0, 2));
    assert(!ms912x_frame_damage_create(3, 2));
    assert(!ms912x_frame_damage_create(4096, 2));
    assert(!ms912x_frame_damage_create(4, 4096));
    assert(!ms912x_frame_damage_packet_capacity(0, 2));
    assert(!ms912x_frame_damage_packet_capacity(4, 0));
    assert(!ms912x_frame_damage_packet_capacity(3, 2));
    assert(!ms912x_frame_damage_packet_capacity(4096, 2));
    ms912x_frame_damage_destroy(NULL);
    ms912x_frame_damage_invalidate(NULL);
    assert(ms912x_frame_damage_finish(NULL, 1, true) == -ESTALE);
    assert(ms912x_frame_damage_prepare(NULL, packet, sizeof(packet), (uint8_t *)pixels, 1) == -EINVAL);
    struct ms912x_frame_damage *damage = ms912x_frame_damage_create(4, 2);
    assert(damage);
    assert(ms912x_frame_damage_prepare(damage, NULL, sizeof(packet), (uint8_t *)pixels, 1) == -EINVAL);
    assert(ms912x_frame_damage_prepare(damage, packet, sizeof(packet), NULL, 1) == -EINVAL);
    assert(ms912x_frame_damage_prepare(damage, packet, sizeof(packet), (uint8_t *)pixels, 0) == -EINVAL);
    assert(ms912x_frame_damage_prepare(damage, packet, sizeof(packet), (uint8_t *)pixels + 1, 1) == -EINVAL);
    assert(ms912x_frame_damage_prepare(damage, packet, 31, (uint8_t *)pixels, 1) == -ENOSPC);
    assert(ms912x_frame_damage_prepare(damage, packet, sizeof(packet), (uint8_t *)pixels, 1) == 32);
    assert(ms912x_frame_damage_prepare(damage, packet, sizeof(packet), (uint8_t *)pixels, 2) == -EBUSY);
    assert(ms912x_frame_damage_finish(damage, 2, true) == -ESTALE);
    assert(ms912x_frame_damage_finish(damage, 1, true) == 0);
    assert(ms912x_frame_damage_prepare_replay(damage, packet, sizeof(packet), 2) == 32);
    assert(ms912x_frame_damage_finish(damage, 2, true) == 0);
    assert(ms912x_frame_damage_prepare(damage, packet, sizeof(packet), (uint8_t *)pixels, 2) == 0);
    ms912x_frame_damage_destroy(damage);
}

static void edid_header_quirk(void)
{
    unsigned char block[128] = {0};
    for (size_t i = 1; i < 7; ++i) block[i] = 255;
    unsigned int sum = 0;
    for (size_t i = 0; i < 127; ++i) sum += block[i];
    block[127] = (unsigned char)-sum;
    assert(!ms912x_core_edid_repair_header_byte1(NULL, 128));
    assert(!ms912x_core_edid_repair_header_byte1(block, 127));
    assert(!ms912x_core_edid_repair_header_byte1(block, 128));
    block[1] = 0;
    block[20] = 1;
    assert(!ms912x_core_edid_repair_header_byte1(block, 128) && block[1] == 0);
    block[20] = 0;
    assert(ms912x_core_edid_repair_header_byte1(block, 128) && block[1] == 255);
    block[1] = 0; block[7] = 1;
    assert(!ms912x_core_edid_repair_header_byte1(block, 128));
}

static void mailbox_boundaries(void)
{
    uint8_t rgba[32] = {0}, packet[64];
    struct ms912x_android_frame_info info;
    const uint8_t *pixels;
    unsigned int slots[3];
    assert(!ms912x_android_frame_pipeline_create(0, 2, 1));
    assert(!ms912x_android_frame_pipeline_create(4, 0, 1));
    assert(!ms912x_android_frame_pipeline_create(4, 2, 0));
    assert(!ms912x_android_frame_pipeline_create(1922, 2, 1));
    assert(!ms912x_android_frame_pipeline_create(4, 1202, 1));
    ms912x_android_frame_pipeline_destroy(NULL);
    ms912x_android_frame_pipeline_release(NULL, 0);
    ms912x_android_frame_pipeline_invalidate_baseline(NULL);
    assert(ms912x_android_frame_pipeline_complete(NULL, 1, 1) == -EINVAL);
    assert(ms912x_android_frame_pipeline_reconfigure(NULL, 4, 2, 1) < 0);
    assert(ms912x_android_frame_pipeline_encode_latest(NULL, packet, sizeof(packet), &info) < 0);
    struct ms912x_android_frame_pipeline *pipeline = ms912x_android_frame_pipeline_create(4, 2, 1);
    assert(pipeline);
    assert(ms912x_android_frame_pipeline_reconfigure(pipeline, 3, 2, 2) < 0);
    assert(ms912x_android_frame_pipeline_acquire(NULL, slots, &info, &pixels) < 0);
    assert(ms912x_android_frame_pipeline_acquire(pipeline, NULL, &info, &pixels) < 0);
    assert(ms912x_android_frame_pipeline_acquire(pipeline, slots, NULL, &pixels) < 0);
    assert(ms912x_android_frame_pipeline_acquire(pipeline, slots, &info, NULL) < 0);
    assert(ms912x_android_frame_pipeline_encode_latest(pipeline, NULL, 64, &info) < 0);
    assert(ms912x_android_frame_pipeline_encode_latest(pipeline, packet, 64, NULL) < 0);
    for (unsigned int i = 0; i < 3; ++i) {
        assert(ms912x_android_frame_pipeline_offer_rgba(pipeline, rgba, 32, 0, 16, 4, 0, 0, 4, 2, 1) == 0);
        assert(ms912x_android_frame_pipeline_acquire(pipeline, slots + i, &info, &pixels) == 0);
    }
    assert(ms912x_android_frame_pipeline_offer_rgba(pipeline, rgba, 32, 0, 16, 4, 0, 0, 4, 2, 1) == 1);
    assert(ms912x_android_frame_pipeline_reconfigure(pipeline, 4, 2, 2) == -2);
    ms912x_android_frame_pipeline_release(pipeline, 99);
    for (unsigned int i = 0; i < 3; ++i) {
        ms912x_android_frame_pipeline_release(pipeline, slots[i]);
        ms912x_android_frame_pipeline_release(pipeline, slots[i]);
    }
    assert(ms912x_android_frame_pipeline_reconfigure(pipeline, 2, 2, 2) == 0);
    assert(ms912x_android_frame_pipeline_offer_rgba(pipeline, rgba, 32, 0, 16, 8, 0, 0, 2, 2, 2) == 0);
    assert(strcmp(ms912x_android_frame_pipeline_backend(pipeline), "scalar") == 0);
    assert(ms912x_android_frame_pipeline_encode_latest(pipeline, packet, sizeof(packet), &info) == 24);
    assert(ms912x_android_frame_pipeline_complete(pipeline, info.sequence, 1) == 0);
    ms912x_android_frame_pipeline_destroy(pipeline);
}

int main(void)
{
    damage_boundaries();
    edid_header_quirk();
    mailbox_boundaries();
    puts("Native damage and mailbox boundary fixtures passed");
    return 0;
}
