/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MS912X_RGB565_BENCH_H
#define MS912X_RGB565_BENCH_H

#include "../simd_bench/compat.h"
#include <errno.h>
#include "production-enums.h"
#define __packed __attribute__((packed))
#include "production-limits.h"
#include "production-api.h"
#define min_t(type, a, b) min((type)(a), (type)(b))
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
#include "ms912x_mode3.h"
#include "production-layout.h"

typedef int (*rgb565_encode_fn)(void *, const void *, unsigned int,
				unsigned int, const struct drm_rect *, bool);
#define RGB565_PROTO(name)                                                     \
	int name(void *, const void *, unsigned int, unsigned int,             \
		 const struct drm_rect *, bool)
RGB565_PROTO(bench_encode_rgb565);
RGB565_PROTO(bench_encode_uyvy);
void bench_configure(bool simd, enum ms912x_fast_chroma_backend chroma);
int bench_encode_batch(bool rgb565, void *dst, const void *src,
		       unsigned int width, unsigned int height,
		       const struct drm_rect *rects, unsigned int count);

#endif
