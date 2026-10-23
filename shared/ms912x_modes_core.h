/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MS912X_MODES_CORE_H
#define MS912X_MODES_CORE_H

#ifdef __KERNEL__
#include <linux/types.h>
typedef u8 ms912x_mode_u8;
typedef u16 ms912x_mode_u16;
typedef u32 ms912x_mode_u32;
#else
#include <stddef.h>
#include <stdint.h>
typedef uint8_t ms912x_mode_u8;
typedef uint16_t ms912x_mode_u16;
typedef uint32_t ms912x_mode_u32;
#endif

struct ms912x_mode {
	int width;
	int height;
	int hz;
	ms912x_mode_u8 vic;
	ms912x_mode_u32 clock_khz;
	ms912x_mode_u16 htotal;
	ms912x_mode_u16 vtotal;
	ms912x_mode_u16 hoffset;
	ms912x_mode_u16 voffset;
	ms912x_mode_u16 hsync_width;
	ms912x_mode_u16 vsync_width;
	ms912x_mode_u8 polarity;
};

size_t ms912x_core_mode_count(void);
const struct ms912x_mode *ms912x_core_mode_at(size_t index);
const struct ms912x_mode *ms912x_core_mode_by_vic(ms912x_mode_u8 vic);

#endif
