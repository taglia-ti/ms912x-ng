/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MS912X_RGBX_CONVERT_H
#define MS912X_RGBX_CONVERT_H

#include <stddef.h>
#include <stdint.h>

/* Names describe byte order in memory; alpha/X is ignored. */
enum ms912x_rgbx_order {
	MS912X_RGBX,
	MS912X_BGRX,
};

enum ms912x_color_backend {
	MS912X_COLOR_AUTO,
	MS912X_COLOR_SCALAR,
	MS912X_COLOR_SSE2,
	MS912X_COLOR_NEON,
	MS912X_COLOR_SSSE3,
	MS912X_COLOR_AVX2,
};

struct ms912x_rgbx_view {
	const uint8_t *data;
	size_t bytes;
	size_t offset;
	size_t row_stride;
	size_t pixel_stride;
	uint32_t crop_x;
	uint32_t crop_y;
	uint32_t width;
	uint32_t height;
	enum ms912x_rgbx_order order;
};

typedef void (*ms912x_rgbx_row_fn)(uint8_t *dst, const uint8_t *src,
				  uint32_t width, size_t pixel_stride);

/* Initialize only with prepare(); the caller keeps the source alive until execute(). */
struct ms912x_rgbx_plan {
	const uint8_t *first;
	size_t row_stride;
	size_t pixel_stride;
	size_t span;
	uint32_t width;
	uint32_t height;
	enum ms912x_color_backend backend;
	ms912x_rgbx_row_fn row;
};

enum ms912x_color_backend ms912x_rgbx_default_backend(void);
int ms912x_rgbx_backend_supported(enum ms912x_color_backend backend);
const char *ms912x_rgbx_backend_name(enum ms912x_color_backend backend);

/* Validate once and choose a row worker; unusual pixel strides use scalar code. */
int ms912x_rgbx_prepare(struct ms912x_rgbx_plan *plan,
			const struct ms912x_rgbx_view *source,
			enum ms912x_color_backend backend);
/* Validate the destination before writing. Source and destination must not overlap. */
int ms912x_rgbx_execute(const struct ms912x_rgbx_plan *plan,
			uint8_t *dst, size_t dst_bytes, size_t dst_stride);
int ms912x_rgbx_to_uyvy(uint8_t *dst, size_t dst_bytes, size_t dst_stride,
			const struct ms912x_rgbx_view *source,
			enum ms912x_color_backend backend);

#endif
