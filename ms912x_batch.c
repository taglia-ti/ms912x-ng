// SPDX-License-Identifier: GPL-2.0-only

#include <linux/minmax.h>
#include <linux/sort.h>
#include <linux/string.h>

#include <drm/drm_rect.h>

#include "ms912x_batch.h"
#include "ms912x_mode3.h"

u64 ms912x_rects_payload_bytes(const struct drm_rect *rects,
				     unsigned int rect_count)
{
	u64 bytes = 0;
	unsigned int i;

	for (i = 0; i < rect_count; i++)
		bytes += ms912x_mode3_rect_encoded_bytes(
			drm_rect_width(&rects[i]), drm_rect_height(&rects[i]));

	return bytes;
}

static struct drm_rect ms912x_rect_bounds(const struct drm_rect *a,
					 const struct drm_rect *b)
{
	return (struct drm_rect) {
		.x1 = min(a->x1, b->x1),
		.y1 = min(a->y1, b->y1),
		.x2 = max(a->x2, b->x2),
		.y2 = max(a->y2, b->y2),
	};
}

/* Keep spatial neighbours together, with containing rectangles first. */
static int ms912x_rect_compare(const void *left, const void *right)
{
	const struct drm_rect *a = left, *b = right;

	if (a->y1 != b->y1)
		return a->y1 < b->y1 ? -1 : 1;
	if (a->x1 != b->x1)
		return a->x1 < b->x1 ? -1 : 1;
	if (a->y2 != b->y2)
		return a->y2 > b->y2 ? -1 : 1;
	return (a->x2 < b->x2) - (a->x2 > b->x2);
}

/* Use enclosing bounds only as a hard packet-budget fallback. */
static void ms912x_rects_limit_bytes(struct drm_rect *rects,
				    unsigned int *count, u64 maximum_bytes)
{
	struct drm_rect bounds;
	unsigned int i;

	if (*count < 2 ||
	    ms912x_rects_payload_bytes(rects, *count) <= maximum_bytes)
		return;
	bounds = rects[0];
	for (i = 1; i < *count; i++)
		bounds = ms912x_rect_bounds(&bounds, &rects[i]);
	rects[0] = bounds;
	*count = 1;
}

struct ms912x_batch_group {
	struct drm_rect bounds;
	u64 extra_pixels;
};

/* A broad small-area shoulder, quadratic relative taper, then the large-area floor. */
u64 ms912x_batch_extra_limit(u64 covered_pixels)
{
	if (covered_pixels <= MS912X_BATCH_SMALL_AREA)
		return covered_pixels;
	if (covered_pixels >= MS912X_BATCH_TAPER_END_AREA)
		return covered_pixels / MS912X_BATCH_EXTRA_DIVISOR;

	/* The guarded range fits a native unsigned division, including on ARM32. */
	return (MS912X_BATCH_SMALL_AREA * MS912X_BATCH_SMALL_AREA) /
		(unsigned int)covered_pixels;
}

/* Bound added area for the complete group, including all earlier merges. */
static bool ms912x_batch_try_join(const struct ms912x_batch_group *a,
				  const struct ms912x_batch_group *b,
				  bool allow_extra,
				  struct ms912x_batch_group *joined,
				  u64 *separate_bytes, u64 *joined_bytes)
{
	const struct drm_rect *left = &a->bounds, *right = &b->bounds;
	int overlap_width = max(0, min(left->x2, right->x2) - max(left->x1, right->x1));
	int overlap_height = max(0, min(left->y2, right->y2) - max(left->y1, right->y1));
	u64 covered = (u64)drm_rect_width(left) * drm_rect_height(left) +
		(u64)drm_rect_width(right) * drm_rect_height(right) -
		(u64)overlap_width * overlap_height;
	u64 area;

	joined->bounds = ms912x_rect_bounds(left, right);
	area = (u64)drm_rect_width(&joined->bounds) *
		drm_rect_height(&joined->bounds);
	joined->extra_pixels = a->extra_pixels + b->extra_pixels + area - covered;
	if (joined->extra_pixels > area)
		return false;
	if (joined->extra_pixels > (allow_extra ?
	    ms912x_batch_extra_limit(area - joined->extra_pixels) : 0))
		return false;
	*separate_bytes = ms912x_rects_payload_bytes(left, 1) +
		ms912x_rects_payload_bytes(right, 1);
	*joined_bytes = ms912x_rects_payload_bytes(&joined->bounds, 1);
	return true;
}

/* Remove exact duplicates and rectangular unions before bounded local expansion. */
static void ms912x_rects_compact_exact(struct drm_rect *rects, unsigned int *count)
{
	unsigned int i, output = 0;

	for (i = 0; i < *count; i++) {
		struct ms912x_batch_group incoming = { .bounds = rects[i] };

		while (output) {
			struct ms912x_batch_group previous = { .bounds = rects[output - 1] };
			struct ms912x_batch_group joined;
			u64 separate, combined;

			if (!ms912x_batch_try_join(&previous, &incoming, false,
						  &joined, &separate, &combined))
				break;
			incoming = joined;
			output--;
		}
		rects[output++] = incoming.bounds;
	}
	*count = output;
}

/* Eight candidate groups bound work and stack use, with no per-frame allocation. */
void ms912x_rects_compact(struct drm_rect *rects, unsigned int *count,
			 u64 maximum_bytes)
{
	struct ms912x_batch_group groups[8];
	unsigned int i, active = 0, output = 0;

	if (*count < 2)
		return;
	sort(rects, *count, sizeof(*rects), ms912x_rect_compare, NULL);
	ms912x_rects_compact_exact(rects, count);
	for (i = 0; i < *count; i++) {
		struct ms912x_batch_group incoming = { .bounds = rects[i] };

		for (;;) {
			struct ms912x_batch_group best_joined;
			u64 best_separate = 0, best_combined = 0;
			unsigned int candidate, best = active;

			for (candidate = 0; candidate < active; candidate++) {
				struct ms912x_batch_group joined;
				u64 separate, combined;

				if (!ms912x_batch_try_join(&groups[candidate], &incoming,
						true,
						&joined, &separate, &combined))
					continue;
				/* Rank savings first, then prefer the smaller area expansion. */
				if (best == active || separate + best_combined >
				    best_separate + combined ||
				    (separate + best_combined == best_separate + combined &&
				     joined.extra_pixels < best_joined.extra_pixels)) {
					best = candidate;
					best_separate = separate;
					best_combined = combined;
					best_joined = joined;
				}
			}
			if (best == active)
				break;
			incoming = best_joined;
			groups[best] = groups[--active];
		}
		if (active == 8) {
			rects[output++] = groups[0].bounds;
			memmove(groups, groups + 1, 7 * sizeof(*groups));
			active--;
		}
		groups[active++] = incoming;
	}
	for (i = 0; i < active; i++)
		rects[output++] = groups[i].bounds;
	*count = output;
	ms912x_rects_limit_bytes(rects, count, maximum_bytes);
}

/* Allocation failures may enlarge coverage, but must never discard replay. */
void ms912x_batch_build(struct drm_rect *rects, unsigned int *count,
			unsigned int capacity,
			const struct drm_rect *current_rects, unsigned int current_count,
			const struct drm_rect *previous, unsigned int previous_count,
			u64 maximum_bytes)
{
	unsigned int i;

	*count = current_count;
	if (current_count > capacity) {
		struct drm_rect bounds = current_rects[0];

		for (i = 1; i < current_count; i++)
			bounds = ms912x_rect_bounds(&bounds, &current_rects[i]);
		rects[0] = bounds;
		*count = 1;
	} else if (current_count) {
		memcpy(rects, current_rects, current_count * sizeof(*rects));
	}
	if (!ms912x_mode3_append_rects(rects, capacity, count,
					 previous, previous_count)) {
		struct drm_rect bounds = previous[0];

		for (i = 0; i < *count; i++)
			bounds = ms912x_rect_bounds(&bounds, &rects[i]);
		for (i = 1; i < previous_count; i++)
			bounds = ms912x_rect_bounds(&bounds, &previous[i]);
		rects[0] = bounds;
		*count = 1;
	}
	ms912x_rects_compact(rects, count, maximum_bytes);
}

/* The cursor is composed only into the last rectangle in a USB batch. */
void ms912x_batch_include_overlay(struct drm_rect *rects, unsigned int *count,
				 unsigned int capacity,
				 const struct drm_rect *overlay, u64 maximum_bytes)
{
	unsigned int i;

	for (i = 0; i < *count; i++) {
		if (!drm_rect_equals(&rects[i], overlay))
			continue;
		memmove(&rects[i], &rects[i + 1],
			(*count - i - 1) * sizeof(*rects));
		rects[*count - 1] = *overlay;
		goto compact;
	}
	if (*count == capacity) {
		struct drm_rect bounds = *overlay;

		for (i = 0; i < *count; i++)
			bounds = ms912x_rect_bounds(&bounds, &rects[i]);
		rects[0] = bounds;
		*count = 1;
		return;
	}
	rects[(*count)++] = *overlay;
compact:
	/* Preserve the overlay order unless the packet exceeds its hard budget. */
	ms912x_rects_limit_bytes(rects, count, maximum_bytes);
}

bool ms912x_batch_backpressured(u64 pressure_ns, u64 low_watermark_ns,
			       u64 high_watermark_ns, bool was_backpressured)
{
	return was_backpressured ? pressure_ns > low_watermark_ns :
		pressure_ns >= high_watermark_ns;
}

u64 ms912x_batch_coalesce_ns(u64 bytes, u64 half_frame_ns, u64 pressure_ns,
			     u64 high_watermark_ns, bool backpressured,
			     bool force_full)
{
	if (!bytes || force_full || backpressured ||
	    (high_watermark_ns && pressure_ns >= high_watermark_ns))
		return 0;
	return half_frame_ns;
}
