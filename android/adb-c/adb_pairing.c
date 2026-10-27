/* SPDX-License-Identifier: GPL-2.0-only */
#include "adb.h"
#include "adb_spake2.h"
#include <string.h>
#include <mbedtls/sha256.h>

static int pairing_send(struct adb_connection *c, uint8_t type,
			const uint8_t *data, size_t length)
{
	uint8_t header[6] = { 1, type, 0, 0, 0, 0 };
	for (int i = 0; i < 4; i++)
		header[2 + i] = (uint8_t)(length >> ((3 - i) * 8));
	return adb_write(c, header, 6) || adb_write(c, data, length) ? -1 : 0;
}
static int pairing_receive(struct adb_connection *c, uint8_t type,
			   uint8_t *data, size_t expected)
{
	uint8_t header[6];
	if (adb_read(c, header, 6))
		return -1;
	uint32_t length = 0;
	for (int i = 2; i < 6; i++)
		length = (length << 8) | header[i];
	if (header[0] != 1 || header[1] != type || length != expected)
		return adb_fail(c->op, "Invalid ADB pairing packet");
	return adb_read(c, data, expected);
}
int adb_pair(struct adb_connection *c, struct adb_identity *id,
	     const char *code, char device[256], char fingerprint[96])
{
	struct adb_spake2 spake = { 0 };
	uint8_t password[70] = { 0 }, random[64] = { 0 }, peer[32],
		material[64] = { 0 };
	uint8_t info[8192] = { 0 }, encrypted[8208];
	int result = -1;
	if (!adb_pairing_code_valid(code))
		return adb_fail(c->op, "Pairing code must contain six digits");
	if (adb_start_tls(c, id))
		goto done;
	const mbedtls_x509_crt *cert = mbedtls_ssl_get_peer_cert(&c->ssl);
	uint8_t digest[32];
	if (!cert || mbedtls_sha256(cert->raw.p, cert->raw.len, digest, 0))
		goto crypto_error;
	static const char hex[] = "0123456789ABCDEF";
	for (size_t i = 0; i < 32; i++) {
		fingerprint[3 * i] = hex[digest[i] >> 4];
		fingerprint[3 * i + 1] = hex[digest[i] & 15];
		fingerprint[3 * i + 2] = i == 31 ? 0 : ':';
	}
	memcpy(password, code, 6);
	if (mbedtls_ssl_export_keying_material(&c->ssl, password + 6, 64,
					       "adb-label", sizeof("adb-label"),
					       NULL, 0, 0) ||
	    mbedtls_ctr_drbg_random(&id->rng, random, sizeof(random)) ||
	    adb_spake2_start(&spake, 0, password, sizeof(password), random))
		goto crypto_error;
	if (pairing_send(c, 0, spake.message, 32) ||
	    pairing_receive(c, 0, peer, 32))
		goto done;
	if (adb_spake2_finish(&spake, peer, material))
		goto crypto_error;
	char public_key[1024];
	size_t length;
	if (adb_public_key(id, public_key, &length))
		goto crypto_error;
	memcpy(info + 1, public_key, length);
	if (adb_pairing_crypt(1, material, info, sizeof(info), encrypted))
		goto crypto_error;
	if (pairing_send(c, 1, encrypted, sizeof(encrypted)) ||
	    pairing_receive(c, 1, encrypted, sizeof(encrypted)))
		goto done;
	if (adb_pairing_crypt(0, material, encrypted, sizeof(encrypted),
			      info)) {
		adb_fail(
			c->op,
			"ADB pairing authentication failed; check the pairing code");
		goto done;
	}
	const uint8_t *terminator = memchr(info + 1, 0, sizeof(info) - 1);
	if (info[0] != 1 || !terminator || terminator - info - 1 > 255 ||
	    terminator == info + 1) {
		adb_fail(c->op, "Invalid paired device identity");
		goto done;
	}
	memcpy(device, info + 1, (size_t)(terminator - info));
	if (!adb_device_valid(device)) {
		adb_fail(c->op, "Invalid paired device identity");
		goto done;
	}
	result = 0;
	goto done;
crypto_error:
	adb_fail(c->op, "ADB pairing cryptography failed");
done:
	adb_wipe(&spake, sizeof(spake));
	adb_wipe(password, sizeof(password));
	adb_wipe(random, sizeof(random));
	adb_wipe(material, sizeof(material));
	adb_wipe(info, sizeof(info));
	adb_wipe(encrypted, sizeof(encrypted));
	return result;
}
