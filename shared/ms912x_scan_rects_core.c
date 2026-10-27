// SPDX-License-Identifier: GPL-2.0-only

#include "../ms912x_scan_rects.h"
#include "../ms912x_rect_limits.h"

#ifdef __KERNEL__
#include <linux/slab.h>
#include <linux/vmalloc.h>
#endif

#ifdef __KERNEL__
typedef u16 ms912x_scan_u16;
#else
typedef uint16_t ms912x_scan_u16;
#endif

#define MS912X_SCAN_INDEX_NONE ((ms912x_scan_u16)~0U)

/* One bucket per bottom edge; nodes use the current rectangle-vector indices. */
struct ms912x_scan_rect_index {
	ms912x_scan_u16 heads[MS912X_SCAN_MAX_HEIGHT + 1];
	ms912x_scan_u16 next[MS912X_MAX_DAMAGE_RECTS];
	ms912x_scan_u16 prev[MS912X_MAX_DAMAGE_RECTS];
};

struct ms912x_scan_rect_index *ms912x_scan_rect_index_alloc(void)
{
#ifdef __KERNEL__
	return vzalloc(sizeof(struct ms912x_scan_rect_index));
#else
	return calloc(1, sizeof(struct ms912x_scan_rect_index));
#endif
}

void ms912x_scan_rect_index_free(struct ms912x_scan_rect_index *index)
{
#ifdef __KERNEL__
	vfree(index);
#else
	free(index);
#endif
}

void ms912x_scan_rect_index_reset(struct ms912x_scan_rect_index *index,
				 unsigned int height)
{
	memset(index->heads, 0xff, (height + 1) * sizeof(index->heads[0]));
}

static void ms912x_scan_rect_link(struct ms912x_scan_rect_index *index,
				 unsigned int node, unsigned int y)
{
	ms912x_scan_u16 first = index->heads[y];

	index->prev[node] = MS912X_SCAN_INDEX_NONE;
	index->next[node] = first;
	if (first != MS912X_SCAN_INDEX_NONE)
		index->prev[first] = node;
	index->heads[y] = node;
}

static void ms912x_scan_rect_unlink(struct ms912x_scan_rect_index *index,
				   unsigned int node, unsigned int y)
{
	ms912x_scan_u16 prev = index->prev[node], next = index->next[node];

	if (prev == MS912X_SCAN_INDEX_NONE)
		index->heads[y] = next;
	else
		index->next[prev] = next;
	if (next != MS912X_SCAN_INDEX_NONE)
		index->prev[next] = prev;
}

bool ms912x_scan_rect_add_with_growth(struct ms912x_scan_rect_index *index,
			 struct drm_rect **rects, unsigned int *capacity,
			 unsigned int *count, const struct drm_rect *rect,
			 unsigned int width, unsigned int height,
			 ms912x_scan_rect_grow_fn grow)
{
	struct drm_rect appended = *rect;
	struct drm_rect bounds = {
		.x1 = 0, .y1 = 0, .x2 = width, .y2 = height,
	};
	struct drm_rect *grown;
	unsigned int join_y = appended.y1, i;
	bool overflow = false;

	if (*count == 1 && drm_rect_equals(&(*rects)[0], &bounds))
		return false;
	while (true) {
		bool joined_layer = false;

		while (true) {
			unsigned int match = MS912X_SCAN_INDEX_NONE;

			/* Preserve the old vector's first-match and swap-delete order. */
			for (i = index->heads[join_y];
			     i != MS912X_SCAN_INDEX_NONE; i = index->next[i]) {
				const struct drm_rect *existing = &(*rects)[i];

				if (existing->x1 < appended.x2 &&
				    appended.x1 < existing->x2 && i < match) {
					match = i;
					if (!match)
						break;
				}
			}
			if (match == MS912X_SCAN_INDEX_NONE)
				break;
			appended.x1 = min(appended.x1, (*rects)[match].x1);
			appended.x2 = max(appended.x2, (*rects)[match].x2);
			appended.y1 = min(appended.y1, (*rects)[match].y1);
			ms912x_scan_rect_unlink(index, match, join_y);
			--(*count);
			if (match != *count) {
				ms912x_scan_rect_unlink(index, *count, (*rects)[*count].y2);
				(*rects)[match] = (*rects)[*count];
				ms912x_scan_rect_link(index, match, (*rects)[match].y2);
			}
			joined_layer = true;
		}
		if (!joined_layer)
			break;
		join_y = appended.y1;
	}
	if (drm_rect_equals(&appended, &bounds))
		goto collapse;
	if (*count == *capacity && grow) {
		grown = grow(*rects, capacity, *count + 1,
			     MS912X_MAX_DAMAGE_RECTS);
		if (grown)
			*rects = grown;
	}
	if (*count < *capacity) {
		(*rects)[*count] = appended;
		ms912x_scan_rect_link(index, *count, appended.y2);
		++(*count);
		return false;
	}
	for (i = 0; i < *count; i++) {
		appended.x1 = min(appended.x1, (*rects)[i].x1);
		appended.y1 = min(appended.y1, (*rects)[i].y1);
		appended.x2 = max(appended.x2, (*rects)[i].x2);
		appended.y2 = max(appended.y2, (*rects)[i].y2);
	}
	overflow = true;
collapse:
	(*rects)[0] = appended;
	*count = 1;
	ms912x_scan_rect_index_reset(index, height);
	ms912x_scan_rect_link(index, 0, appended.y2);
	return overflow;
}
