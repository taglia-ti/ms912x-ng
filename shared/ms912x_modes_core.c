/* SPDX-License-Identifier: GPL-2.0-only */

#include "ms912x_modes_core.h"

#define MODE(w, h, z, v, clk, ht, vt, ho, vo, hsw, vsw, pol) \
	{ (w), (h), (z), (v), (clk), (ht), (vt), (ho), (vo), (hsw), (vsw), (pol) }

static const struct ms912x_mode modes[] = {
#define MS912X_MODE_TIMED MODE
#include "../ms912x_mode_catalog.h"
#undef MS912X_MODE_TIMED
};

size_t ms912x_core_mode_count(void)
{
	return sizeof(modes) / sizeof(modes[0]);
}

const struct ms912x_mode *ms912x_core_mode_at(size_t index)
{
	return index < ms912x_core_mode_count() ? &modes[index] : NULL;
}
