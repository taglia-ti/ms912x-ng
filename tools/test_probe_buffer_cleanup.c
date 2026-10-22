// SPDX-License-Identifier: GPL-2.0-only

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

struct drm_device { unsigned int unused; };
struct ms912x_scan_rect_index { unsigned int unused; };
struct ms912x_scan_frame { unsigned int *pixels; };
struct ms912x_device {
	struct drm_device drm;
	void *scan_rect_index;
	void *payload_compare_rects;
	void *scan_damage_rects;
	void *submit_damage_rects;
	void *pending_rects;
	void *previous_damage_rects;
	unsigned int *damage_scan_snapshot;
	unsigned int *damage_scan_snapshot_back;
	unsigned int *rgb565_scale_scratch;
	unsigned int *conversion_pixels;
	unsigned int *latest_pixels;
};

static unsigned int freed;
static void tracked_free(void *ptr)
{
	if (!ptr)
		return;
	freed++;
	free(ptr);
}
static void kvfree(void *ptr) { tracked_free(ptr); }
static void vfree(void *ptr) { tracked_free(ptr); }
static void ms912x_scan_rect_index_free(struct ms912x_scan_rect_index *index)
{
	tracked_free(index);
}
static void *resource(void) { return malloc(8); }

#include "probe-buffer-cleanup.h"

static void partial_rect_vector_unwind_is_per_adapter(unsigned int count)
{
	struct ms912x_device first = {}, second = {};
	unsigned int i;
	void **first_slots[] = {
		&first.scan_rect_index,
		&first.scan_damage_rects,
		&first.submit_damage_rects,
		&first.pending_rects,
		&first.previous_damage_rects,
		&first.payload_compare_rects,
	};
	void **second_slots[] = {
		&second.scan_rect_index,
		&second.scan_damage_rects,
		&second.submit_damage_rects,
		&second.pending_rects,
		&second.previous_damage_rects,
		&second.payload_compare_rects,
	};

	for (i = 0; i < count; i++) {
		*first_slots[i] = resource();
		assert(*first_slots[i]);
	}
	for (i = 0; i < sizeof(second_slots) / sizeof(second_slots[0]); i++) {
		*second_slots[i] = resource();
		assert(*second_slots[i]);
	}

	freed = 0;
	ms912x_free_rect_vectors(&first.drm, &first);
	assert(freed == count);
	for (i = 0; i < sizeof(second_slots) / sizeof(second_slots[0]); i++)
		assert(*second_slots[i]);
	ms912x_free_rect_vectors(&second.drm, &second);
	assert(freed == count + sizeof(second_slots) / sizeof(second_slots[0]));
}

static void complete_snapshots_unwind_only_their_owner(void)
{
	struct ms912x_device first = {}, second = {};

	first.damage_scan_snapshot = resource();
	first.damage_scan_snapshot_back = resource();
	first.rgb565_scale_scratch = resource();
	second.damage_scan_snapshot = resource();
	second.damage_scan_snapshot_back = resource();
	second.rgb565_scale_scratch = resource();
	first.latest_pixels = resource();
	first.conversion_pixels = resource();
	second.latest_pixels = resource();
	second.conversion_pixels = resource();
	assert(first.damage_scan_snapshot && first.damage_scan_snapshot_back &&
	       first.rgb565_scale_scratch && second.damage_scan_snapshot &&
	       second.damage_scan_snapshot_back && second.rgb565_scale_scratch &&
	       first.latest_pixels && first.conversion_pixels && second.latest_pixels &&
	       second.conversion_pixels);

	freed = 0;
	ms912x_free_damage_scan_snapshot(&first.drm, &first);
	assert(freed == 3 && second.damage_scan_snapshot &&
	       second.damage_scan_snapshot_back && second.rgb565_scale_scratch);
	ms912x_free_conversion_snapshots(&second.drm, &second);
	assert(freed == 5 && first.latest_pixels && first.conversion_pixels);
	ms912x_free_conversion_snapshots(&first.drm, &first);
	assert(freed == 7);
	ms912x_free_damage_scan_snapshot(&second.drm, &second);
	assert(freed == 10);
}

int main(void)
{
	unsigned int allocated;

	for (allocated = 0; allocated <= 6; allocated++)
		partial_rect_vector_unwind_is_per_adapter(allocated);
	complete_snapshots_unwind_only_their_owner();
	puts("PASS probe buffer cleanup: partial vectors and snapshots stay per-adapter");
	return 0;
}
