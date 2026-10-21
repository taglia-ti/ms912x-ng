/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MS912X_CURSOR_TEST_COMPAT_H
#define MS912X_CURSOR_TEST_COMPAT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;
typedef uint32_t __le32;

#ifndef __always_inline
#define __always_inline inline __attribute__((always_inline))
#endif
#define MS912X_CURSOR_MAX_WIDTH 128
#define MS912X_CURSOR_MAX_HEIGHT 128

struct drm_rect {
	int x1;
	int y1;
	int x2;
	int y2;
};

struct ms912x_frame_update_header {
	u8 bytes[8];
} __attribute__((packed));

#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))
#define min_t(type, a, b) min((type)(a), (type)(b))
#define max_t(type, a, b) max((type)(a), (type)(b))
#define clamp_t(type, value, low, high) \
	min_t(type, max_t(type, value, low), high)

static inline int drm_rect_width(const struct drm_rect *rect)
{
	return rect->x2 - rect->x1;
}

static inline int drm_rect_height(const struct drm_rect *rect)
{
	return rect->y2 - rect->y1;
}

static inline uint32_t le32_to_cpup(const __le32 *value)
{
	return *value;
}

#endif
