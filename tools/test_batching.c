// SPDX-License-Identifier: GPL-2.0-only

#include <assert.h>
#include <stdio.h>
#include <drm/drm_rect.h>

#include "ms912x_batch.h"
#include "ms912x_mode3.h"

#define MAX_RECTS MS912X_MAX_BATCH_RECTS
#define WIDTH 1280
#define HEIGHT 720
#define FRAME_BYTES ms912x_mode3_rect_encoded_bytes(WIDTH, HEIGHT)

static unsigned int cases;
static unsigned int random_state = 0x912c;

static unsigned int next_random(void)
{
	random_state ^= random_state << 13;
	random_state ^= random_state >> 17;
	random_state ^= random_state << 5;
	return random_state;
}

static bool contains(const struct drm_rect *a, const struct drm_rect *b)
{
	return a->x1 <= b->x1 && a->y1 <= b->y1 &&
		a->x2 >= b->x2 && a->y2 >= b->y2;
}

static void verify(const struct drm_rect *before, unsigned int before_count,
		   const struct drm_rect *after, unsigned int after_count)
{
	unsigned int i, j;

	assert(after_count <= before_count);
	assert(ms912x_rects_payload_bytes(after, after_count) <=
	       ms912x_rects_payload_bytes(before, before_count) * 2);
	assert(ms912x_rects_payload_bytes(after, after_count) <=
	       ms912x_mode3_rect_encoded_bytes(WIDTH, HEIGHT));
	for (i = 0; i < after_count; i++) {
		assert(after[i].x1 >= 0 && after[i].y1 >= 0);
		assert(after[i].x2 <= WIDTH && after[i].y2 <= HEIGHT);
		assert(after[i].x1 < after[i].x2 && after[i].y1 < after[i].y2);
		assert(!(after[i].x1 & 3) && !(after[i].x2 & 3));
	}
	for (i = 0; i < before_count; i++) {
		for (j = 0; j < after_count; j++)
			if (contains(&after[j], &before[i]))
				break;
		assert(j < after_count);
	}
	cases++;
}

static void compact_cases(void)
{
	struct drm_rect input[MAX_RECTS], result[MAX_RECTS];
	unsigned int count, i, iteration;

	count = 0;
	ms912x_rects_compact_indexed(NULL, &count, FRAME_BYTES, NULL);
	assert(!count);
	/* Two successive updates to the same 60% video region, including replay. */
	input[0] = input[1] = DRM_RECT_INIT(0, 0, WIDTH, 432);
	memcpy(result, input, 2 * sizeof(*input));
	count = 2;
	ms912x_rects_compact_indexed(result, &count, FRAME_BYTES, NULL);
	assert(count == 1);
	verify(input, 2, result, count);
	printf("60%% video + replay: %llu -> %llu bytes\n",
	       (unsigned long long)ms912x_rects_payload_bytes(input, 2),
	       (unsigned long long)ms912x_rects_payload_bytes(result, count));

	/* Repeated sparse islands must stay separate instead of filling the screen. */
	input[0] = input[2] = DRM_RECT_INIT(0, 0, 16, 16);
	input[1] = input[3] = DRM_RECT_INIT(1200, 680, 16, 16);
	memcpy(result, input, 4 * sizeof(*input));
	count = 4;
	ms912x_rects_compact_indexed(result, &count, FRAME_BYTES, NULL);
	assert(count == 2);
	verify(input, 4, result, count);

	/* Containment, touching edges, and overlap with an expensive enclosing box. */
	input[0] = DRM_RECT_INIT(0, 0, 128, 64);
	input[1] = DRM_RECT_INIT(4, 4, 16, 16);
	input[2] = DRM_RECT_INIT(128, 0, 128, 64);
	memcpy(result, input, 3 * sizeof(*input));
	count = 3;
	ms912x_rects_compact_indexed(result, &count, FRAME_BYTES, NULL);
	assert(count == 1 && result[0].x2 == 256);
	verify(input, 3, result, count);
	input[0] = DRM_RECT_INIT(0, 0, 4, 600);
	input[1] = DRM_RECT_INIT(0, 0, 1200, 4);
	memcpy(result, input, 2 * sizeof(*input));
	count = 2;
	ms912x_rects_compact_indexed(result, &count, FRAME_BYTES, NULL);
	assert(count == 2);
	verify(input, 2, result, count);

	/* Fragmented repeated video and maximum-size batches. */
	for (i = 0; i < MAX_RECTS; i++)
		input[i] = DRM_RECT_INIT((i % 20) * 64, ((i / 20) % 20) * 20, 64, 20);
	memcpy(result, input, sizeof(input));
	count = MAX_RECTS;
	ms912x_rects_compact_indexed(result, &count, FRAME_BYTES, NULL);
	verify(input, MAX_RECTS, result, count);
	for (i = 0; i < MAX_RECTS; i++)
		input[i] = DRM_RECT_INIT(0, 0, WIDTH, HEIGHT);
	memcpy(result, input, sizeof(input));
	count = MAX_RECTS;
	ms912x_rects_compact_indexed(result, &count, FRAME_BYTES, NULL);
	assert(count == 1);
	verify(input, MAX_RECTS, result, count);

	for (iteration = 0; iteration < 12000; iteration++) {
		unsigned int original_count = next_random() % 256 + 1;

		for (i = 0; i < original_count; i++) {
			unsigned int x = next_random() % (WIDTH / 4);
			unsigned int y = next_random() % HEIGHT;
			unsigned int w = 1 + next_random() % (WIDTH / 4 - x);
			unsigned int h = 1 + next_random() % (HEIGHT - y);

			if (iteration & 1) {
				w = min(w, 4U);
				h = min(h, 8U);
			}
			input[i] = DRM_RECT_INIT(x * 4, y, w * 4, h);
			if (i && !(i % 4))
				input[i] = input[next_random() % i];
		}
		memcpy(result, input, original_count * sizeof(*input));
		count = original_count;
		ms912x_rects_compact_indexed(result, &count, FRAME_BYTES, NULL);
		verify(input, original_count, result, count);
	}
}

static bool pixel_covered(const struct drm_rect *rects, unsigned int count,
			  int x, int y)
{
	unsigned int i;

	for (i = 0; i < count; i++)
		if (x >= rects[i].x1 && x < rects[i].x2 &&
		    y >= rects[i].y1 && y < rects[i].y2)
			return true;
	return false;
}

static void verify_exact_union(const struct drm_rect *before, unsigned int before_count,
			       const struct drm_rect *after, unsigned int after_count)
{
	int x, y;

	verify(before, before_count, after, after_count);
	for (y = 0; y < HEIGHT; y++)
		for (x = 0; x < WIDTH; x += 4)
			assert(pixel_covered(before, before_count, x, y) ==
			       pixel_covered(after, after_count, x, y));
}

static void excessive_merge_cases(void)
{
	struct drm_rect input[4], result[4];
	struct drm_rect enclosing = DRM_RECT_INIT(0, 0, WIDTH, 432);
	unsigned int count;

	/* Replay duplicates must not make a large undamaged gap look economical. */
	input[0] = input[2] = DRM_RECT_INIT(0, 0, 480, 432);
	input[1] = input[3] = DRM_RECT_INIT(800, 0, 480, 432);
	assert(ms912x_rects_payload_bytes(&enclosing, 1) <
	       ms912x_rects_payload_bytes(input, 4));
	memcpy(result, input, sizeof(input));
	count = 4;
	ms912x_rects_compact_indexed(result, &count, FRAME_BYTES, NULL);
	assert(count == 2);
	verify_exact_union(input, 4, result, count);

	/* Shifted overlapping rectangles have uncovered corners inside their bounds. */
	input[0] = input[2] = DRM_RECT_INIT(0, 0, 64, 64);
	input[1] = input[3] = DRM_RECT_INIT(16, 16, 64, 64);
	memcpy(result, input, sizeof(input));
	count = 4;
	ms912x_rects_compact_indexed(result, &count, FRAME_BYTES, NULL);
	assert(count == 2);
	verify_exact_union(input, 4, result, count);

	/* Tiny damage may fill a small gap within its area-dependent allowance. */
	input[0] = DRM_RECT_INIT(0, 0, 4, 1);
	input[1] = DRM_RECT_INIT(8, 0, 4, 1);
	memcpy(result, input, 2 * sizeof(*input));
	count = 2;
	ms912x_rects_compact_indexed(result, &count, FRAME_BYTES, NULL);
	assert(count == 1);
	verify(input, 2, result, count);

	/* The final current+replay assembly must obey the same spatial policy. */
	input[0] = DRM_RECT_INIT(0, 0, 480, 432);
	input[1] = DRM_RECT_INIT(800, 0, 480, 432);
	ms912x_batch_build(result, &count, 4, input, 2, input, 2, FRAME_BYTES, NULL);
	assert(count == 2);
	verify_exact_union(input, 2, result, count);
	puts("excessive merge regression: replay duplicates and large undamaged gaps remain separate");
}

static void overlay_cases(void)
{
	struct drm_rect rects[5], original[5], overlay;
	unsigned int count, i;

	for (i = 0; i < 2000; i++) {
		unsigned int old_count;

		rects[0] = DRM_RECT_INIT(0, 0, 16, 16);
		rects[1] = DRM_RECT_INIT(1200, 680, 16, 16);
		overlay = i & 1 ? rects[0] : DRM_RECT_INIT(600, 320, 128, 128);
		count = 2;
		if (i & 2)
			rects[count++] = overlay;
		memcpy(original, rects, count * sizeof(*rects));
		old_count = count;
		original[old_count++] = overlay;
		ms912x_batch_include_overlay(rects, &count, i & 4 ? count : 5,
					    &overlay, FRAME_BYTES);
		assert(contains(&rects[count - 1], &overlay));
		/* Capacity exhaustion may increase area, but still fits one full frame. */
		assert(ms912x_rects_payload_bytes(rects, count) <=
		       ms912x_mode3_rect_encoded_bytes(WIDTH, HEIGHT));
		for (unsigned int j = 0; j < old_count; j++) {
			unsigned int k;

			for (k = 0; k < count; k++)
				if (contains(&rects[k], &original[j]))
					break;
			assert(k < count);
		}
		cases++;
	}
	/* A full-screen overlay must collapse the batch and remain last. */
	rects[0] = DRM_RECT_INIT(0, 0, 16, 16);
	overlay = DRM_RECT_INIT(0, 0, WIDTH, HEIGHT);
	count = 1;
	ms912x_batch_include_overlay(rects, &count, 5, &overlay, FRAME_BYTES);
	assert(count == 1 && drm_rect_equals(rects, &overlay));
}

static void replay_history_cases(void)
{
	struct drm_rect pending[64], current[64], saved_current[64];
	struct drm_rect previous[64], batch[128];
	unsigned int previous_count = 0, frame;

	/* Three producer updates can accumulate while one USB request is active. */
	for (frame = 0; frame < 1000; frame++) {
		unsigned int count = 3, current_count, batch_count;

		pending[0] = DRM_RECT_INIT((frame % 8) * 4, 40, 800, 400);
		pending[1] = pending[0];
		pending[2] = DRM_RECT_INIT((frame % 8) * 4 + 4, 40, 800, 400);
		memcpy(current, pending, count * sizeof(*pending));
		current_count = count;
		ms912x_rects_compact_indexed(current, &current_count, FRAME_BYTES, NULL);
		verify(pending, count, current, current_count);
		memcpy(saved_current, current, current_count * sizeof(*current));
		memcpy(batch, current, current_count * sizeof(*current));
		batch_count = current_count;
		assert(ms912x_mode3_append_rects(batch, 128, &batch_count,
						previous, previous_count));
		memcpy(pending, batch, batch_count * sizeof(*batch));
		count = batch_count;
		ms912x_batch_build(batch, &batch_count, 128,
				   current, current_count, previous, previous_count, FRAME_BYTES, NULL);
		verify(pending, count, batch, batch_count);
		/* Combined coverage must not pollute the next current-damage history. */
		assert(!memcmp(current, saved_current, current_count * sizeof(*current)));
		memcpy(previous, current, current_count * sizeof(*current));
		previous_count = current_count;
	}
}

static void allocation_fallback_cases(void)
{
	struct drm_rect current[32], previous[32], original[64];
	struct drm_rect saved_current[32], saved_previous[32], result[65];
	unsigned int capacity, i;

	for (i = 0; i < 32; i++) {
		current[i] = DRM_RECT_INIT((i % 8) * 160, (i / 8) * 160, 16, 16);
		previous[i] = DRM_RECT_INIT((i % 8) * 160 + 24,
					  (i / 8) * 160 + 24, 16, 16);
	}
	memcpy(original, current, sizeof(current));
	memcpy(original + 32, previous, sizeof(previous));
	memcpy(saved_current, current, sizeof(current));
	memcpy(saved_previous, previous, sizeof(previous));
	for (capacity = 1; capacity <= 64; capacity++) {
		unsigned int count;

		memset(result, 0x5a, sizeof(result));
		ms912x_batch_build(result, &count, capacity, current, 32, previous, 32, FRAME_BYTES, NULL);
		assert(count && count <= capacity);
		assert(ms912x_rects_payload_bytes(result, count) <=
		       ms912x_mode3_rect_encoded_bytes(WIDTH, HEIGHT));
		for (i = 0; i < 64; i++) {
			unsigned int j;

			for (j = 0; j < count; j++)
				if (contains(&result[j], &original[i]))
					break;
			assert(j < count);
		}
		for (i = capacity * sizeof(*result); i < sizeof(result); i++)
			assert(((const unsigned char *)result)[i] == 0x5a);
		assert(!memcmp(current, saved_current, sizeof(current)));
		assert(!memcmp(previous, saved_previous, sizeof(previous)));
		cases++;
	}
	/* Empty history and a first replay-only batch also preserve coverage. */
	capacity = 0;
	ms912x_batch_build(result, &capacity, 64, NULL, 0, NULL, 0, FRAME_BYTES, NULL);
	assert(!capacity);
	ms912x_batch_build(result, &capacity, 64, NULL, 0, previous, 32, FRAME_BYTES, NULL);
	verify(previous, 32, result, capacity);
}

static void coalescing_cases(void)
{
	const u64 reference = 512 * 1024;
	const u64 sizes[] = { 0, 1, 64 * 1024, 256 * 1024 - 1,
		256 * 1024, reference - 1, reference, reference + 1, 2 * reference };
	const unsigned int refresh_rates[] = { 24, 30, 50, 60, 75, 120, 144 };
	unsigned int i, j, p, flags;

	for (i = 0; i < sizeof(sizes) / sizeof(*sizes); i++)
		for (j = 0; j < sizeof(refresh_rates) / sizeof(*refresh_rates); j++) {
			u64 half_frame = 1000000000ULL / (2 * refresh_rates[j]);
			u64 high = 4 * half_frame;
			u64 pressure[] = { 0, half_frame, high - 1, high, high + 1 };

			for (p = 0; p < sizeof(pressure) / sizeof(*pressure); p++)
				for (flags = 0; flags < 4; flags++) {
					u64 delay = ms912x_batch_coalesce_ns(sizes[i], half_frame,
						pressure[p], high, flags & 1, flags & 2);
					bool immediate = !sizes[i] || flags || pressure[p] >= high;

					assert(delay == (immediate ? 0 : half_frame));
					cases++;
				}
		}
	assert(ms912x_batch_coalesce_ns(4096, 10000000, 0, 40000000, false, false)
	       == 10000000); /* 50 Hz; small packets send without reaching 512 KiB. */
	assert(ms912x_batch_coalesce_ns(4096, 8333333, 0, 34000000, false, false)
	       == 8333333); /* 60 Hz. */
}

static void watermark_cases(void)
{
	const u64 low = 8000000, high = 34000000, half_frame = 8333333;
	const u64 pressure[] = { 0, low, low + 1, high - 1, high,
		2 * high, high - 1, low + 1, low, 0 };
	const bool expected[] = { false, false, false, false, true,
		true, true, true, false, false };
	bool backpressured = false;
	unsigned int i;

	for (i = 0; i < sizeof(pressure) / sizeof(*pressure); i++) {
		backpressured = ms912x_batch_backpressured(pressure[i], low, high,
							  backpressured);
		assert(backpressured == expected[i]);
		assert(ms912x_batch_coalesce_ns(1048576, half_frame, pressure[i], high,
			backpressured, false) == (backpressured ? 0 : half_frame));
		cases++;
	}
}

static void local_group_cases(void)
{
	struct drm_rect input[32], result[32];
	unsigned int count, i, j;
	u64 original;

	/* Eight independent clusters should become eight rectangles in one batch. */
	for (i = 0; i < 8; i++) {
		input[2 * i] = DRM_RECT_INIT(i * 160, 0, 100, 100);
		input[2 * i + 1] = DRM_RECT_INIT(i * 160 + 4, 4, 100, 100);
	}
	ms912x_batch_build(result, &count, 32, input, 16, NULL, 0, FRAME_BYTES, NULL);
	assert(count == 8);
	verify(input, 16, result, count);
	for (i = 0; i < count; i++)
		assert(drm_rect_width(&result[i]) == 104 &&
		       drm_rect_height(&result[i]) == 104);

	/* A medium-sized group permits only a modest corner expansion. */
	input[0] = DRM_RECT_INIT(0, 0, 100, 100);
	input[1] = DRM_RECT_INIT(4, 4, 100, 100);
	memcpy(result, input, 2 * sizeof(*input));
	count = 2;
	ms912x_rects_compact_indexed(result, &count, FRAME_BYTES, NULL);
	assert(count == 1);
	verify(input, 2, result, count);

	/* Packet size must not change the local merge decision. */
	input[2] = DRM_RECT_INIT(800, 0, 400, 400);
	memcpy(result, input, 3 * sizeof(*input));
	count = 3;
	ms912x_rects_compact_indexed(result, &count, FRAME_BYTES, NULL);
	assert(count == 2);
	verify(input, 3, result, count);

	/* Small gaps may grow the packet, within the area-dependent allowance. */
	input[0] = DRM_RECT_INIT(0, 0, 512, 4);
	input[1] = DRM_RECT_INIT(516, 0, 512, 4);
	original = ms912x_rects_payload_bytes(input, 2);
	memcpy(result, input, 2 * sizeof(*input));
	count = 2;
	ms912x_rects_compact_indexed(result, &count, FRAME_BYTES, NULL);
	assert(count == 1 && ms912x_rects_payload_bytes(result, count) > original);
	verify(input, 2, result, count);

	/* Long merge chains must not compound the allowed extra area. */
	for (i = 0; i < 32; i++)
		input[i] = DRM_RECT_INIT(i * 4, i * 4, 64, 64);
	memcpy(result, input, sizeof(input));
	count = 32;
	ms912x_rects_compact_indexed(result, &count, FRAME_BYTES, NULL);
	verify(input, 32, result, count);
	for (i = 0; i < count; i++) {
		u64 covered = 0, area = drm_rect_width(&result[i]) * drm_rect_height(&result[i]);
		int x, y;

		for (y = result[i].y1; y < result[i].y2; y++)
			for (x = result[i].x1; x < result[i].x2; x += 4)
				if (pixel_covered(input, 32, x, y))
					covered += 4;
		assert(area - covered <= ms912x_batch_extra_limit(covered));
	}
	/* A multi-rectangle batch has one final marker, after all intermediate markers. */
	for (j = 0; j < count; j++) {
		u8 trailer[8];

		ms912x_mode3_write_trailer(trailer, j + 1 == count);
		assert(trailer[1] == (j + 1 == count ? 0xc0 : 0x80));
	}
	puts("local groups: area-dependent budget, bounded growth, one final batch marker pass");
}

static void dynamic_budget_cases(void)
{
	const u64 areas[] = { 0, 1, 32, 63, 64, 65, 127, 128, 129,
		255, 256, 257, 512, 640, 1024, 2047, 2048, 2049, 4096,
		16383, 16384, 16385, 65536, 1048576 };
	const u64 expected[] = { 0, 1, 32, 63, 64, 65, 127, 128, 127,
		64, 64, 63, 32, 25, 16, 8, 8, 8, 16, 63, 64, 64, 256, 4096 };
	struct drm_rect input[32], result[32];
	unsigned int i, count;

	for (i = 0; i < sizeof(areas) / sizeof(*areas); i++) {
		assert(ms912x_batch_extra_limit(areas[i]) == expected[i]);
		cases++;
	}
	for (i = 1; i <= WIDTH * HEIGHT; i++) {
		u64 limit = ms912x_batch_extra_limit(i);
		u64 old_limit = max(min((u64)i, (u64)64), (u64)i / 256);

		assert(limit <= i);
		if (i <= 128)
			assert(limit == i);
		else if (i < 2048)
			assert(limit * i <= 16384 && (limit + 1) * i > 16384);
		else
			assert(limit * 256 <= i && (limit + 1) * 256 > i);
		if (i < 256)
			assert(limit >= old_limit);
		else if (i < 16384)
			assert(limit <= old_limit);
	}
	/* Area, rather than individual dimensions: 32 covered pixels may become 64. */
	input[0] = DRM_RECT_INIT(0, 0, 4, 4);
	input[1] = DRM_RECT_INIT(12, 0, 4, 4);
	memcpy(result, input, 2 * sizeof(*input));
	count = 2;
	ms912x_rects_compact_indexed(result, &count, FRAME_BYTES, NULL);
	assert(count == 1 && drm_rect_width(result) * drm_rect_height(result) == 64);
	verify(input, 2, result, count);

	/* The same two tiny rectangles cannot add more than their covered area. */
	input[1] = DRM_RECT_INIT(16, 0, 4, 4);
	memcpy(result, input, 2 * sizeof(*input));
	count = 2;
	ms912x_rects_compact_indexed(result, &count, FRAME_BYTES, NULL);
	assert(count == 2);
	verify_exact_union(input, 2, result, count);

	/* The wider low-area shoulder admits expansion rejected by the old curve. */
	input[0] = DRM_RECT_INIT(0, 0, 4, 12);
	input[1] = DRM_RECT_INIT(12, 0, 4, 12);
	memcpy(result, input, 2 * sizeof(*input));
	count = 2;
	ms912x_rects_compact_indexed(result, &count, FRAME_BYTES, NULL);
	assert(count == 1 && drm_rect_width(result) * drm_rect_height(result) == 192);
	verify(input, 2, result, count);

	/* The tighter middle rejects 32 extra pixels over 1,024 covered pixels. */
	input[0] = DRM_RECT_INIT(0, 0, 128, 4);
	input[1] = DRM_RECT_INIT(136, 0, 128, 4);
	memcpy(result, input, 2 * sizeof(*input));
	count = 2;
	ms912x_rects_compact_indexed(result, &count, FRAME_BYTES, NULL);
	assert(count == 2);
	verify_exact_union(input, 2, result, count);

	/* A long chain must re-evaluate the decreasing allowance for the whole group. */
	for (i = 0; i < 32; i++)
		input[i] = DRM_RECT_INIT(i * 12, 0, 4, 4);
	memcpy(result, input, sizeof(input));
	count = 32;
	ms912x_rects_compact_indexed(result, &count, FRAME_BYTES, NULL);
	assert(count > 1);
	verify(input, 32, result, count);
	for (i = 0; i < count; i++) {
		u64 covered = 0, area = drm_rect_width(&result[i]) * drm_rect_height(&result[i]);
		int x, y;

		for (y = result[i].y1; y < result[i].y2; y++)
			for (x = result[i].x1; x < result[i].x2; x += 4)
				if (pixel_covered(input, 32, x, y))
					covered += 4;
		assert(area - covered <= ms912x_batch_extra_limit(covered));
	}
	puts("dynamic budget: broad <=128-pixel shoulder, quadratic taper and merge chains pass");
}

static void staged_compaction_budget_cases(void)
{
	struct drm_rect source[8] = {
		DRM_RECT_INIT(0, 0, 8, 8), DRM_RECT_INIT(12, 0, 8, 8),
		DRM_RECT_INIT(24, 0, 8, 8), DRM_RECT_INIT(0, 12, 8, 8),
		DRM_RECT_INIT(24, 12, 8, 8), DRM_RECT_INIT(0, 24, 8, 8),
		DRM_RECT_INIT(12, 24, 8, 8), DRM_RECT_INIT(24, 24, 8, 8),
	};
	struct drm_rect pending[16], current[16], batch[32];
	unsigned int pending_count = 4, current_count, batch_count, i;

	memcpy(pending, source, pending_count * sizeof(*pending));
	ms912x_rects_compact_exact(pending, &pending_count);
	memcpy(pending + pending_count, source + 4, 4 * sizeof(*pending));
	pending_count += 4;
	ms912x_rects_compact_exact(pending, &pending_count);

	memcpy(current, pending, pending_count * sizeof(*current));
	current_count = pending_count;
	ms912x_rects_compact_exact(current, &current_count);
	ms912x_batch_build(batch, &batch_count,
			   sizeof(batch) / sizeof(*batch),
			   current, current_count, source,
			   sizeof(source) / sizeof(*source),
			   FRAME_BYTES, NULL);
	verify(source, sizeof(source) / sizeof(*source), batch, batch_count);
	for (i = 0; i < batch_count; i++) {
		u64 covered = 0;
		u64 area = drm_rect_width(&batch[i]) * drm_rect_height(&batch[i]);
		int x, y;

		for (y = batch[i].y1; y < batch[i].y2; y++)
			for (x = batch[i].x1; x < batch[i].x2; x++)
				covered += pixel_covered(source,
						  sizeof(source) / sizeof(*source), x, y);
		assert(area - covered <= ms912x_batch_extra_limit(covered));
	}
	puts("staged compaction: exact intermediate merges preserve one final expansion budget");
}

/* Interleaved spans must merge even after leaving the eight local candidates. */
static void indexed_compaction_cases(void)
{
	struct ms912x_compact_index *index = calloc(1, sizeof(*index));
	struct ms912x_compact_index *peer = calloc(1, sizeof(*peer));
	struct drm_rect input[MAX_RECTS], result[MAX_RECTS];
	unsigned int count, before_count, i, row, column;

	assert(index && peer);
	for (unsigned int axis = 0; axis < 2; axis++) {
		before_count = 0;
		for (row = 0; row < 16; row++)
			for (column = 0; column < 16; column++)
				input[before_count++] = axis ?
					DRM_RECT_INIT(row * 8, column * 32, 8, 8) :
					DRM_RECT_INIT(column * 32, row * 8, 8, 8);
		memcpy(result, input, before_count * sizeof(*input));
		count = before_count;
		/* Exercise generation wrap and an unrelated adapter's scratch. */
		index->epoch = ~0U;
		memset(index->entries, 0xff, sizeof(index->entries));
		ms912x_rects_compact_indexed(result, &count, FRAME_BYTES, index);
		assert(count == 16 && index->epoch == 2 && !peer->epoch);
		verify(input, before_count, result, count);
		assert(ms912x_rects_payload_bytes(result, count) == 16 * (8 * 128 * 2 + 16));
	}
	/* Overlapping replay columns save duplicated pixels as well as headers. */
	before_count = 0;
	for (row = 0; row < 2; row++)
		for (column = 0; column < 16; column++)
			input[before_count++] = DRM_RECT_INIT(column * 64, row * 128, 16, 256);
	memcpy(result, input, before_count * sizeof(*input));
	count = before_count;
	ms912x_batch_build(result, &count, MAX_RECTS, input, 16, input + 16, 16,
			   FRAME_BYTES, index);
	assert(count == 16);
	verify(input, before_count, result, count);

	/* Reuse the same index with many collisions and unordered, clipped inputs. */
	for (unsigned int iteration = 0; iteration < 300; iteration++) {
		before_count = iteration == 0 ? MAX_RECTS : 9 + next_random() % 500;
		for (i = 0; i < before_count; i++) {
			unsigned int x = next_random() % (WIDTH / 4) * 4;
			unsigned int y = next_random() % HEIGHT;

			input[i] = DRM_RECT_INIT(x, y, 4, 1);
		}
		memcpy(result, input, before_count * sizeof(*input));
		count = before_count;
		u32 previous_epoch = (iteration & 1 ? peer : index)->epoch;
		ms912x_rects_compact_indexed(result, &count, FRAME_BYTES,
					    iteration & 1 ? peer : index);
		assert((iteration & 1 ? peer : index)->epoch == previous_epoch + 2);
		verify(input, before_count, result, count);
	}
	free(index);
	free(peer);
	puts("indexed compaction: interleaved axes, overlapping replay, wrap, maximum input and private index reuse pass");
}

int main(void)
{
	indexed_compaction_cases();
	compact_cases();
	excessive_merge_cases();
	overlay_cases();
	replay_history_cases();
	allocation_fallback_cases();
	coalescing_cases();
	watermark_cases();
	local_group_cases();
	dynamic_budget_cases();
	staged_compaction_budget_cases();
	printf("PASS: %u batch coverage, byte budget, cursor and coalescing cases\n", cases);
	return 0;
}
