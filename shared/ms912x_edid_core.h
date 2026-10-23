/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MS912X_EDID_CORE_H
#define MS912X_EDID_CORE_H

#include "../ms912x_protocol_values.h"

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

static inline int ms912x_core_edid_checksum_valid(const unsigned char *block, size_t length)
{
    unsigned int checksum = 0;
    size_t i;
    if (!block || length != 128) return 0;
    for (i = 0; i < length; ++i) checksum += block[i];
    return !(checksum & 255U);
}
static inline int ms912x_core_edid_header_valid(const unsigned char *block, size_t length)
{
    size_t i;
    if (!block || length < 8 || block[0] || block[7]) return 0;
    for (i = 1; i < 7; ++i) if (block[i] != 255) return 0;
    return 1;
}
/* Shared byte fallback for adapter XDATA; each caller owns transport locking. */
static inline int ms912x_core_read_edid_bytes(void *context,
    int (*read)(void *, unsigned int), unsigned int block, unsigned char *data, size_t length)
{
    size_t i;
    unsigned int address;
    if (!data || length > 128 || block > (65535U - MS912X_REG_EDID_BASE) / 128U) return -1;
    address = MS912X_REG_EDID_BASE + block * 128U;
    if (length && length - 1 > 65535U - address) return -1;
    for (i = 0; i < length; ++i) {
        int value = read(context, address + i);
        if (value < 0) return value;
        data[i] = value;
    }
    return 0;
}

#endif
