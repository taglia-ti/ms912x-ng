/* SPDX-License-Identifier: GPL-2.0-only */
#include "adb.h"
#include <arpa/inet.h>
#include <string.h>

bool adb_numeric_address(const char *host)
{
	uint8_t address[16];
	return host && (inet_pton(AF_INET, host, address) == 1 ||
			inet_pton(AF_INET6, host, address) == 1);
}
bool adb_pairing_code_valid(const char *code)
{
	if (!code || strlen(code) != 6)
		return false;
	for (int i = 0; i < 6; i++)
		if (code[i] < '0' || code[i] > '9')
			return false;
	return true;
}
bool adb_device_valid(const char *device)
{
	if (!device || !*device || strlen(device) > 255)
		return false;
	const unsigned char *p = (const unsigned char *)device;
	while (*p) {
		uint32_t codepoint;
		if (adb_utf8_next(&p, &codepoint) || codepoint < 32 ||
		    (codepoint >= 127 && codepoint <= 159))
			return false;
	}
	return true;
}
int adb_utf8_next(const unsigned char **cursor, uint32_t *codepoint)
{
	const unsigned char *p = *cursor;
	uint32_t value = *p++;
	unsigned count;
	uint32_t minimum;
	if (value < 128) {
		*codepoint = value;
		*cursor = p;
		return 0;
	}
	if (value >= 0xc2 && value <= 0xdf) {
		value &= 31;
		count = 1;
		minimum = 128;
	} else if (value >= 0xe0 && value <= 0xef) {
		value &= 15;
		count = 2;
		minimum = 2048;
	} else if (value >= 0xf0 && value <= 0xf4) {
		value &= 7;
		count = 3;
		minimum = 65536;
	} else
		return -1;
	for (unsigned i = 0; i < count; i++) {
		if ((*p & 0xc0) != 0x80)
			return -1;
		value = (value << 6) | (*p++ & 63);
	}
	if (value < minimum || value > 0x10ffff ||
	    (value >= 0xd800 && value <= 0xdfff))
		return -1;
	*codepoint = value;
	*cursor = p;
	return 0;
}
bool adb_digest_valid(const char *digest)
{
	if (!digest || strlen(digest) != 64)
		return false;
	for (int i = 0; i < 64; i++)
		if (!((digest[i] >= '0' && digest[i] <= '9') ||
		      (digest[i] >= 'a' && digest[i] <= 'f')))
			return false;
	return true;
}
