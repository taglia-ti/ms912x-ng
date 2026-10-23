/* SPDX-License-Identifier: GPL-2.0-only */
#include "frame_pipeline.h"
#include "ms912x_frame_damage.h"
#include "ms912x_color_core.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

static void offer(struct ms912x_android_frame_pipeline *pipeline, const uint8_t rgba[32])
{
	assert(ms912x_android_frame_pipeline_offer_rgba(
		pipeline, rgba, 32, 0, 16, 4, 0, 0, 4, 2, 1) == 0);
}

static void test_acknowledgement_and_repair(void)
{
	struct ms912x_android_frame_pipeline *pipeline = ms912x_android_frame_pipeline_create(4, 2, 1);
	struct ms912x_android_frame_info info;
	uint8_t rgba[32] = {0}, packet[64];
	uint64_t first;

	assert(pipeline);
	assert(ms912x_android_frame_pipeline_packet_capacity(pipeline) == 48);
	offer(pipeline, rgba);
	assert(ms912x_android_frame_pipeline_encode_latest(pipeline, packet, sizeof(packet), &info) == 32);
	first = info.sequence;
	rgba[8] = 255;
	offer(pipeline, rgba);
	assert(ms912x_android_frame_pipeline_encode_latest(pipeline, packet, sizeof(packet), &info) == -EBUSY);
	assert(ms912x_android_frame_pipeline_reconfigure(pipeline, 4, 2, 2) == -2);
	assert(ms912x_android_frame_pipeline_complete(pipeline, first + 100, 1) == -ESTALE);
	assert(ms912x_android_frame_pipeline_complete(pipeline, first, 0) == 0);
	assert(ms912x_android_frame_pipeline_complete(pipeline, first, 1) == -ESTALE);
	assert(ms912x_android_frame_pipeline_encode_latest(pipeline, packet, sizeof(packet), &info) == 32);
	assert(ms912x_android_frame_pipeline_complete(pipeline, info.sequence, 1) == 0);
	assert(ms912x_android_frame_pipeline_encode_latest(pipeline, packet, sizeof(packet), &info) == 32);
	assert(ms912x_android_frame_pipeline_reconfigure(pipeline, 4, 2, 2) == -2);
	assert(ms912x_android_frame_pipeline_encode_latest(pipeline, packet, sizeof(packet), &info) == -EBUSY);
	assert(ms912x_android_frame_pipeline_complete(pipeline, info.sequence, 1) == 0);
	offer(pipeline, rgba);
	assert(ms912x_android_frame_pipeline_encode_latest(pipeline, packet, sizeof(packet), &info) == 0);
	rgba[9] = 255;
	offer(pipeline, rgba);
	assert(ms912x_android_frame_pipeline_encode_latest(pipeline, packet, sizeof(packet), &info) == 20);
	ms912x_android_frame_pipeline_invalidate_baseline(pipeline);
	assert(ms912x_android_frame_pipeline_complete(pipeline, info.sequence, 1) == 0);
	offer(pipeline, rgba);
	assert(ms912x_android_frame_pipeline_encode_latest(pipeline, packet, sizeof(packet), &info) == 32);
	assert(ms912x_android_frame_pipeline_complete(pipeline, info.sequence, 1) == 0);
	ms912x_android_frame_pipeline_invalidate_baseline(pipeline);
	offer(pipeline, rgba);
	assert(ms912x_android_frame_pipeline_encode_latest(pipeline, packet, 31, &info) == -ENOSPC);
	offer(pipeline, rgba);
	assert(ms912x_android_frame_pipeline_encode_latest(pipeline, packet, sizeof(packet), &info) == 32);
	assert(ms912x_android_frame_pipeline_complete(pipeline, info.sequence, 1) == 0);
	assert(ms912x_android_frame_pipeline_reconfigure(pipeline, 2, 2, 2) == 0);
	ms912x_android_frame_pipeline_destroy(pipeline);
}

/* Decode independently: all rectangles must use the latest pixels and one final FFC0. */
static uint32_t coverage(const uint8_t *packet, int length, const uint32_t source[16])
{
	size_t at = 0;
	uint32_t mask = 0;
	while (at < (size_t)length) {
		assert((size_t)length - at >= 16 && packet[at] == 255 && packet[at + 1] == 0);
		unsigned int position = (unsigned int)packet[at + 2] << 16 |
			(unsigned int)packet[at + 3] << 8 | packet[at + 4];
		unsigned int dimensions = (unsigned int)packet[at + 5] << 16 |
			(unsigned int)packet[at + 6] << 8 | packet[at + 7];
		unsigned int x = position >> 12, y = position & 4095;
		unsigned int w = dimensions >> 12, h = dimensions & 4095;
		assert(w && h && !(x & 1) && !(w & 1) && x + w <= 8 && y + h <= 4);
		at += 8;
		assert((size_t)length - at >= (size_t)w * h * 2 + 8);
		for (unsigned int row = 0; row < h; ++row) {
			size_t first = (y + row) * 4 + x / 2;
			assert(memcmp(packet + at, source + first, w * 2) == 0);
			for (unsigned int pair = 0; pair < w / 2; ++pair) mask |= 1U << (first + pair);
			at += w * 2;
		}
		assert(packet[at] == 255 && packet[at + 1] == (at + 8 == (size_t)length ? 0xc0 : 0x80));
		for (size_t i = 2; i < 8; ++i) assert(packet[at + i] == 0);
		at += 8;
	}
	assert(at == (size_t)length);
	return mask;
}

static void test_previous_damage_and_idle_replay(void)
{
	struct ms912x_frame_damage *damage = ms912x_frame_damage_create(8, 4);
	uint32_t pixels[16] = {0};
	uint8_t packet[128];
	uint64_t sequence = 1;
	int length;
	assert(damage);
	assert(ms912x_frame_damage_prepare_replay(damage, packet, sizeof(packet), sequence) == 0);
	assert(ms912x_frame_damage_prepare(damage, packet, sizeof(packet), (uint8_t *)pixels, sequence) == 80);
	assert(ms912x_frame_damage_finish(damage, sequence++, true) == 0);
	assert(ms912x_frame_damage_prepare_replay(damage, packet, 79, sequence) == -ENOSPC);
	assert(ms912x_frame_damage_prepare_replay(damage, packet, sizeof(packet), sequence) == 80);
	assert(ms912x_frame_damage_finish(damage, sequence++, true) == 0);
	assert(ms912x_frame_damage_prepare_replay(damage, packet, sizeof(packet), sequence) == 0);
	for (unsigned int i = 0; i < 4; ++i) {
		pixels[i * 5] = 0x10203040U + i;
		length = ms912x_frame_damage_prepare(damage, packet, sizeof(packet), (uint8_t *)pixels, sequence);
		assert(length == (i ? 40 : 20));
		assert(coverage(packet, length, pixels) == ((1U << (i * 5)) | (i ? 1U << ((i - 1) * 5) : 0)));
		assert(ms912x_frame_damage_finish(damage, sequence++, true) == 0);
	}
	assert(ms912x_frame_damage_prepare_replay(damage, packet, sizeof(packet), sequence) == 20);
	assert(coverage(packet, 20, pixels) == 1U << 15);
	assert(ms912x_frame_damage_finish(damage, sequence++, true) == 0);
	assert(ms912x_frame_damage_prepare_replay(damage, packet, sizeof(packet), sequence) == 0);
	/* Overlap reads new pixels, rather than replaying saved packet bytes. */
	pixels[0] = 7;
	assert(ms912x_frame_damage_prepare(damage, packet, sizeof(packet), (uint8_t *)pixels, sequence) == 20);
	assert(ms912x_frame_damage_finish(damage, sequence++, true) == 0);
	pixels[0] = 9; pixels[15] = 11;
	length = ms912x_frame_damage_prepare(damage, packet, sizeof(packet), (uint8_t *)pixels, sequence);
	assert(length > 0 && coverage(packet, length, pixels) == ((1U << 0) | (1U << 15)));
	assert(ms912x_frame_damage_finish(damage, sequence++, true) == 0);
	assert(ms912x_frame_damage_prepare_replay(damage, packet, sizeof(packet), sequence) > 0);
	assert(ms912x_frame_damage_finish(damage, sequence++, false) == 0);
	assert(ms912x_frame_damage_prepare(damage, packet, sizeof(packet), (uint8_t *)pixels, sequence) == 80);
	assert(ms912x_frame_damage_finish(damage, sequence++, true) == 0);
	assert(ms912x_frame_damage_prepare_replay(damage, packet, sizeof(packet), sequence) == 80);
	ms912x_frame_damage_invalidate(damage);
	assert(ms912x_frame_damage_finish(damage, sequence++, true) == 0);
	assert(ms912x_frame_damage_prepare_replay(damage, packet, sizeof(packet), sequence) == 0);
	assert(ms912x_frame_damage_prepare(damage, packet, sizeof(packet), (uint8_t *)pixels, sequence) == 80);
	assert(ms912x_frame_damage_finish(damage, sequence++, true) == 0);
	assert(ms912x_frame_damage_prepare_replay(damage, packet, sizeof(packet), sequence) == 80);
	assert(ms912x_frame_damage_finish(damage, sequence++, true) == 0);
	/* Eight disjoint rectangles exceed the fixed packet budget; full fallback preserves D. */
	for (unsigned int row = 0; row < 4; ++row) pixels[row * 4 + (row & 1)] += 100;
	assert(ms912x_frame_damage_prepare(damage, packet, sizeof(packet), (uint8_t *)pixels, sequence) == 80);
	assert(ms912x_frame_damage_finish(damage, sequence++, true) == 0);
	for (unsigned int row = 0; row < 4; ++row) pixels[row * 4 + 2 + (row & 1)] += 100;
	assert(ms912x_frame_damage_prepare(damage, packet, sizeof(packet), (uint8_t *)pixels, sequence) == 80);
	assert(coverage(packet, 80, pixels) == 65535);
	assert(ms912x_frame_damage_finish(damage, sequence++, true) == 0);
	assert(ms912x_frame_damage_prepare_replay(damage, packet, sizeof(packet), sequence) == 80);
	assert(coverage(packet, 80, pixels) == ((1U << 2) | (1U << 7) | (1U << 10) | (1U << 15)));
	assert(ms912x_frame_damage_finish(damage, sequence++, true) == 0);
	assert(ms912x_frame_damage_prepare_replay(damage, packet, sizeof(packet), sequence) == 0);
	ms912x_frame_damage_destroy(damage);
}

struct producer {
	struct ms912x_android_frame_pipeline *pipeline;
	atomic_uint *done;
	uint8_t red;
};

static void *produce(void *argument)
{
	struct producer *producer = argument;
	uint8_t rgba[128 * 8 * 4];
	for (size_t i = 0; i < sizeof(rgba); i += 4) {
		rgba[i] = producer->red; rgba[i + 1] = 72;
		rgba[i + 2] = 193; rgba[i + 3] = 255;
	}
	for (unsigned int n = 0; n < 3000; ++n) {
		int result = ms912x_android_frame_pipeline_offer_rgba(
			producer->pipeline, rgba, sizeof(rgba), 0, 128 * 4, 4, 0, 0, 128, 8, 1);
		assert(result == 0 || result == 1);
	}
	atomic_fetch_add(producer->done, 1);
	return NULL;
}

static void check_uniform_packet(const uint8_t *packet, int length)
{
	size_t payload = length == 2064 ? 2048U : 256U;
	size_t groups = length == 2064 ? 1U : 8U;
	uint32_t first;
	assert(length == 2064 || length == 2176);
	first = (uint32_t)packet[8] | ((uint32_t)packet[9] << 8) |
		((uint32_t)packet[10] << 16) | ((uint32_t)packet[11] << 24);
	assert(first == ms912x_core_rgb_pair_to_uyvy(0, 72, 193, 0, 72, 193) ||
	       first == ms912x_core_rgb_pair_to_uyvy(255, 72, 193, 255, 72, 193));
	for (size_t i = 0; i < groups; ++i)
		for (size_t j = 0; j < payload; j += 4)
			assert(memcmp(packet + i * (payload + 16U) + 8U + j, packet + 8, 4) == 0);
}

static void test_concurrent_submission(void)
{
	struct ms912x_android_frame_pipeline *pipeline = ms912x_android_frame_pipeline_create(128, 8, 1);
	atomic_uint done = 0;
	struct producer args[2] = {{pipeline, &done, 0}, {pipeline, &done, 255}};
	pthread_t threads[2];
	uint8_t packet[8 * (256 + 16)];
	struct ms912x_android_frame_info info;
	uint64_t last = 0;
	unsigned int batches = 0;
	int length;

	assert(pipeline);
	assert(pthread_create(&threads[0], NULL, produce, &args[0]) == 0);
	assert(pthread_create(&threads[1], NULL, produce, &args[1]) == 0);
	do {
		length = ms912x_android_frame_pipeline_encode_latest(pipeline, packet, sizeof(packet), &info);
		assert(length >= 0);
		if (length > 0) {
			assert(info.sequence > last);
			last = info.sequence;
			check_uniform_packet(packet, length);
			assert(ms912x_android_frame_pipeline_complete(pipeline, info.sequence, batches % 3 != 0) == 0);
			++batches;
		}
	} while (atomic_load(&done) < 2 || length > 0);
	assert(pthread_join(threads[0], NULL) == 0);
	assert(pthread_join(threads[1], NULL) == 0);
	assert(batches > 0);
	ms912x_android_frame_pipeline_destroy(pipeline);
}

int main(void)
{
	test_acknowledgement_and_repair();
	test_previous_damage_and_idle_replay();
	test_concurrent_submission();
	puts("Frame submission acknowledgement, repair, and concurrency tests passed");
	return 0;
}
