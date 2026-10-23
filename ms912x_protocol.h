/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MS912X_PROTOCOL_H
#define MS912X_PROTOCOL_H

#include <linux/bits.h>
#include <linux/build_bug.h>
#include <linux/stddef.h>
#include <linux/types.h>

#include "ms912x_protocol_values.h"

struct ms912x_request {
	u8 type;
	__be16 addr;
	u8 data[5];
} __packed;

struct ms912x_block_read_request {
	u8 type;
	__be16 addr;
	u8 data[MS912X_BLOCK_READ_DATA_SIZE];
} __packed;

static_assert(offsetof(struct ms912x_block_read_request, data) == 3);
static_assert(sizeof(struct ms912x_block_read_request) == 8);

struct ms912x_write_request {
	u8 type;
	u8 addr;
	u8 data[6];
} __packed;

struct ms912x_resolution_request {
	__be16 width;
	__be16 height;
	u8 pixel_format;
	u8 byte_select;
} __packed;

struct ms912x_mode_request {
	u8 vic;
	u8 pixel_format;
	__be16 width;
	__be16 height;
} __packed;

struct ms912x_frame_update_header {
	__be16 marker;
	u8 position[3];	/* x:y, packed 12-bit big-endian values */
	u8 dimensions[3];	/* width:height, packed 12-bit big-endian values */
} __packed;

struct ms912x_flash_read_request {
	u8 type;
	u8 addr[3];
	u8 reserved[4];
} __packed;

struct ms912x_custom_timing_record {
	u8 vic;
	u8 polarity;
	__le16 htotal;
	__le16 vtotal;
	__le16 hactive;
	__le16 vactive;
	__le16 pixclk;
	__le16 vfreq;
	__le16 hoffset;
	__le16 voffset;
	__le16 hsyncwidth;
	__le16 vsyncwidth;
} __packed;
static_assert(sizeof(struct ms912x_custom_timing_record) == MS912X_CUSTOM_TIMING_RECORD_SIZE);

#endif /* MS912X_PROTOCOL_H */
