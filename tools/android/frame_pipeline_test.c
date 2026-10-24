#include "../../android/app/src/main/cpp/frame_pipeline.h"
#include "../../shared/ms912x_scan_core.h"
#include "../../ms912x_scan_rects.h"
#include "../../shared/ms912x_mode3_core.h"

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void offer(struct ms912x_android_frame_pipeline *pipeline,
		  const uint8_t *rgba, size_t size, uint32_t width,
		  uint32_t height, uint64_t generation)
{
	int result = ms912x_android_frame_pipeline_offer_rgba(
		pipeline, rgba, size, 0, width * 4U, 4, 0, 0,
		width, height, generation);
	assert(result == 0);
}

static void test_crop_stride_and_rgba_to_uyvy(void)
{
	struct ms912x_android_frame_pipeline *pipeline;
	struct ms912x_android_frame_info info;
	const uint8_t rgba[] = {
		1, 2, 3, 9, 10, 20, 30, 8, 40, 50, 60, 7, 0, 0, 0, 0,
		4, 5, 6, 9, 70, 80, 90, 8, 100, 110, 120, 7, 0, 0, 0, 0,
	};
	const uint8_t expected[] = { 133, 31, 122, 57, 133, 82, 122, 108 };
	const uint8_t *pixels;
	unsigned int slot;

	pipeline = ms912x_android_frame_pipeline_create(2, 2, 1);
	assert(pipeline != NULL);
	assert(ms912x_android_frame_pipeline_offer_rgba(
		pipeline, rgba, sizeof(rgba), 0, 16, 4, 1, 0, 2, 2, 1) == 0);
	assert(ms912x_android_frame_pipeline_acquire(
		pipeline, &slot, &info, &pixels) == 0);
	assert(info.width == 2 && info.height == 2);
	assert(info.stride_bytes == 4 && info.generation == 1 && info.sequence == 1);
	assert(info.pixel_format == MS912X_ANDROID_PIXEL_UYVY422);
	assert(memcmp(pixels, expected, sizeof(expected)) == 0);
	ms912x_android_frame_pipeline_release(pipeline, slot);
	ms912x_android_frame_pipeline_destroy(pipeline);
}

static void test_newest_pending_and_active_immutability(void)
{
	struct ms912x_android_frame_pipeline *pipeline;
	struct ms912x_android_frame_info info;
	const uint8_t initial[] = {
		255, 0, 0, 255, 0, 0, 255, 255,
	};
	const uint8_t first[] = {
		10, 20, 30, 255, 40, 50, 60, 255,
	};
	const uint8_t newest[] = {
		40, 50, 60, 255, 70, 80, 90, 255,
	};
	const uint8_t *pixels;
	unsigned int slot;

	pipeline = ms912x_android_frame_pipeline_create(2, 1, 1);
	assert(pipeline != NULL);
	offer(pipeline, initial, sizeof(initial), 2, 1, 1);
	assert(ms912x_android_frame_pipeline_acquire(
		pipeline, &slot, &info, &pixels) == 0);
	assert(pixels[0] == 164 && pixels[1] == 81 &&
	       pixels[2] == 174 && pixels[3] == 40);

	offer(pipeline, first, sizeof(first), 2, 1, 1);
	offer(pipeline, newest, sizeof(newest), 2, 1, 1);
	assert(pixels[0] == 164 && pixels[1] == 81 &&
	       pixels[2] == 174 && pixels[3] == 40);
	ms912x_android_frame_pipeline_release(pipeline, slot);
	assert(ms912x_android_frame_pipeline_acquire(
		pipeline, &slot, &info, &pixels) == 0);
	assert(info.sequence == 3);
	assert(pixels[0] == 133 && pixels[1] == 57 &&
	       pixels[2] == 122 && pixels[3] == 82);
	ms912x_android_frame_pipeline_release(pipeline, slot);
	ms912x_android_frame_pipeline_destroy(pipeline);
}

static void test_generation_reconfiguration(void)
{
	struct ms912x_android_frame_pipeline *pipeline;
	struct ms912x_android_frame_info info;
	const uint8_t rgba[] = { 11, 22, 33, 44, 55, 66, 77, 88 };
	const uint8_t *pixels;
	unsigned int slot;

	pipeline = ms912x_android_frame_pipeline_create(2, 1, 4);
	assert(pipeline != NULL);
	assert(ms912x_android_frame_pipeline_offer_rgba(
		pipeline, rgba, sizeof(rgba), 0, 8, 4, 0, 0, 2, 1, 3) == -2);
	assert(ms912x_android_frame_pipeline_reconfigure(pipeline, 2, 1, 5) == 0);
	offer(pipeline, rgba, sizeof(rgba), 2, 1, 5);
	assert(ms912x_android_frame_pipeline_acquire(
		pipeline, &slot, &info, &pixels) == 0);
	assert(info.generation == 5 && info.sequence == 1);
	assert(pixels[0] == 134 && pixels[1] == 33 &&
	       pixels[2] == 122 && pixels[3] == 70);
	ms912x_android_frame_pipeline_release(pipeline, slot);
	assert(ms912x_android_frame_pipeline_reconfigure(pipeline, 2, 1, 5) == -1);
	ms912x_android_frame_pipeline_destroy(pipeline);
}

static void test_rejects_out_of_bounds_source(void)
{
	struct ms912x_android_frame_pipeline *pipeline;
	const uint8_t rgba[] = { 1, 2, 3, 4 };

	pipeline = ms912x_android_frame_pipeline_create(2, 1, 1);
	assert(pipeline != NULL);
	assert(ms912x_android_frame_pipeline_offer_rgba(
		pipeline, rgba, sizeof(rgba), 0, 8, 4, 1, 0, 2, 1, 1) == -1);
	ms912x_android_frame_pipeline_destroy(pipeline);
}

static void test_rejects_odd_width(void)
{
	assert(ms912x_android_frame_pipeline_create(1, 1, 1) == NULL);
}

static void test_shared_mode3_full_frame_encoding(void)
{
	const uint8_t uyvy[] = { 1, 2, 3, 4, 5, 6, 7, 8 };
	const uint8_t expected[] = {
		0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x02,
		1, 2, 3, 4, 5, 6, 7, 8,
		0xff, 0xc0, 0, 0, 0, 0, 0, 0,
	};
	uint8_t packet[sizeof(expected)];

	assert(ms912x_core_mode3_frame_size(2, 2) == sizeof(expected));
	assert(ms912x_core_mode3_encode_rectangles(packet, sizeof(packet),
		uyvy, 4, 2, 2, &(struct drm_rect){0, 0, 2, 2}, 1) ==
	       (int)sizeof(expected));
	assert(memcmp(packet, expected, sizeof(expected)) == 0);
	assert(ms912x_core_mode3_frame_size(4096, 2) == 0);
	assert(ms912x_core_mode3_encode_rectangles(packet, sizeof(packet) - 1,
		uyvy, 4, 2, 2, &(struct drm_rect){0, 0, 2, 2}, 1) < 0);
}

static void test_shared_mode3_rectangle_encoding(void)
{
	const uint8_t uyvy[] = {
		1, 2, 3, 4, 5, 6, 7, 8,
		9, 10, 11, 12, 13, 14, 15, 16,
	};
	const struct ms912x_core_rect rects[] = {
		{ .x1 = 2, .y1 = 0, .x2 = 4, .y2 = 2 },
	};
	const uint8_t expected[] = {
		0xff, 0x00, 0x00, 0x20, 0x00, 0x00, 0x20, 0x02,
		5, 6, 7, 8, 13, 14, 15, 16,
		0xff, 0xc0, 0, 0, 0, 0, 0, 0,
	};
	uint8_t packet[sizeof(expected)];

	assert(ms912x_core_mode3_encode_rectangles(
		packet, sizeof(packet), uyvy, 8, 4, 2, rects, 1) ==
	       (int)sizeof(expected));
	assert(memcmp(packet, expected, sizeof(expected)) == 0);
	assert(ms912x_core_mode3_encode_rectangles(
		packet, sizeof(packet) - 1U, uyvy, 8, 4, 2, rects, 1) == -ENOSPC);
	assert(ms912x_core_mode3_encode_rectangles(
		packet, sizeof(packet), uyvy, 8, 4, 2,
		&(struct ms912x_core_rect){ .x1 = 1, .y1 = 0, .x2 = 4, .y2 = 1 },
		1) == -EINVAL);
}

static void test_pipeline_sends_only_damage_rectangles(void)
{
	struct ms912x_android_frame_pipeline *pipeline;
	struct ms912x_android_frame_info info;
	const uint8_t black[] = {
		0, 0, 0, 255, 0, 0, 0, 255,
		0, 0, 0, 255, 0, 0, 0, 255,
		0, 0, 0, 255, 0, 0, 0, 255,
		0, 0, 0, 255, 0, 0, 0, 255,
	};
	const uint8_t white_then_red[] = {
		0, 0, 0, 255, 0, 0, 0, 255,
		255, 0, 0, 255, 255, 0, 0, 255,
		0, 0, 0, 255, 0, 0, 0, 255,
		0, 0, 0, 255, 0, 0, 0, 255,
	};
	const uint8_t expected_header[] = {
		0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x01,
	};
	uint8_t packet[2U * 2U * 2U + 64U];
	int length;

	pipeline = ms912x_android_frame_pipeline_create(4, 2, 1);
	assert(pipeline != NULL);
	offer(pipeline, black, sizeof(black), 4, 2, 1);
	length = ms912x_android_frame_pipeline_encode_latest(
		pipeline, packet, sizeof(packet), &info);
	assert(length == 4 * 2 * 2 + 16);
	assert(info.width == 4 && info.height == 2 && info.sequence == 1);
	assert(ms912x_android_frame_pipeline_complete(pipeline, info.sequence, 1) == 0);
	assert(ms912x_android_frame_pipeline_encode_latest(
		pipeline, packet, sizeof(packet), &info) == 32);
	assert(ms912x_android_frame_pipeline_complete(pipeline, info.sequence, 1) == 0);
	offer(pipeline, white_then_red, sizeof(white_then_red), 4, 2, 1);
	length = ms912x_android_frame_pipeline_encode_latest(
		pipeline, packet, sizeof(packet), &info);
	assert(length == 24);
	assert(memcmp(packet, expected_header, sizeof(expected_header)) == 0);
	assert(packet[1] == 0x00); /* The 16-pixel block clips to the four-pixel row. */
	assert(ms912x_android_frame_pipeline_complete(pipeline, info.sequence, 1) == 0);
	offer(pipeline, white_then_red, sizeof(white_then_red), 4, 2, 1);
	assert(ms912x_android_frame_pipeline_encode_latest(
		pipeline, packet, sizeof(packet), &info) == 24);
	assert(ms912x_android_frame_pipeline_complete(pipeline, info.sequence, 1) == 0);
	assert(ms912x_android_frame_pipeline_encode_latest(
		pipeline, packet, sizeof(packet), &info) == 0);
	ms912x_android_frame_pipeline_destroy(pipeline);
}

static void test_shared_mode3_color_bar_pattern(void)
{
	uint8_t packet[640U * 480U * 2U + 16U];
	size_t length = sizeof(packet);
	const uint8_t *pixels = packet + MS912X_CORE_MODE3_HEADER_BYTES;
	const uint8_t expected_header[] = {
		0xff, 0x00, 0x00, 0x00, 0x00, 0x28, 0x01, 0xe0,
	};
	const uint8_t white_pair[] = { 128, 234, 128, 234 };
	const uint8_t black_pair[] = { 128, 16, 128, 16 };
	const uint8_t expected_trailer[] = {
		0xff, 0xc0, 0, 0, 0, 0, 0, 0,
	};

	assert(ms912x_core_mode3_generate_color_bars(packet, length, 640, 480) ==
	       (int)length);
	assert(memcmp(packet, expected_header, sizeof(expected_header)) == 0);
	assert(memcmp(pixels, white_pair, sizeof(white_pair)) == 0);
	assert(memcmp(pixels + 479U * 640U * 2U,
		      black_pair, sizeof(black_pair)) == 0);
	assert(memcmp(packet + length - sizeof(expected_trailer),
		      expected_trailer, sizeof(expected_trailer)) == 0);
	assert(ms912x_core_mode3_generate_color_bars(packet, length, 641, 480) == -1);
}

static void test_uyvy_scan_compares_full_words_and_maps_pairs(void)
{
	const uint32_t baseline[] = {
		0x44332211, 0x88776655, 0xccbbaa99, 0x00ffeedd, 0xdeadbeef, 0xdeadbeef,
		0x01020304, 0x05060708, 0x090a0b0c, 0x0d0e0f10, 0xdeadbeef, 0xdeadbeef,
		0x11121314, 0x15161718, 0x191a1b1c, 0x1d1e1f20, 0xdeadbeef, 0xdeadbeef,
	};
	uint32_t newest[sizeof(baseline) / sizeof(baseline[0])];
	struct ms912x_core_rect rects[3] = {
		{ .x1 = -7, .y1 = -7, .x2 = -7, .y2 = -7 },
	};
	size_t rect_count = 0;
	struct ms912x_scan_rect_index *index = ms912x_scan_rect_index_alloc();
	assert(index);

	memcpy(newest, baseline, sizeof(baseline));
	newest[0] ^= 0xff000000U;
	newest[6 + 3] ^= 0x00000001U;
	newest[2 * 6 + 2] ^= 0xff000000U;
	assert(ms912x_core_scan_words_backend(baseline, 18, newest, 18, 8, 3, 6, 1,
				      UINT32_MAX, rects, 3, &rect_count, index, MS912X_SCAN_AUTO) == 0);
	assert(rect_count == 1);
	assert(rects[0].x1 == 0 && rects[0].x2 == 8 &&
	       rects[0].y1 == 0 && rects[0].y2 == 3);
	assert(ms912x_core_scan_words_backend(baseline, 18, newest, 18, 7, 3, 6, 1,
				      UINT32_MAX, rects, 3, &rect_count, index, MS912X_SCAN_AUTO) == -EINVAL);
	ms912x_scan_rect_index_free(index);
}

int main(void)
{
	test_crop_stride_and_rgba_to_uyvy();
	test_newest_pending_and_active_immutability();
	test_generation_reconfiguration();
	test_rejects_out_of_bounds_source();
	test_rejects_odd_width();
	test_shared_mode3_full_frame_encoding();
	test_shared_mode3_rectangle_encoding();
	test_pipeline_sends_only_damage_rectangles();
	test_shared_mode3_color_bar_pattern();
	test_uyvy_scan_compares_full_words_and_maps_pairs();
	puts("Android frame pipeline tests passed");
	return 0;
}
