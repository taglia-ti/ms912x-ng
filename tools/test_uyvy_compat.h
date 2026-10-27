/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MS912X_UYVY_TEST_COMPAT_H
#define MS912X_UYVY_TEST_COMPAT_H
#include "simd_bench/compat.h"
#include "ms912x_neon.h"
#include "ms912x_rect_limits.h"
#include "shared/ms912x_scan_backend_types.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#define MS912X_MAX_HEIGHT 1200
#define MS912X_MAX_TRANSFER_LEN (MS912X_MAX_WIDTH * MS912X_MAX_HEIGHT * 2 + MS912X_MODE3_RECT_OVERHEAD)
#define ALIGN_DOWN(value, alignment) ((value) & ~((alignment) - 1))
#define WRITE_ONCE(value, next) ((value) = (next))
#define GFP_KERNEL 0
#define kvrealloc(old, n, flags) realloc((void *)(old), n)
#define ms912x_kvrealloc(old, old_size, n, flags) kvrealloc(old, n, flags)
#define kvzalloc(n, flags) calloc(1, n)
#define kvfree(ptr) free(ptr)
#define min_t(type, a, b) min((type)(a), (type)(b))
#define max_t(type, a, b) max((type)(a), (type)(b))
#define DRM_RECT_INIT(x, y, w, h) ((struct drm_rect){(x), (y), (x) + (w), (y) + (h)})
static inline bool drm_rect_equals(const struct drm_rect *a, const struct drm_rect *b)
{
	return a->x1 == b->x1 && a->y1 == b->y1 && a->x2 == b->x2 && a->y2 == b->y2;
}
static inline void sort(void *base, size_t n, size_t size,
			int (*compare)(const void *, const void *),
			void (*swap)(void *, void *, int))
{
	(void)swap;
	qsort(base, n, size, compare);
}
struct ms912x_frame_update_header {
	u16 marker;
	u8 position[3], dimensions[3];
} __attribute__((packed));
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
static inline u16 cpu_to_be16(u16 value)
{
	return __builtin_bswap16(value);
}
#else
static inline u16 cpu_to_be16(u16 value)
{
	return value;
}
#endif
static inline void put_unaligned_be24(u32 value, void *pointer)
{
	u8 *p = pointer;
	p[0] = value >> 16;
	p[1] = value >> 8;
	p[2] = value;
}
enum ms912x_fast_chroma_backend {
	MS912X_FAST_CHROMA_SCALAR,
	MS912X_FAST_CHROMA_AUTO,
	MS912X_FAST_CHROMA_AVX2,
	MS912X_FAST_CHROMA_NEON,
	MS912X_FAST_CHROMA_SSE2,
	MS912X_FAST_CHROMA_SSSE3,
};
struct ms912x_parameters_fixture {
	enum ms912x_fast_chroma_backend fast_chroma;
};
extern struct ms912x_parameters_fixture ms912x_parameters;
#include "ms912x_cursor.h"
#define DRM_FORMAT_XRGB8888 0x34325258U
#define DRM_FORMAT_XBGR8888 0x34324258U
int ms912x_pack_uyvy_batch(void *, const u32 *, unsigned, unsigned, const struct drm_rect *,
			   unsigned);
struct drm_rect *ms912x_rect_vector_grow(struct drm_rect *, unsigned *, unsigned, unsigned);
#endif
