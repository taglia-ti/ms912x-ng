/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MS912X_DAMAGE_POLICY_H
#define MS912X_DAMAGE_POLICY_H

#include <linux/types.h>

/* Floor requests to 0, 1, 3, 7, 15 or 31 without exceeding five ignored bits. */
static inline unsigned int
ms912x_damage_scan_tolerance_level(unsigned int tolerance)
{
	return (tolerance >= 1) + (tolerance >= 3) + (tolerance >= 7) +
		(tolerance >= 15) + (tolerance >= 31);
}

static inline unsigned int
ms912x_damage_scan_tolerance_bucket(unsigned int tolerance)
{
	return (1U << ms912x_damage_scan_tolerance_level(tolerance)) - 1;
}

static inline unsigned int
ms912x_damage_scan_auto_tolerance(u64 pressure_ns, u64 low_watermark_ns,
				  u64 high_watermark_ns,
				  unsigned int minimum,
				  unsigned int maximum)
{
	u64 delta, span, remainder = 0;
	unsigned int scaled = 0, i;
	unsigned int range;

	/* Interpolate bucket indices, keeping every pressure-selected value valid. */
	minimum = ms912x_damage_scan_tolerance_level(minimum);
	maximum = ms912x_damage_scan_tolerance_level(maximum);
	if (minimum >= maximum)
		return (1U << minimum) - 1;
	if (pressure_ns <= low_watermark_ns)
		return (1U << minimum) - 1;
	if (pressure_ns >= high_watermark_ns ||
	    high_watermark_ns <= low_watermark_ns)
		return (1U << maximum) - 1;

	delta = pressure_ns - low_watermark_ns;
	span = high_watermark_ns - low_watermark_ns;
	range = maximum - minimum;
	for (i = 0; i < range; i++) {
		if (remainder >= span - delta) {
			remainder -= span - delta;
			scaled++;
		} else {
			remainder += delta;
		}
	}
	return (1U << (minimum + scaled)) - 1;
}

#endif /* MS912X_DAMAGE_POLICY_H */
