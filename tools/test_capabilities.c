#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ms912x_capabilities.h"
#include "ms912x_edid.h"
#include "ms912x_fake_edid.h"
#include "ms912x_profile_policy.h"
#include "ms912x_scale_policy.h"

#define CHECK(condition) do { \
	if (!(condition)) { \
		fprintf(stderr, "check failed at %s:%d: %s\n", \
			__FILE__, __LINE__, #condition); \
		return 1; \
	} \
} while (0)

struct test_mode {
	unsigned int width;
	unsigned int height;
	unsigned int hz;
	unsigned int vic;
};

static unsigned int dtd_offset(unsigned int index)
{
	if (index < 2)
		return 54 + index * 18;
	index -= 2;
	return (1 + index / 6) * MS912X_EDID_BLOCK_SIZE + 4 +
		(index % 6) * 18;
}

static int test_sdram_values(void)
{
	static const enum ms912x_sdram_type expected[] = {
		MS912X_SDRAM_2M,
		MS912X_SDRAM_4M,
		MS912X_SDRAM_8M,
		MS912X_SDRAM_16M,
		MS912X_SDRAM_NONE,
	};
	unsigned int value;

	for (value = 0; value < sizeof(expected) / sizeof(expected[0]); value++)
		CHECK(ms912x_sdram_type_from_register(value) == expected[value]);
	CHECK(ms912x_sdram_type_from_register(5) == MS912X_SDRAM_UNKNOWN);
	CHECK(ms912x_sdram_type_from_register(0xff) == MS912X_SDRAM_UNKNOWN);
	return 0;
}

static int test_profile_policy(void)
{
	struct ms912x_profile_values values;

	values = ms912x_profile_values_for(MS912X_PROFILE_EFFICIENCY);
	CHECK(values.damage_scan == MS912X_DAMAGE_SCAN_OFF);
	CHECK(values.stride == 5);
	CHECK(values.tolerance_min == 0 && values.tolerance_max == 0);

	values = ms912x_profile_values_for(MS912X_PROFILE_LATENCY);
	CHECK(values.damage_scan == MS912X_DAMAGE_SCAN_ON);
	CHECK(values.stride == 2);
	CHECK(values.tolerance_min == 4 && values.tolerance_max == 32);

	values = ms912x_profile_values_for(MS912X_PROFILE_QUALITY);
	CHECK(values.damage_scan == MS912X_DAMAGE_SCAN_AUTO);
	CHECK(values.stride == 1);
	CHECK(values.tolerance_min == 1 && values.tolerance_max == 2);

	CHECK(ms912x_profile_auto_select(1, 0) == MS912X_PROFILE_LATENCY);
	CHECK(ms912x_profile_auto_select(1, 1) == MS912X_PROFILE_QUALITY);
	CHECK(ms912x_profile_auto_select(0, 0) == MS912X_PROFILE_EFFICIENCY);
	CHECK(ms912x_profile_auto_select(0, 1) == MS912X_PROFILE_EFFICIENCY);
	return 0;
}

static int test_scale_auto_policy(void)
{
	CHECK(ms912x_scale_auto_select(1920, 1080, 60, 1) ==
	      MS912X_SCALE_MODE_DOWNSCALE_2_TO_1);
	CHECK(ms912x_scale_auto_select(1280, 720, 60, 1) ==
	      MS912X_SCALE_MODE_DOWNSCALE_3_TO_2);
	CHECK(ms912x_scale_auto_select(1280, 720, 56, 1) == MS912X_SCALE_MODE_NATIVE);
	CHECK(ms912x_scale_auto_select(1280, 720, 57, 1) ==
	      MS912X_SCALE_MODE_DOWNSCALE_3_TO_2);
	CHECK(ms912x_scale_auto_select(1920, 1080, 50, 1) ==
	      MS912X_SCALE_MODE_DOWNSCALE_3_TO_2);
	CHECK(ms912x_scale_auto_select(1920, 1080, 75, 1) ==
	      MS912X_SCALE_MODE_DOWNSCALE_2_TO_1);
	CHECK(ms912x_scale_auto_select(1920, 1080, 60, 0) ==
	      MS912X_SCALE_MODE_NATIVE);
	CHECK(ms912x_scale_auto_select(1920, 1080, 0, 1) ==
	      MS912X_SCALE_MODE_DOWNSCALE_2_TO_1);
	/* Independently select the first raster within the unchanged bandwidth rule. */
	for (unsigned int w = 320; w <= 1920; w += 37)
	for (unsigned int h = 200; h <= 1200; h += 43)
	for (unsigned int hz = 24; hz <= 144; hz += 7) {
		const unsigned int numerator[] = {1, 2, 1};
		const unsigned int denominator[] = {1, 3, 2};
		const unsigned int setting[] = {100, 67, 50};
		unsigned int selected = 50;

		for (unsigned int i = 0; i < 3; i++) {
			unsigned int iw = (w * numerator[i] + denominator[i] - 1) / denominator[i];
			unsigned int ih = (h * numerator[i] + denominator[i] - 1) / denominator[i];
			if (i) iw = (iw + 3) & ~3U;
			if ((unsigned long long)iw * ih * 2 * hz <= 4ULL * 26000000) {
				selected = setting[i];
				break;
			}
		}
		CHECK(ms912x_scale_setting_for_mode(ms912x_scale_auto_select(w, h, hz, 1)) == selected);
	}
	return 0;
}

static unsigned int decode_12bit(unsigned char low, unsigned char high,
				 unsigned char mask, unsigned int shift)
{
	return low | ((high & mask) << shift);
}

static int test_catalog_and_edid(const char *source_path,
				 const char *edid_output_path)
{
	struct test_mode modes[64];
	unsigned char detailed_timings[64 * 18] = {};
	unsigned int mode_count = 0, hz30_count = 0;
	unsigned int has_640x480_75 = 0, has_1920x1200_60 = 0;
	unsigned int preferred_index = 0, i, block;
	struct test_mode preferred_mode;
	unsigned char preferred_timing[18], dummy[18];
	char line[512];
	FILE *source = fopen(source_path, "r");

	CHECK(source != NULL);
	while (fgets(line, sizeof(line), source)) {
		unsigned int width, height, hz, vic, clock, htotal, vtotal;
		unsigned int hoffset, voffset, hsync_width, vsync_width, polarity;
		unsigned char block_data[MS912X_EDID_BLOCK_SIZE] = {};
		unsigned char *dtd = block_data + 54;
		unsigned int hsync_start, vsync_start, hsync_offset;
		unsigned int decoded_hsync_width, vsync_offset, decoded_vsync_width;
		unsigned int sync_flags = 0, descriptor_flags;
		int fields;

		fields = sscanf(line,
			" MS912X_MODE_TIMED(%u, %u, %u, 0x%x, %u, %u, %u, %u, %u, %u, %u, %u)",
			&width, &height, &hz, &vic, &clock, &htotal, &vtotal,
			&hoffset, &voffset, &hsync_width, &vsync_width, &polarity);
		if (fields != 12)
			continue;
		CHECK(mode_count < sizeof(modes) / sizeof(modes[0]));
		for (i = 0; i < mode_count; i++)
			CHECK(modes[i].width != width || modes[i].height != height ||
			      modes[i].hz != hz);
		modes[mode_count] = (struct test_mode) {
			.width = width,
			.height = height,
			.hz = hz,
			.vic = vic,
		};
		mode_count++;

		if (hz == 30) {
			CHECK(width == 1920 && height == 1080 && vic == 0x22);
			hz30_count++;
		}
		if (vic == 0x41) {
			CHECK(width == 640 && height == 480 && hz == 75 &&
			      clock == 31500 && htotal == 840 && vtotal == 500);
			has_640x480_75++;
		}
		if (vic == 0x85) {
			CHECK(width == 1920 && height == 1200 && hz == 60 &&
			      clock == 154000 && htotal == 2080 && vtotal == 1235);
			has_1920x1200_60++;
		}

		if (polarity & 2)
			sync_flags |= MS912X_EDID_SYNC_POSITIVE_HSYNC;
		if (polarity & 4)
			sync_flags |= MS912X_EDID_SYNC_POSITIVE_VSYNC;
		if (!(polarity & 1))
			sync_flags |= MS912X_EDID_SYNC_INTERLACED;
		hsync_start = htotal - hoffset;
		vsync_start = vtotal - voffset;
		CHECK(ms912x_edid_encode_detailed_timing_fields(
			dtd, clock, width, hsync_start,
			hsync_start + hsync_width, htotal, height, vsync_start,
			vsync_start + vsync_width, vtotal, sync_flags));
		CHECK(((unsigned int)dtd[0] | ((unsigned int)dtd[1] << 8)) * 10 ==
		      clock);
		CHECK(decode_12bit(dtd[2], dtd[4], 0xf0, 4) == width);
		CHECK(decode_12bit(dtd[5], dtd[7], 0xf0, 4) == height);
		CHECK(decode_12bit(dtd[3], dtd[4], 0x0f, 8) == htotal - width);
		CHECK(decode_12bit(dtd[6], dtd[7], 0x0f, 8) == vtotal - height);
		hsync_offset = dtd[8] | ((dtd[11] & 0xc0) << 2);
		decoded_hsync_width = dtd[9] | ((dtd[11] & 0x30) << 4);
		vsync_offset = ((dtd[10] >> 4) & 0x0f) |
			((dtd[11] & 0x0c) << 2);
		decoded_vsync_width = (dtd[10] & 0x0f) |
			((dtd[11] & 0x03) << 4);
		CHECK(hsync_offset == hsync_start - width);
		CHECK(decoded_hsync_width == hsync_width);
		CHECK(vsync_offset == vsync_start - height);
		CHECK(decoded_vsync_width == vsync_width);
		descriptor_flags = 0x18 | ((polarity & 4) ? 0x04 : 0) |
			((polarity & 2) ? 0x02 : 0) | ((polarity & 1) ? 0 : 0x80);
		CHECK(dtd[17] == descriptor_flags);
		memcpy(detailed_timings + (mode_count - 1) * 18, dtd, 18);
	}
	fclose(source);

	CHECK(mode_count == MS912X_FAKE_EDID_FIXED_MODE_COUNT);
	CHECK(hz30_count == 1);
	CHECK(has_640x480_75 == 1);
	CHECK(has_1920x1200_60 == 1);
	CHECK(sizeof(ms912x_fake_edid_data) ==
	      MS912X_FAKE_EDID_BLOCK_COUNT * MS912X_EDID_BLOCK_SIZE);
	for (i = 0; i < mode_count; i++) {
		if (modes[i].vic == 0x81) {
			preferred_index = i;
			break;
		}
	}
	CHECK(i < mode_count);
	preferred_mode = modes[preferred_index];
	memcpy(preferred_timing, detailed_timings + preferred_index * 18, 18);
	memmove(modes + 1, modes, preferred_index * sizeof(modes[0]));
	memmove(detailed_timings + 18, detailed_timings, preferred_index * 18);
	modes[0] = preferred_mode;
	memcpy(detailed_timings, preferred_timing, 18);

	for (i = 0; i < mode_count; i++) {
		const struct ms912x_fake_edid_mode_info *info =
			&ms912x_fake_edid_modes[i];

		CHECK(info->width == modes[i].width);
		CHECK(info->height == modes[i].height);
		CHECK(info->hz == modes[i].hz);
		CHECK(info->vic == modes[i].vic);
		CHECK(!memcmp(ms912x_fake_edid_data + dtd_offset(i),
			      detailed_timings + i * 18, 18));
	}
	CHECK(ms912x_fake_edid_data[24] & (1U << 1));
	CHECK(ms912x_fake_edid_modes[0].vic == 0x81);
	CHECK(ms912x_edid_encode_detailed_timing_fields(
		dummy, 31501, 640, 656, 752, 800, 480, 490, 492, 525, 0) == 0);

	for (block = 0; block < MS912X_FAKE_EDID_BLOCK_COUNT; block++) {
		unsigned int block_checksum = 0;
		unsigned int offset = block * MS912X_EDID_BLOCK_SIZE;

		for (i = 0; i < MS912X_EDID_BLOCK_SIZE; i++)
			block_checksum += ms912x_fake_edid_data[offset + i];
		CHECK((block_checksum & 0xff) == 0);
	}
	CHECK(ms912x_fake_edid_data[126] ==
	      MS912X_FAKE_EDID_BLOCK_COUNT - 1);
	if (edid_output_path) {
		FILE *output = fopen(edid_output_path, "wb");

		CHECK(output != NULL);
		CHECK(fwrite(ms912x_fake_edid_data, sizeof(ms912x_fake_edid_data),
			     1, output) == 1);
		CHECK(fclose(output) == 0);
	}
	puts("SDRAM decoding and generated 29-mode EDID passed");
	return 0;
}

int main(int argc, char **argv)
{
	if (argc < 2 || argc > 3) {
		fprintf(stderr, "usage: %s ms912x_kms.c [edid-output.bin]\n", argv[0]);
		return 2;
	}
	if (test_sdram_values())
		return 1;
	if (test_profile_policy())
		return 1;
	if (test_scale_auto_policy())
		return 1;
	return test_catalog_and_edid(argv[1], argc == 3 ? argv[2] : NULL);
}
