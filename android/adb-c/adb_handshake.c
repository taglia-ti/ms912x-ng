/* SPDX-License-Identifier: GPL-2.0-only */
#include "adb.h"
#include <string.h>

static bool supports_shell_v2(const uint8_t *banner, size_t length)
{
	/* CNXN payload length is authoritative. adbd versions differ on whether
	 * the text includes a trailing NUL, so accept either representation. */
	if (length && !banner[length - 1])
		length--;
	if (!length || memchr(banner, 0, length))
		return false;
	size_t position = 0;
	while (position + 1 < length &&
	       (banner[position] != ':' || banner[position + 1] != ':'))
		position++;
	if (position + 1 >= length)
		return false;
	position += 2;
	while (position < length) {
		size_t end = position;
		while (end < length && banner[end] != ';')
			end++;
		if (end - position >= 9 &&
		    !memcmp(banner + position, "features=", 9)) {
			position += 9;
			while (position < end) {
				size_t next = position;
				while (next < end && banner[next] != ',')
					next++;
				if (next - position == 8 &&
				    !memcmp(banner + position, "shell_v2", 8))
					return true;
				position = next + 1;
			}
		}
		position = end + 1;
	}
	return false;
}
int adb_handshake(struct adb_connection *c, struct adb_identity *id,
		  bool wireless)
{
	static const char banner[] = "host::features=shell_v2,cmd";
	if (adb_packet_send(c, ADB_CNXN, 0x01000001, ADB_MAX_PAYLOAD, banner,
			    sizeof(banner)))
		return -1;
	unsigned auth = 0;
	int result = -1;
	for (unsigned attempts = 0; attempts < 64; attempts++) {
		const struct adb_packet *p = adb_packet_next(c);
		if (!p)
			break;
		if (p->command == ADB_STLS && wireless && !c->tls &&
		    p->arg0 == 0x01000000 && !p->arg1 && !p->length) {
			if (adb_packet_send(c, ADB_STLS, 0x01000000, 0, NULL,
					    0) ||
			    adb_start_tls(c, id))
				break;
		} else if (p->command == ADB_CNXN && (!wireless || c->tls) &&
			   p->arg0 >= 0x01000000 && p->arg1) {
			c->version =
				p->arg0 > 0x01000001 ? 0x01000001 : p->arg0;
			c->max_payload = p->arg1 > ADB_MAX_PAYLOAD ?
						 ADB_MAX_PAYLOAD :
						 p->arg1;
			/* All consumers use shell v2's separate output/exit/stdin frames. */
			if (!supports_shell_v2(p->data, p->length))
				adb_fail(
					c->op,
					"ADB endpoint does not support shell v2");
			else
				result = 0;
			break;
		} else if (p->command == ADB_AUTH && !wireless &&
			   p->arg0 == 1 && !p->arg1 && p->length == 20) {
			if (!auth++) {
				uint8_t signature[256];
				if (adb_sign_token(id, p->data, signature) ||
				    adb_packet_send(c, ADB_AUTH, 2, 0,
						    signature,
						    sizeof(signature)))
					break;
			} else if (auth == 2) {
				char key[1024];
				size_t size;
				if (adb_public_key(id, key, &size) ||
				    adb_packet_send(c, ADB_AUTH, 3, 0, key,
						    size))
					break;
			} else {
				adb_fail(c->op,
					 "ADB host key was not authorized");
				break;
			}
		} else {
			adb_fail(c->op, "Unexpected ADB handshake packet");
			break;
		}
	}
	adb_packet_release(c);
	return result ? adb_fail(c->op, "ADB handshake failed") : 0;
}
