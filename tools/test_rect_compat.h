/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef BATCH_TEST_COMPAT_H
#define BATCH_TEST_COMPAT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "ms912x_rect_limits.h"
typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;
struct drm_rect { int x1, y1, x2, y2; };
#define DRM_RECT_INIT(x, y, w, h) \
    ((struct drm_rect){(x), (y), (x) + (w), (y) + (h)})
#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))
#define min_t(t, a, b) min((t)(a), (t)(b))
#define max_t(t, a, b) max((t)(a), (t)(b))
static inline int drm_rect_width(const struct drm_rect *r) { return r->x2 - r->x1; }
static inline int drm_rect_height(const struct drm_rect *r) { return r->y2 - r->y1; }
static inline bool drm_rect_equals(const struct drm_rect *a, const struct drm_rect *b)
{ return a->x1 == b->x1 && a->x2 == b->x2 && a->y1 == b->y1 && a->y2 == b->y2; }
#ifdef MS912X_TEST_SORT_HOOK
void ms912x_test_sort_hook(void);
#endif
static inline void sort(void *base, size_t count, size_t size,
                        int (*compare)(const void *, const void *),
                        void (*swap)(void *, void *, int))
{
    (void)swap;
#ifdef MS912X_TEST_SORT_HOOK
    ms912x_test_sort_hook();
#endif
    qsort(base, count, size, compare);
}
#endif
