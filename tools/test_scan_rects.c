// SPDX-License-Identifier: GPL-2.0-only
/* Frozen pre-index builder: compare the exact vector after every insertion. */
#include <assert.h>
#include <stdio.h>
#include <time.h>
#include "ms912x_scan_rects.h"

static bool fail_growth;
struct drm_rect *ms912x_rect_vector_grow(struct drm_rect *p, unsigned int *cap,
                                       unsigned int required, unsigned int limit)
{
	unsigned int n = min(limit, max(required, *cap * 2));
	if (required <= *cap)
		return p;
	if (fail_growth || required > limit)
		return NULL;
	p = realloc(p, n * sizeof(*p));
	assert(p);
	*cap = n;
	return p;
}
struct ms912x_scan_frame_stats { unsigned int rect_overflows; };
static void reference_add(struct drm_rect **rects,
					unsigned int *capacity,
					unsigned int *count,
					const struct drm_rect *rect,
					struct ms912x_scan_frame_stats *frame_stats,
					unsigned int width,
					unsigned int height)
{
	struct drm_rect appended = *rect;
	struct drm_rect bounds = DRM_RECT_INIT(0, 0, width, height);
	struct drm_rect *grown;
	unsigned int join_y = appended.y1, i;

	if (*count == 1 && drm_rect_equals(&(*rects)[0], &bounds))
		return;
	while (true) {
		bool joined_layer = false;

		while (true) {
			bool joined = false;

			for (i = 0; i < *count; i++) {
				struct drm_rect *existing = &(*rects)[i];

				if (existing->y2 != join_y ||
				    existing->x1 >= appended.x2 ||
				    appended.x1 >= existing->x2)
					continue;

				appended.x1 = min(appended.x1, existing->x1);
				appended.x2 = max(appended.x2, existing->x2);
				appended.y1 = min(appended.y1, existing->y1);
				*existing = (*rects)[--(*count)];
				joined = true;
				joined_layer = true;
				break;
			}
			if (!joined)
				break;
		}
		if (!joined_layer)
			break;
		join_y = appended.y1;
	}
	if (drm_rect_equals(&appended, &bounds)) {
		(*rects)[0] = bounds;
		*count = 1;
		return;
	}
	if (*count == *capacity) {
		grown = ms912x_rect_vector_grow(*rects, capacity, *count + 1,
						MS912X_MAX_DAMAGE_RECTS);
		if (grown)
			*rects = grown;
	}
	if (*count < *capacity) {
		(*rects)[(*count)++] = appended;
		return;
	}
	for (i = 0; i < *count; i++) {
		appended.x1 = min(appended.x1, (*rects)[i].x1);
		appended.y1 = min(appended.y1, (*rects)[i].y1);
		appended.x2 = max(appended.x2, (*rects)[i].x2);
		appended.y2 = max(appended.y2, (*rects)[i].y2);
	}
	(*rects)[0] = appended;
	*count = 1;
	if (frame_stats)
		frame_stats->rect_overflows++;
}


static unsigned int rng = 0x912c, checks;
static unsigned int random_next(void)
{
	rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
	return rng;
}
static void compare_stream(unsigned int pattern, bool fail)
{
	struct ms912x_scan_rect_index *index = ms912x_scan_rect_index_alloc();
	unsigned int ac = 1, bc = 1, an = 0, bn = 0;
	struct drm_rect *a = calloc(ac, sizeof(*a)), *b = calloc(bc, sizeof(*b));
	struct ms912x_scan_frame_stats stats = {0};
	unsigned int overflows = 0;

	assert(index && a && b);
	fail_growth = fail;
	ms912x_scan_rect_index_reset(index, MS912X_MAX_HEIGHT);
	for (unsigned int i = 0; i < 3500; i++) {
		unsigned int x = random_next() % (MS912X_MAX_WIDTH - 64);
		unsigned int y = random_next() % (MS912X_MAX_HEIGHT - 16);
		unsigned int w = 1 + random_next() % 64, h = 1 + random_next() % 16;
		if (pattern == 0) { y = i / 100; x = (i % 100) * 16; w = 8; h = 1; }
		if (pattern == 1) { y = i % 100; x = (i % 23) * 64; w = 64; h = 1; }
		if (pattern == 2) { y = i % 200; x = (i % 19) * 80; w = 128; h = 1; }
		struct drm_rect rect = DRM_RECT_INIT(x, y, w, h);
		if (i == 3498) rect = DRM_RECT_INIT(0, 0, MS912X_MAX_WIDTH, MS912X_MAX_HEIGHT);
		reference_add(&a, &ac, &an, &rect, &stats, MS912X_MAX_WIDTH, MS912X_MAX_HEIGHT);
		overflows += ms912x_scan_rect_add(index, &b, &bc, &bn, &rect,
						 MS912X_MAX_WIDTH, MS912X_MAX_HEIGHT);
		assert(an == bn && ac == bc && stats.rect_overflows == overflows);
		assert(!memcmp(a, b, an * sizeof(*a)));
		checks++;
	}
	free(a); free(b); ms912x_scan_rect_index_free(index);
}
static double benchmark(bool indexed)
{
	struct ms912x_scan_rect_index *index = ms912x_scan_rect_index_alloc();
	unsigned int cap = MS912X_MAX_DAMAGE_RECTS, count;
	struct drm_rect *rects = calloc(cap, sizeof(*rects));
	struct ms912x_scan_frame_stats stats = {0};
	struct timespec start, end;
	fail_growth = false;
	clock_gettime(CLOCK_MONOTONIC, &start);
	for (unsigned int repeat = 0; repeat < 100; repeat++) {
		count = 0;
		ms912x_scan_rect_index_reset(index, MS912X_MAX_HEIGHT);
		for (unsigned int i = 0; i < 2000; i++) {
			struct drm_rect rect = DRM_RECT_INIT((i % 100) * 16, (i / 100) * 2, 8, 1);
			if (indexed)
				ms912x_scan_rect_add(index, &rects, &cap, &count, &rect,
						     MS912X_MAX_WIDTH, MS912X_MAX_HEIGHT);
			else
				reference_add(&rects, &cap, &count, &rect, &stats,
					      MS912X_MAX_WIDTH, MS912X_MAX_HEIGHT);
		}
		assert(count == 2000);
	}
	clock_gettime(CLOCK_MONOTONIC, &end);
	free(rects); ms912x_scan_rect_index_free(index);
	return ((end.tv_sec - start.tv_sec) * 1e6 + (end.tv_nsec - start.tv_nsec) / 1e3) / 100;
}
int main(int argc, char **argv)
{
	for (unsigned int i = 0; i < 64; i++) {
		compare_stream(i % 4, false);
		compare_stream(i % 4, true);
	}
	printf("PASS scan rectangle index: %u exact vector/overflow checks\n", checks);
	if (argc > 1 && !strcmp(argv[1], "--benchmark"))
		printf("Fragmented builder fixture (2000 disjoint rectangles): old %.2f us, indexed %.2f us\n",
		       benchmark(false), benchmark(true));
	return 0;
}
