/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MS912X_OVERSCALE_H
#define MS912X_OVERSCALE_H

#include <linux/types.h>

enum ms912x_overscale_mode {
	MS912X_OVERSCALE_AUTO,
	MS912X_OVERSCALE_OFF,
	MS912X_OVERSCALE_ON,
};

static inline bool ms912x_overscale_should_enable(
		enum ms912x_overscale_mode mode, bool usb2_link,
		unsigned int width, unsigned int height)
{
	bool above_4_3 = width > 1024 && height > 768 &&
		(u64)width * 3 == (u64)height * 4;
	bool above_16_9 = width > 1366 && height > 768 &&
		(u64)width * 9 == (u64)height * 16;

	return mode == MS912X_OVERSCALE_ON ||
		(mode == MS912X_OVERSCALE_AUTO && usb2_link &&
		 (above_4_3 || above_16_9));
}

static inline void ms912x_overscale_input_geometry(
		unsigned int output_width, unsigned int output_height,
		bool enabled, unsigned int *input_width,
		unsigned int *input_height)
{
	if (enabled) {
		unsigned int scaled_width = DIV_ROUND_UP(output_width * 2, 3);

		*input_width = (scaled_width + 3) & ~3U;
		*input_height = DIV_ROUND_UP(output_height * 2, 3);
	} else {
		*input_width = output_width;
		*input_height = output_height;
	}
}

#endif /* MS912X_OVERSCALE_H */
