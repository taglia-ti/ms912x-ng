/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MS912X_EDID_H
#define MS912X_EDID_H

#define MS912X_EDID_BLOCK_SIZE 128
#define MS912X_EDID_SYNC_POSITIVE_HSYNC (1U << 0)
#define MS912X_EDID_SYNC_POSITIVE_VSYNC (1U << 1)
#define MS912X_EDID_SYNC_INTERLACED (1U << 2)

static inline int
ms912x_edid_encode_detailed_timing_fields(unsigned char *descriptor,
				   unsigned int clock_khz,
				   unsigned int hdisplay,
				   unsigned int hsync_start,
				   unsigned int hsync_end,
				   unsigned int htotal,
				   unsigned int vdisplay,
				   unsigned int vsync_start,
				   unsigned int vsync_end,
				   unsigned int vtotal,
				   unsigned int sync_flags)
{
	unsigned int hblank, vblank, hsync_offset, hsync_width;
	unsigned int vsync_offset, vsync_width, pixel_clock;
	unsigned int i;

	if (!clock_khz || clock_khz % 10 || htotal <= hdisplay ||
	    vtotal <= vdisplay || hsync_start < hdisplay ||
	    hsync_end <= hsync_start || hsync_end > htotal ||
	    vsync_start < vdisplay || vsync_end <= vsync_start ||
	    vsync_end > vtotal)
		return 0;

	hblank = htotal - hdisplay;
	vblank = vtotal - vdisplay;
	hsync_offset = hsync_start - hdisplay;
	hsync_width = hsync_end - hsync_start;
	vsync_offset = vsync_start - vdisplay;
	vsync_width = vsync_end - vsync_start;
	pixel_clock = clock_khz / 10;
	if (pixel_clock > 0xffff || hdisplay > 4095 || hblank > 4095 ||
	    vdisplay > 4095 || vblank > 4095 || hsync_offset > 1023 ||
	    hsync_width > 1023 || vsync_offset > 63 || vsync_width > 63)
		return 0;

	for (i = 0; i < 18; i++)
		descriptor[i] = 0;
	descriptor[0] = pixel_clock & 0xff;
	descriptor[1] = pixel_clock >> 8;
	descriptor[2] = hdisplay & 0xff;
	descriptor[3] = hblank & 0xff;
	descriptor[4] = ((hdisplay >> 8) << 4) | (hblank >> 8);
	descriptor[5] = vdisplay & 0xff;
	descriptor[6] = vblank & 0xff;
	descriptor[7] = ((vdisplay >> 8) << 4) | (vblank >> 8);
	descriptor[8] = hsync_offset & 0xff;
	descriptor[9] = hsync_width & 0xff;
	descriptor[10] = ((vsync_offset & 0x0f) << 4) |
		(vsync_width & 0x0f);
	descriptor[11] = (((hsync_offset >> 8) & 0x03) << 6) |
		(((hsync_width >> 8) & 0x03) << 4) |
		(((vsync_offset >> 4) & 0x03) << 2) |
		((vsync_width >> 4) & 0x03);
	descriptor[17] = 0x18;
	if (sync_flags & MS912X_EDID_SYNC_POSITIVE_VSYNC)
		descriptor[17] |= 1U << 2;
	if (sync_flags & MS912X_EDID_SYNC_POSITIVE_HSYNC)
		descriptor[17] |= 1U << 1;
	if (sync_flags & MS912X_EDID_SYNC_INTERLACED)
		descriptor[17] |= 1U << 7;

	return 1;
}

static inline void ms912x_edid_set_block_checksum(unsigned char *block)
{
	unsigned char checksum = 0;
	unsigned int i;

	for (i = 0; i < MS912X_EDID_BLOCK_SIZE - 1; i++)
		checksum += block[i];
	block[MS912X_EDID_BLOCK_SIZE - 1] = -checksum;
}

#endif