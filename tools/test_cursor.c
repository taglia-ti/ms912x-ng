// SPDX-License-Identifier: GPL-2.0-only

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "ms912x_color.h"
#include "ms912x_cursor.h"

u64 ms912x_cursor_composite_uyvy(void *transfer_buffer,
				 const struct drm_rect *rect,
				 const u32 *cursor_pixels, int cursor_x, int cursor_y,
				 unsigned int cursor_width,
				 unsigned int cursor_height,
				 unsigned int cursor_alpha_x,
				 unsigned int cursor_alpha_y,
				 bool premultiplied);

static u32 cursor_pixels[MS912X_CURSOR_MAX_WIDTH * MS912X_CURSOR_MAX_HEIGHT];
static u8 packet[8 + 24];
static unsigned int cases;

static void reset_packet(u8 u, u8 y0, u8 v, u8 y1)
{
	memset(packet, 0, sizeof(packet));
	for (unsigned int offset = 8; offset < 20; offset += 4) {
		packet[offset] = u;
		packet[offset + 1] = y0;
		packet[offset + 2] = v;
		packet[offset + 3] = y1;
	}
}

static void assert_pair(u8 u, u8 y0, u8 v, u8 y1)
{
	if (packet[8] != u || packet[9] != y0 ||
	    packet[10] != v || packet[11] != y1)
		fprintf(stderr, "UYVY expected %u,%u,%u,%u got %u,%u,%u,%u\n",
			u, y0, v, y1, packet[8], packet[9],
			packet[10], packet[11]);
	assert(packet[8] == u);
	assert(packet[9] == y0);
	assert(packet[10] == v);
	assert(packet[11] == y1);
}

static void assert_pair_at(unsigned int offset, u8 u, u8 y0, u8 v, u8 y1)
{
	assert(packet[offset] == u);
	assert(packet[offset + 1] == y0);
	assert(packet[offset + 2] == v);
	assert(packet[offset + 3] == y1);
}

static void opaque_pair(void)
{
	struct drm_rect rect = { .x1 = 4, .y1 = 2, .x2 = 6, .y2 = 3 };
	u64 blended;

	memset(cursor_pixels, 0, sizeof(cursor_pixels));
	cursor_pixels[0] = 0xffff0000;
	cursor_pixels[1] = 0xffff0000;
	reset_packet(128, 50, 128, 60);
	blended = ms912x_cursor_composite_uyvy(
		packet, &rect, cursor_pixels, 4, 2, 2, 1, 0, 0, false);
	assert(blended == 2);
	assert_pair(90, 81, 239, 81);
	cases++;
}

static void transparent_pixels(void)
{
	struct drm_rect rect = { .x1 = 4, .y1 = 2, .x2 = 6, .y2 = 3 };
	u8 before[sizeof(packet)];

	memset(cursor_pixels, 0, sizeof(cursor_pixels));
	reset_packet(91, 42, 173, 77);
	memcpy(before, packet, sizeof(packet));
	assert(!ms912x_cursor_composite_uyvy(
		packet, &rect, cursor_pixels, 4, 2, 2, 1, 0, 0, false));
	assert(!memcmp(packet, before, sizeof(packet)));
	cases++;
}

static void partial_alpha_and_premultiplication(void)
{
	struct drm_rect rect = { .x1 = 4, .y1 = 2, .x2 = 6, .y2 = 3 };

	memset(cursor_pixels, 0, sizeof(cursor_pixels));
	cursor_pixels[0] = 0x800000ff;
	cursor_pixels[1] = 0x800000ff;
	reset_packet(128, 50, 128, 50);
	assert(ms912x_cursor_composite_uyvy(
		packet, &rect, cursor_pixels, 4, 2, 2, 1, 0, 0, false) == 2);
	assert_pair(184, 45, 118, 45);

	cursor_pixels[0] = 0x80000080;
	cursor_pixels[1] = 0x80000080;
	reset_packet(128, 50, 128, 50);
	assert(ms912x_cursor_composite_uyvy(
		packet, &rect, cursor_pixels, 4, 2, 2, 1, 0, 0, true) == 2);
	assert_pair(184, 45, 118, 45);
	cases += 2;
}

static void rectangle_clipping_and_shared_chroma(void)
{
	struct drm_rect rect = { .x1 = 4, .y1 = 2, .x2 = 6, .y2 = 3 };

	memset(cursor_pixels, 0, sizeof(cursor_pixels));
	cursor_pixels[1] = 0xffff0000;
	reset_packet(128, 50, 128, 60);
	assert(ms912x_cursor_composite_uyvy(
		packet, &rect, cursor_pixels, 3, 2, 2, 1, 0, 0, false) == 1);
	assert_pair(109, 81, 184, 60);

	cursor_pixels[0] = 0xffff0000;
	cursor_pixels[1] = 0;
	reset_packet(128, 50, 128, 60);
	assert(ms912x_cursor_composite_uyvy(
		packet, &rect, cursor_pixels, 4, 2, 2, 1, 0, 0, false) == 1);
	assert_pair(109, 81, 184, 60);
	cases += 2;

	memset(cursor_pixels, 0, sizeof(cursor_pixels));
	cursor_pixels[0] = 0xffff0000;
	cursor_pixels[1] = 0xffff0000;
	rect = (struct drm_rect) { .x1 = 2, .y1 = 2, .x2 = 8, .y2 = 3 };
	reset_packet(128, 50, 128, 60);
	assert(ms912x_cursor_composite_uyvy(
		packet, &rect, cursor_pixels, 4, 2, 2, 1, 0, 0, false) == 2);
	assert_pair_at(8, 128, 50, 128, 60);
	assert_pair_at(12, 90, 81, 239, 81);
	assert_pair_at(16, 128, 50, 128, 60);
	cases++;

	memset(cursor_pixels, 0, sizeof(cursor_pixels));
	cursor_pixels[2] = 0xffff0000;
	cursor_pixels[3] = 0xffff0000;
	rect = (struct drm_rect) { .x1 = 6, .y1 = 2, .x2 = 10, .y2 = 3 };
	reset_packet(128, 50, 128, 60);
	assert(ms912x_cursor_composite_uyvy(
		packet, &rect, cursor_pixels, 4, 2, 4, 1, 0, 0, false) == 2);
	assert_pair_at(8, 90, 81, 239, 81);
	assert_pair_at(12, 128, 50, 128, 60);
	cases++;
}

static void empty_or_invalid_cursor(void)
{
	struct drm_rect rect = { .x1 = 4, .y1 = 2, .x2 = 6, .y2 = 3 };

	reset_packet(128, 50, 128, 60);
	assert(!ms912x_cursor_composite_uyvy(
		packet, &rect, NULL, 4, 2, 2, 1, 0, 0, false));
	assert(!ms912x_cursor_composite_uyvy(
		packet, &rect, cursor_pixels, 4, 2, 0, 1, 0, 0, false));
	cases += 2;
}

static void prepared_source_reuse_requires_matching_pixels(void)
{
	u32 candidate[MS912X_CURSOR_MAX_WIDTH * 2] = {};
	u32 cached[MS912X_CURSOR_MAX_WIDTH * 2] = {};

	candidate[MS912X_CURSOR_MAX_WIDTH + 3] = 0xff123456;
	cached[MS912X_CURSOR_MAX_WIDTH + 3] = 0xff123456;
	assert(ms912x_cursor_source_matches(candidate, cached, 8, 2));
	cached[MS912X_CURSOR_MAX_WIDTH + 3] ^= 1;
	assert(!ms912x_cursor_source_matches(candidate, cached, 8, 2));
	assert(!ms912x_cursor_source_matches(candidate, cached, 129, 2));
	cases += 3;
}

int main(void)
{
	opaque_pair();
	transparent_pixels();
	partial_alpha_and_premultiplication();
	rectangle_clipping_and_shared_chroma();
	empty_or_invalid_cursor();
	prepared_source_reuse_requires_matching_pixels();
	printf("PASS cursor compositor: %u scenarios\n", cases);
	return 0;
}
