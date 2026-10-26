/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MS912X_SCALE_POLICY_H
#define MS912X_SCALE_POLICY_H

/* Per-adapter sysfs scale values. */
#define MS912X_SCALE_AUTO 0U
#define MS912X_SCALE_50 50U
#define MS912X_SCALE_67 67U
#define MS912X_SCALE_80 80U
#define MS912X_SCALE_100 100U

/* Resolved geometry modes; AUTO is a setting, never a resolved mode. */
enum ms912x_scale_mode {
	MS912X_SCALE_MODE_NATIVE = 0,
	MS912X_SCALE_MODE_DOWNSCALE_2_TO_1,
	MS912X_SCALE_MODE_DOWNSCALE_3_TO_2,
	MS912X_SCALE_MODE_DOWNSCALE_5_TO_4,
};

static inline enum ms912x_scale_mode
ms912x_scale_mode_from_setting(unsigned int setting)
{
	switch (setting) {
	case MS912X_SCALE_50:
		return MS912X_SCALE_MODE_DOWNSCALE_2_TO_1;
	case MS912X_SCALE_67:
		return MS912X_SCALE_MODE_DOWNSCALE_3_TO_2;
	case MS912X_SCALE_80:
		return MS912X_SCALE_MODE_DOWNSCALE_5_TO_4;
	default:
		return MS912X_SCALE_MODE_NATIVE;
	}
}

static inline unsigned int
ms912x_scale_setting_for_mode(enum ms912x_scale_mode mode)
{
	switch (mode) {
	case MS912X_SCALE_MODE_DOWNSCALE_2_TO_1:
		return MS912X_SCALE_50;
	case MS912X_SCALE_MODE_DOWNSCALE_3_TO_2:
		return MS912X_SCALE_67;
	case MS912X_SCALE_MODE_DOWNSCALE_5_TO_4:
		return MS912X_SCALE_80;
	default:
		return MS912X_SCALE_100;
	}
}

static inline const char *
ms912x_scale_mode_name(enum ms912x_scale_mode mode)
{
	switch (mode) {
	case MS912X_SCALE_MODE_DOWNSCALE_2_TO_1:
		return "2:1 downscale";
	case MS912X_SCALE_MODE_DOWNSCALE_3_TO_2:
		return "3:2 downscale";
	case MS912X_SCALE_MODE_DOWNSCALE_5_TO_4:
		return "5:4 downscale";
	default:
		return "native";
	}
}

#define MS912X_SCALE_AUTO_USB_BYTES_PER_SEC (26ULL * 1000 * 1000)
#define MS912X_SCALE_AUTO_BYTES_PER_PIXEL 2U
/* Keep automatic VIDEO_IN below the observed scale-path width boundary. */
#define MS912X_SCALE_AUTO_MAX_INPUT_WIDTH 1280U

static inline unsigned int ms912x_scale_dimension(unsigned int dimension,
						  enum ms912x_scale_mode mode)
{
	switch (mode) {
	case MS912X_SCALE_MODE_DOWNSCALE_2_TO_1:
		return (dimension + 1U) / 2U;
	case MS912X_SCALE_MODE_DOWNSCALE_3_TO_2:
		/* Three source pixels map to two output pixels. */
		return (unsigned int)(((unsigned long long)dimension * 2 + 2) / 3);
	case MS912X_SCALE_MODE_DOWNSCALE_5_TO_4:
		return (unsigned int)(((unsigned long long)dimension * 4 + 4) / 5);
	default:
		return dimension;
	}
}

static inline enum ms912x_scale_mode
ms912x_scale_auto_select(unsigned int output_width,
			 unsigned int output_height,
			 unsigned int refresh_hz,
			 unsigned int link_below_usb3)
{
	static const enum ms912x_scale_mode modes[] = {
		MS912X_SCALE_MODE_NATIVE,
		MS912X_SCALE_MODE_DOWNSCALE_5_TO_4,
		MS912X_SCALE_MODE_DOWNSCALE_3_TO_2,
		MS912X_SCALE_MODE_DOWNSCALE_2_TO_1,
	};
	unsigned int i;

	if (!link_below_usb3)
		return MS912X_SCALE_MODE_NATIVE;
	if (!refresh_hz)
		refresh_hz = 60;

	for (i = 0; i < sizeof(modes) / sizeof(modes[0]); i++) {
		enum ms912x_scale_mode mode = modes[i];
		unsigned int input_width =
			ms912x_scale_dimension(output_width, mode);
		unsigned int input_height =
			ms912x_scale_dimension(output_height, mode);
		unsigned long long frame_bytes;

		if (mode != MS912X_SCALE_MODE_NATIVE)
			input_width = (input_width + 3U) & ~3U;
		if (input_width > MS912X_SCALE_AUTO_MAX_INPUT_WIDTH)
			continue;
		frame_bytes = (unsigned long long)input_width * input_height *
			MS912X_SCALE_AUTO_BYTES_PER_PIXEL;
		if (frame_bytes * refresh_hz <=
		    4ULL * MS912X_SCALE_AUTO_USB_BYTES_PER_SEC)
			return mode;
	}

	return MS912X_SCALE_MODE_DOWNSCALE_2_TO_1;
}

#endif
