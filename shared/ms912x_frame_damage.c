/* SPDX-License-Identifier: GPL-2.0-only */
#include "ms912x_frame_damage.h"
#include "ms912x_mode3_core.h"
#include "ms912x_scan_core.h"
#include "../ms912x_batch.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

struct ms912x_frame_damage {
	uint32_t width, height;
	size_t frame_bytes;
	uint8_t *baseline;
	struct ms912x_compact_index *compact_index;
	struct ms912x_core_rect *rects;
	struct ms912x_core_rect *previous;
	struct ms912x_core_rect *outgoing;
	size_t rect_count, previous_count, outgoing_count;
	const uint8_t *pending;
	uint64_t sequence;
	bool baseline_valid;
	bool can_commit;
};

size_t ms912x_frame_damage_packet_capacity(uint32_t width, uint32_t height)
{
	if (!width || !height || (width & 1U) || width > 0xfffU || height > 0xfffU)
		return 0;
	return (size_t)height * ((size_t)width * 2U + MS912X_CORE_MODE3_OVERHEAD);
}

struct ms912x_frame_damage *ms912x_frame_damage_create(uint32_t width, uint32_t height)
{
	struct ms912x_frame_damage *damage;

	if (!width || !height || (width & 1U) || width > 0xfffU || height > 0xfffU)
		return NULL;
	damage = calloc(1, sizeof(*damage));
	if (!damage)
		return NULL;
	damage->width = width;
	damage->height = height;
	damage->frame_bytes = (size_t)width * height * 2U;
	damage->compact_index = calloc(1, sizeof(*damage->compact_index));
	damage->baseline = calloc(1, damage->frame_bytes);
	/* Use the Linux damage and batch limits for fragmented/interleaved spans. */
	damage->rects = calloc(MS912X_MAX_DAMAGE_RECTS * 2U + MS912X_MAX_BATCH_RECTS, sizeof(*damage->rects));
	if (!damage->compact_index || !damage->baseline || !damage->rects) {
		ms912x_frame_damage_destroy(damage);
		return NULL;
	}
	damage->previous = damage->rects + MS912X_MAX_DAMAGE_RECTS;
	damage->outgoing = damage->previous + MS912X_MAX_DAMAGE_RECTS;
	return damage;
}

void ms912x_frame_damage_destroy(struct ms912x_frame_damage *damage)
{
	if (!damage)
		return;
	free(damage->compact_index);
	free(damage->baseline);
	free(damage->rects);
	free(damage);
}

static bool is_full(const struct ms912x_frame_damage *damage,
		    const struct ms912x_core_rect *rects, size_t count)
{
	return count == 1 && rects[0].x1 == 0 && rects[0].y1 == 0 &&
		rects[0].x2 == (int32_t)damage->width &&
		rects[0].y2 == (int32_t)damage->height;
}

static void outgoing_full(struct ms912x_frame_damage *damage)
{
	damage->outgoing[0] = (struct ms912x_core_rect) {
		.x1 = 0, .y1 = 0, .x2 = (int32_t)damage->width,
		.y2 = (int32_t)damage->height,
	};
	damage->outgoing_count = 1;
}

static int prepare(struct ms912x_frame_damage *damage,
		   uint8_t *packet, size_t capacity,
		   const uint8_t *pixels, uint64_t sequence, bool replay_only)
{
	int result;

	if (!damage || !packet || !pixels || !sequence ||
	    (uintptr_t)pixels % _Alignof(uint32_t))
		return -EINVAL;
	if (damage->pending)
		return -EBUSY;
	if (replay_only) {
		if (!damage->baseline_valid || !damage->previous_count) return 0;
		damage->rect_count = 0;
	} else if (!damage->baseline_valid) {
		damage->rects[0] = (struct ms912x_core_rect) {
			.x1 = 0, .y1 = 0, .x2 = (int32_t)damage->width,
			.y2 = (int32_t)damage->height,
		};
		damage->rect_count = 1;
	} else {
		result = ms912x_core_scan_words(
			(const uint32_t *)damage->baseline, damage->frame_bytes / 4U,
			(const uint32_t *)pixels, damage->frame_bytes / 4U,
			damage->width, damage->height, damage->width / 2U,
			1, UINT32_MAX, damage->rects, MS912X_MAX_DAMAGE_RECTS, &damage->rect_count);
		if (result == -ENOSPC) {
			damage->rects[0] = (struct ms912x_core_rect){0, 0, damage->width, damage->height};
			damage->rect_count = 1;
		} else if (result)
			return result;
	}
	if (!damage->rect_count && !damage->previous_count)
		return 0;
	if (is_full(damage, damage->rects, damage->rect_count) ||
	    is_full(damage, damage->previous, damage->previous_count)) {
		outgoing_full(damage);
	} else {
		unsigned int count = 0;
		ms912x_batch_build(damage->outgoing, &count, MS912X_MAX_BATCH_RECTS,
			damage->rects, damage->rect_count, damage->previous,
			damage->previous_count, capacity, damage->compact_index);
		damage->outgoing_count = count;
	}
	result = ms912x_core_mode3_encode_rectangles(
		packet, capacity, pixels, (size_t)damage->width * 2U,
		damage->width, damage->height, damage->outgoing, damage->outgoing_count);
	if (result == -ENOSPC) {
		/* Preserve all required coverage within the existing packet budget. */
		outgoing_full(damage);
		result = ms912x_core_mode3_encode_rectangles(
			packet, capacity, pixels, (size_t)damage->width * 2U,
			damage->width, damage->height, damage->outgoing, 1);
	}
	if (result > 0) {
		damage->pending = pixels;
		damage->sequence = sequence;
		damage->can_commit = true;
	}
	return result;
}

int ms912x_frame_damage_prepare(struct ms912x_frame_damage *damage,
				uint8_t *packet, size_t capacity,
				const uint8_t *pixels, uint64_t sequence)
{
	return prepare(damage, packet, capacity, pixels, sequence, false);
}

int ms912x_frame_damage_prepare_replay(struct ms912x_frame_damage *damage,
				       uint8_t *packet, size_t capacity, uint64_t sequence)
{
	return prepare(damage, packet, capacity, damage ? damage->baseline : NULL,
		       sequence, true);
}

int ms912x_frame_damage_finish(struct ms912x_frame_damage *damage,
			       uint64_t sequence, bool transferred)
{
	size_t i;

	if (!damage || !damage->pending || damage->sequence != sequence)
		return -ESTALE;
	if (transferred && damage->can_commit) {
		for (i = 0; damage->pending != damage->baseline && i < damage->outgoing_count; ++i) {
			const struct ms912x_core_rect *rect = &damage->outgoing[i];
			size_t y, x_bytes = (size_t)rect->x1 * 2U;
			size_t bytes = (size_t)(rect->x2 - rect->x1) * 2U;

			for (y = (size_t)rect->y1; y < (size_t)rect->y2; ++y) {
				size_t offset = y * (size_t)damage->width * 2U + x_bytes;

				memcpy(damage->baseline + offset, damage->pending + offset, bytes);
			}
		}
		/* History is current-only damage, never the transmitted union. */
		memcpy(damage->previous, damage->rects,
		       damage->rect_count * sizeof(*damage->rects));
		damage->previous_count = damage->rect_count;
		damage->baseline_valid = true;
	} else {
		damage->baseline_valid = false;
		damage->previous_count = 0;
	}
	damage->pending = NULL;
	damage->sequence = 0;
	return 0;
}

void ms912x_frame_damage_invalidate(struct ms912x_frame_damage *damage)
{
	if (!damage)
		return;
	damage->baseline_valid = false;
	damage->can_commit = false;
	damage->previous_count = 0;
}
