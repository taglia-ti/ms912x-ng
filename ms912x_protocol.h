/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MS912X_PROTOCOL_H
#define MS912X_PROTOCOL_H

#include <linux/bits.h>
#include <linux/build_bug.h>
#include <linux/stddef.h>
#include <linux/types.h>

#define MS912X_REQ_TYPE_WRITE_6_BYTES	0xa6
#define MS912X_REQ_TYPE_READ_BYTE	0xb5
#define MS912X_REQ_TYPE_READ_BLOCK	0xb5
#define MS912X_REQ_TYPE_WRITE_BYTE	0xb6
#define MS912X_REQ_TYPE_READ_FLASH	0xf5

#define MS912X_BLOCK_READ_DATA_SIZE	61
#define MS912X_BLOCK_READ_CHUNK_SIZE	60

#define MS912X_REG_VIDEO_PORT		0x0031
#define MS912X_REG_DISPLAY_STATUS	0x0032
#define MS912X_REG_EDID_BASE		0xc000
#define MS912X_EDID_CACHE_BLOCKS	8
#define MS912X_EDID_CACHE_BLOCK_SIZE	128

#define MS912X_REG_MODE_SEQUENCE_0	0x0030
#define MS912X_REG_MODE_SEQUENCE_1	0x0033
#define MS912X_REG_MODE_SEQUENCE_2	0xc620

#define MS912X_CMD_RESOLUTION		0x01
#define MS912X_CMD_MODE			0x02
#define MS912X_CMD_UNKNOWN1		0x03
#define MS912X_CMD_UNKNOWN2		0x04
#define MS912X_CMD_OUTPUT_ENABLE	0x05
#define MS912X_CMD_POWER		0x07

#define MS913X_REG_CHIP_ID		0xff00
#define MS912X_REG_CHIP_ID		0xf000
#define MS913X_CHIP_ID_SIGNATURE_MSB	0x13
#define MS912X_CHIP_ID_SIGNATURE_MSB	0x16
#define MS91XX_CHIP_ID_SIGNATURE_LSB	0x0a
#define MS913X_CUSTOM_TIMING_BASE	0xfc50
#define MS912X_CUSTOM_TIMING_BASE	0x1c00
#define MS912X_CUSTOM_TIMING_OFFSET	0x10
#define MS912X_CUSTOM_TIMING_STRIDE	0x20

#define MS912X_TIMING_PROGRESSIVE	BIT(0)
#define MS912X_TIMING_POSITIVE_HSYNC	BIT(1)
#define MS912X_TIMING_POSITIVE_VSYNC	BIT(2)

#define MS912X_PIXFMT_RGB888		0x11
#define MS912X_PIXFMT_UYVY		0x22
#define MS912X_BYTE_SELECT_UYVY	0x00
#define MS912X_FRAME_OVERHEAD		16

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
static_assert(sizeof(struct ms912x_block_read_request) == 64);

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
	u8 mode;
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

#endif /* MS912X_PROTOCOL_H */
