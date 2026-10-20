/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MS912X_CAPABILITIES_H
#define MS912X_CAPABILITIES_H

enum ms912x_sdram_type {
	MS912X_SDRAM_UNKNOWN,
	MS912X_SDRAM_2M,
	MS912X_SDRAM_4M,
	MS912X_SDRAM_8M,
	MS912X_SDRAM_16M,
	MS912X_SDRAM_NONE,
};

static inline enum ms912x_sdram_type
ms912x_sdram_type_from_register(unsigned int value)
{
	switch (value) {
	case 0:
		return MS912X_SDRAM_2M;
	case 1:
		return MS912X_SDRAM_4M;
	case 2:
		return MS912X_SDRAM_8M;
	case 3:
		return MS912X_SDRAM_16M;
	case 4:
		return MS912X_SDRAM_NONE;
	default:
		return MS912X_SDRAM_UNKNOWN;
	}
}

#endif