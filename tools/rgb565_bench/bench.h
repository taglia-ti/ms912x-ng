/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MS912X_RGB565_BENCH_H
#define MS912X_RGB565_BENCH_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
#define MS912X_MAX_WIDTH 1920
#define __packed __attribute__((packed))
#ifndef __always_inline
#define __always_inline inline __attribute__((always_inline))
#endif
struct drm_rect {
	int x1, y1, x2, y2;
};
static inline int drm_rect_width(const struct drm_rect *r)
{
	return r->x2 - r->x1;
}
static inline int drm_rect_height(const struct drm_rect *r)
{
	return r->y2 - r->y1;
}
static inline u16 cpu_to_be16(u16 v)
{
	return __builtin_bswap16(v);
}
static inline void put_unaligned_be24(u32 v, void *destination)
{
	u8 *p = destination;
	p[0] = v >> 16;
	p[1] = v >> 8;
	p[2] = v;
}
static inline void put_unaligned_le16(u16 v, void *destination)
{
	memcpy(destination, &v, sizeof(v));
}

#include "ms912x_mode3.h"
#include "production-layout.h"

static inline u32 le32_to_cpup(const __le32 *p)
{
	u32 v;
	memcpy(&v, p, sizeof(v));
	return v;
}
static inline void put_unaligned_le32(u32 v, void *p)
{
	memcpy(p, &v, sizeof(v));
}
static inline void put_unaligned_le64(u64 v, void *p)
{
	memcpy(p, &v, sizeof(v));
}

static inline u16 get_unaligned_le16(const void *p)
{
	u16 v;
	memcpy(&v, p, 2);
	return v;
}
static inline u32 get_unaligned_le32(const void *p)
{
	u32 v;
	memcpy(&v, p, 4);
	return v;
}
static inline u64 get_unaligned_le64(const void *p)
{
	u64 v;
	memcpy(&v, p, 8);
	return v;
}

typedef int (*rgb565_encode_fn)(void *, const void *, unsigned int,
				unsigned int, const struct drm_rect *, bool);
#define RGB565_PROTO(name)                                                     \
	int name(void *, const void *, unsigned int, unsigned int,             \
		 const struct drm_rect *, bool)
RGB565_PROTO(rgb565_scalar);
RGB565_PROTO(rgb565_nomask);
RGB565_PROTO(rgb565_scalar_row);
RGB565_PROTO(uyvy_scalar);
#if defined(__x86_64__)
RGB565_PROTO(rgb565_sse2);
RGB565_PROTO(rgb565_avx2);
RGB565_PROTO(uyvy_sse2);
RGB565_PROTO(uyvy_avx2);
#elif defined(__aarch64__) || defined(__arm__)
RGB565_PROTO(rgb565_neon);
RGB565_PROTO(uyvy_neon);
#endif

#endif
