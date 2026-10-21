/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MS912X_PROFILE_POLICY_H
#define MS912X_PROFILE_POLICY_H

enum ms912x_damage_scan_mode {
	MS912X_DAMAGE_SCAN_AUTO,
	MS912X_DAMAGE_SCAN_OFF,
	MS912X_DAMAGE_SCAN_ON,
};

enum ms912x_profile {
	MS912X_PROFILE_AUTO,
	MS912X_PROFILE_EFFICIENCY,
	MS912X_PROFILE_LATENCY,
	MS912X_PROFILE_QUALITY,
};

struct ms912x_profile_values {
	enum ms912x_damage_scan_mode damage_scan;
	unsigned int stride;
	unsigned int tolerance_min;
	unsigned int tolerance_max;
};

static inline struct ms912x_profile_values
ms912x_profile_values_for(enum ms912x_profile profile)
{
	switch (profile) {
	case MS912X_PROFILE_LATENCY:
		return (struct ms912x_profile_values) {
			.damage_scan = MS912X_DAMAGE_SCAN_ON,
			.stride = 2,
			.tolerance_min = 3,
			.tolerance_max = 31,
		};
	case MS912X_PROFILE_QUALITY:
		return (struct ms912x_profile_values) {
			.damage_scan = MS912X_DAMAGE_SCAN_AUTO,
			.stride = 1,
			.tolerance_min = 0,
			.tolerance_max = 0,
		};
	case MS912X_PROFILE_EFFICIENCY:
	default:
		return (struct ms912x_profile_values) {
			.damage_scan = MS912X_DAMAGE_SCAN_OFF,
			.stride = 5,
			.tolerance_min = 0,
			.tolerance_max = 0,
		};
	}
}

static inline enum ms912x_profile
ms912x_profile_auto_select(unsigned int is_x86, unsigned int superspeed)
{
	if (!is_x86)
		return MS912X_PROFILE_EFFICIENCY;
	return superspeed ? MS912X_PROFILE_QUALITY : MS912X_PROFILE_LATENCY;
}

#endif
