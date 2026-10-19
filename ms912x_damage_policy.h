/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MS912X_DAMAGE_POLICY_H
#define MS912X_DAMAGE_POLICY_H

#include <linux/types.h>

static inline unsigned int
ms912x_damage_scan_auto_tolerance(u64 pressure_ns, u64 low_watermark_ns,
				  u64 high_watermark_ns,
				  unsigned int minimum,
				  unsigned int maximum)
{
	u64 delta, span, remainder = 0;
	unsigned int scaled = 0, i;
	unsigned int range;

	if (minimum >= maximum)
		return minimum;
	if (pressure_ns <= low_watermark_ns)
		return minimum;
	if (pressure_ns >= high_watermark_ns ||
	    high_watermark_ns <= low_watermark_ns)
		return maximum;

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
	return minimum + scaled;
}

#endif /* MS912X_DAMAGE_POLICY_H */