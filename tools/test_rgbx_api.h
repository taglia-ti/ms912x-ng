/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MS912X_TEST_RGBX_API_H
#define MS912X_TEST_RGBX_API_H
#include "../shared/ms912x_rgbx_convert.h"

/* Use Android's prepare/execute contract; this helper performs no color arithmetic. */
static int test_convert_rgbx(uint8_t *dst, size_t bytes, size_t stride,
			     const struct ms912x_rgbx_view *source,
			     enum ms912x_color_backend backend)
{
	struct ms912x_rgbx_plan plan;
	int ret = ms912x_rgbx_prepare(&plan, source, backend);

	return ret ? ret : ms912x_rgbx_execute(&plan, dst, bytes, stride);
}
#endif
