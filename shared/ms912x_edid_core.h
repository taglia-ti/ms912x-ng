/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MS912X_EDID_CORE_H
#define MS912X_EDID_CORE_H

#ifdef __KERNEL__
#include <linux/stddef.h>
#else
#include <stddef.h>
#endif

/* Repair only the adapter's zeroed second header byte when the checksum matches. */
static inline int ms912x_core_edid_repair_header_byte1(unsigned char *block, size_t length)
{
	unsigned int checksum = 0;
	size_t i;

	if (!block || length != 128 || block[0] != 0 || block[1] != 0 || block[7] != 0)
		return 0;
	for (i = 2; i < 7; ++i) {
		if (block[i] != 0xff)
			return 0;
	}
	for (i = 0; i < length; ++i)
		checksum += block[i];
	if (((checksum + 0xffU) & 0xffU) != 0)
		return 0;
	block[1] = 0xff;
	return 1;
}

#endif
