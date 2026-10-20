/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MS912X_SCALE_H
#define MS912X_SCALE_H

#include <linux/kernel.h>
#include <linux/math64.h>
#include <linux/types.h>

#include "ms912x_scale_policy.h"

static inline void ms912x_scale_input_geometry(
		unsigned int output_width, unsigned int output_height,
		unsigned int scale, unsigned int *input_width,
		unsigned int *input_height)
{
	*input_width = ms912x_scale_dimension(output_width, scale);
	*input_height = ms912x_scale_dimension(output_height, scale);
	if (scale != MS912X_SCALE_100)
		*input_width = (*input_width + 3) & ~3U;
}

static inline int ms912x_scale_coordinate(int value, unsigned int scale)
{
	s64 numerator, denominator;

	if (scale == MS912X_SCALE_50) {
		numerator = value;
		denominator = 2;
	} else {
		numerator = (s64)value * 2;
		denominator = 3;
	}
	if (numerator < 0)
		return -(int)div_s64(-numerator + denominator - 1, denominator);
	return (int)div_s64(numerator, denominator);
}

#endif /* MS912X_SCALE_H */
