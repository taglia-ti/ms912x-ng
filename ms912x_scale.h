/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MS912X_SCALE_H
#define MS912X_SCALE_H

#include <linux/kernel.h>
#include <linux/math64.h>
#include <linux/types.h>

#include "ms912x_scale_policy.h"

static inline void ms912x_scale_input_geometry(
		unsigned int output_width, unsigned int output_height,
		enum ms912x_scale_mode mode, unsigned int *input_width,
		unsigned int *input_height)
{
	*input_width = ms912x_scale_dimension(output_width, mode);
	*input_height = ms912x_scale_dimension(output_height, mode);
	if (mode != MS912X_SCALE_MODE_NATIVE)
		*input_width = (*input_width + 3) & ~3U;
}

static inline int ms912x_scale_coordinate(int value,
					  enum ms912x_scale_mode mode)
{
	s64 numerator, denominator;

	switch (mode) {
	case MS912X_SCALE_MODE_DOWNSCALE_2_TO_1:
		numerator = value;
		denominator = 2;
		break;
	case MS912X_SCALE_MODE_DOWNSCALE_3_TO_2:
		numerator = (s64)value * 2;
		denominator = 3;
		break;
	default:
		return value;
	}
	if (numerator < 0)
		return -(int)div_s64(-numerator + denominator - 1, denominator);
	return (int)div_s64(numerator, denominator);
}

#endif /* MS912X_SCALE_H */
