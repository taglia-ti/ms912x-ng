/* SPDX-License-Identifier: GPL-2.0-only */
#include "ms912x_rgbx_internal.h"

#include <errno.h>

void ms912x_rgbx_row_scalar(uint8_t *dst, const uint8_t *src,
			   uint32_t width, size_t step)
{
	ms912x_rgbx_scalar_tail(dst, src, width, step, 0);
}

void ms912x_bgrx_row_scalar(uint8_t *dst, const uint8_t *src,
			   uint32_t width, size_t step)
{
	ms912x_rgbx_scalar_tail(dst, src, width, step, 2);
}

enum ms912x_color_backend ms912x_rgbx_default_backend(void)
{
	if (ms912x_rgbx_select_neon(MS912X_RGBX))
		return MS912X_COLOR_NEON;
	if (ms912x_rgbx_select_sse2(MS912X_RGBX))
		return MS912X_COLOR_SSE2;
	return MS912X_COLOR_SCALAR;
}

int ms912x_rgbx_backend_supported(enum ms912x_color_backend backend)
{
	switch (backend) {
	case MS912X_COLOR_AUTO:
	case MS912X_COLOR_SCALAR:
		return 1;
	case MS912X_COLOR_SSE2:
		return ms912x_rgbx_select_sse2(MS912X_RGBX) != NULL;
	case MS912X_COLOR_NEON:
		return ms912x_rgbx_select_neon(MS912X_RGBX) != NULL;
	default:
		return 0;
	}
}

const char *ms912x_rgbx_backend_name(enum ms912x_color_backend backend)
{
	switch (backend) {
	case MS912X_COLOR_AUTO: return "auto";
	case MS912X_COLOR_SCALAR: return "scalar";
	case MS912X_COLOR_SSE2: return "sse2";
	case MS912X_COLOR_NEON: return "neon";
	default: return "unsupported";
	}
}

static ms912x_rgbx_row_fn select_row(enum ms912x_color_backend backend,
				    enum ms912x_rgbx_order order)
{
	switch (backend) {
	case MS912X_COLOR_SSE2:
		return ms912x_rgbx_select_sse2(order);
	case MS912X_COLOR_NEON:
		return ms912x_rgbx_select_neon(order);
	default:
		return order == MS912X_RGBX ? ms912x_rgbx_row_scalar : ms912x_bgrx_row_scalar;
	}
}

int ms912x_rgbx_prepare(struct ms912x_rgbx_plan *plan,
			const struct ms912x_rgbx_view *src,
			enum ms912x_color_backend backend)
{
	size_t last_x, row_extent, start, row_offset, pixel_offset, span, end;
	struct ms912x_rgbx_plan prepared;

	if (!plan || !src || !src->data || !src->width || !src->height ||
	    src->width > INT32_MAX || src->height > INT32_MAX ||
	    (src->width & 1U) || src->pixel_stride < 4U || !src->row_stride ||
	    (src->order != MS912X_RGBX && src->order != MS912X_BGRX))
		return -EINVAL;
	if (!ms912x_rgbx_backend_supported(backend))
		return -ENOTSUP;
	if (__builtin_add_overflow((size_t)src->crop_x, (size_t)src->width - 1U, &last_x) ||
	    __builtin_mul_overflow((size_t)src->crop_y, src->row_stride, &row_offset) ||
	    __builtin_mul_overflow((size_t)src->crop_x, src->pixel_stride, &pixel_offset) ||
	    __builtin_add_overflow(src->offset, row_offset, &start) ||
	    __builtin_add_overflow(start, pixel_offset, &start) ||
	    __builtin_mul_overflow((size_t)src->height - 1U, src->row_stride, &span) ||
	    __builtin_mul_overflow((size_t)src->width - 1U, src->pixel_stride, &row_extent))
		return -EOVERFLOW;
	/* Validate the crop against the physical row independently of total capacity. */
	if (last_x > (SIZE_MAX - 4U) / src->pixel_stride ||
	    last_x * src->pixel_stride + 4U > src->row_stride)
		return -EINVAL;
	if (__builtin_add_overflow(row_extent, (size_t)4, &row_extent) ||
	    __builtin_add_overflow(span, row_extent, &span) ||
	    __builtin_add_overflow(start, span, &end))
		return -EOVERFLOW;
	if (end > src->bytes)
		return -EINVAL;
	if ((uintptr_t)src->data > UINTPTR_MAX - end)
		return -EOVERFLOW;
	if (backend == MS912X_COLOR_AUTO)
		backend = ms912x_rgbx_default_backend();
	if (src->pixel_stride != 4U)
		backend = MS912X_COLOR_SCALAR;
	prepared = (struct ms912x_rgbx_plan) {
		.first = src->data + start, .span = span,
		.row_stride = src->row_stride, .pixel_stride = src->pixel_stride,
		.width = src->width, .height = src->height,
		.backend = backend, .row = select_row(backend, src->order),
	};
	*plan = prepared;
	return 0;
}

int ms912x_rgbx_execute(const struct ms912x_rgbx_plan *plan,
			uint8_t *dst, size_t dst_bytes, size_t dst_stride)
{
	size_t row_bytes, span;
	uintptr_t first, out;
	uint32_t y;

	if (!plan || !plan->row || !plan->first || !plan->width || !plan->height || !dst)
		return -EINVAL;
	if (__builtin_mul_overflow((size_t)plan->width, (size_t)2, &row_bytes) ||
	    __builtin_mul_overflow((size_t)plan->height - 1U, dst_stride, &span) ||
	    __builtin_add_overflow(span, row_bytes, &span))
		return -EOVERFLOW;
	if (dst_stride < row_bytes || dst_bytes < span)
		return -EINVAL;
	first = (uintptr_t)plan->first;
	out = (uintptr_t)dst;
	if (out > UINTPTR_MAX - span)
		return -EOVERFLOW;
	if (out < first + plan->span && first < out + span)
		return -EINVAL;
	for (y = 0; y < plan->height; ++y)
		plan->row(dst + (size_t)y * dst_stride,
			  plan->first + (size_t)y * plan->row_stride,
			  plan->width, plan->pixel_stride);
	return 0;
}

int ms912x_rgbx_to_uyvy(uint8_t *dst, size_t dst_bytes, size_t dst_stride,
			const struct ms912x_rgbx_view *source,
			enum ms912x_color_backend backend)
{
	struct ms912x_rgbx_plan plan;
	int result = ms912x_rgbx_prepare(&plan, source, backend);

	return result ? result : ms912x_rgbx_execute(&plan, dst, dst_bytes, dst_stride);
}
