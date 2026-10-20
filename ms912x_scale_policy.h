/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MS912X_SCALE_POLICY_H
#define MS912X_SCALE_POLICY_H

#define MS912X_SCALE_AUTO 0U
#define MS912X_SCALE_50 50U
#define MS912X_SCALE_75 75U
#define MS912X_SCALE_100 100U
#define MS912X_SCALE_AUTO_USB_BYTES_PER_SEC (26ULL * 1000 * 1000)
#define MS912X_SCALE_AUTO_BYTES_PER_PIXEL 2U

static inline unsigned int ms912x_scale_dimension(unsigned int dimension,
						  unsigned int scale)
{
	switch (scale) {
	case MS912X_SCALE_50:
		return (dimension + 1U) / 2U;
	case MS912X_SCALE_75:
		return (unsigned int)(((unsigned long long)dimension * 2 + 2) / 3);
	default:
		return dimension;
	}
}

static inline unsigned int
ms912x_scale_auto_select(unsigned int output_width,
			 unsigned int output_height,
			 unsigned int refresh_hz,
			 unsigned int link_below_usb3)
{
	static const unsigned int scales[] = {
		MS912X_SCALE_100,
		MS912X_SCALE_75,
		MS912X_SCALE_50,
	};
	unsigned int i;

	if (!link_below_usb3)
		return MS912X_SCALE_100;
	if (!refresh_hz)
		refresh_hz = 60;

	for (i = 0; i < sizeof(scales) / sizeof(scales[0]); i++) {
		unsigned int scale = scales[i];
		unsigned int input_width =
			ms912x_scale_dimension(output_width, scale);
		unsigned int input_height =
			ms912x_scale_dimension(output_height, scale);
		unsigned long long frame_bytes;

		if (scale != MS912X_SCALE_100)
			input_width = (input_width + 3U) & ~3U;
		frame_bytes = (unsigned long long)input_width * input_height *
			MS912X_SCALE_AUTO_BYTES_PER_PIXEL;
		if (frame_bytes * refresh_hz <=
		    4ULL * MS912X_SCALE_AUTO_USB_BYTES_PER_SEC)
			return scale;
	}

	return MS912X_SCALE_50;
}

#endif