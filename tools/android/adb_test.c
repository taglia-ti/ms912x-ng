/* SPDX-License-Identifier: GPL-2.0-only */
#define _POSIX_C_SOURCE 200809L
#include "adb.h"
#include "adb_spake2.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <mbedtls/base64.h>
#include <mbedtls/pem.h>
#include <mbedtls/rsa.h>

static void from_hex(const char *hex, uint8_t *bytes, size_t length)
{
	for (size_t i = 0; i < length; i++) {
		unsigned value;
		assert(sscanf(hex + 2 * i, "%2x", &value) == 1);
		bytes[i] = (uint8_t)value;
	}
}
static void spake_vectors(void)
{
	struct adb_spake2 client, server;
	uint8_t password[70], a[64], b[64], key[64], other[64], expected[64];
	memcpy(password, "123456", 6);
	memset(password + 6, 0xa5, 64);
	for (int i = 0; i < 64; i++) {
		a[i] = (uint8_t)i;
		b[i] = (uint8_t)(i + 64);
	}
	assert(!adb_spake2_start(&client, 0, password, 70, a));
	assert(!adb_spake2_start(&server, 1, password, 70, b));
	from_hex(
		"065f7cb42693cd91ee88cc20710bd8dda680ecf727c1c63a0d0b39253397f27b",
		expected, 32);
	assert(!memcmp(client.message, expected, 32));
	from_hex(
		"a2badee9f920773b294b8122dc9d713f6c9efe58348e5bf17cad7522955eaf19",
		expected, 32);
	assert(!memcmp(server.message, expected, 32));
	uint8_t client_message[32];
	memcpy(client_message, client.message, 32);
	assert(!adb_spake2_finish(&client, server.message, key));
	assert(!adb_spake2_finish(&server, client_message, other));
	assert(!memcmp(key, other, 64));
	from_hex(
		"02c2dd532c0b3b368cb7949410efc71ce0eba98ea44fc53133714181eacc30e08da5063adf84a06726568fa27bc9c3c8fca50c4a270a46840b74f42059108915",
		expected, 64);
	assert(!memcmp(key, expected, 64));
	uint8_t encrypted[25], plain[9];
	assert(!adb_pairing_crypt(1, key, (const uint8_t *)"DroidDeck", 9,
				  encrypted));
	from_hex("22995daac0e869e3c1eded3d16252bf6ad7d29f722ee3389e2", expected,
		 25);
	assert(!memcmp(encrypted, expected, 25));
	assert(!adb_pairing_crypt(0, other, encrypted, 25, plain));
	assert(!memcmp(plain, "DroidDeck", 9));
	encrypted[0] ^= 1;
	assert(adb_pairing_crypt(0, other, encrypted, 25, plain));
	password[0] = '9';
	assert(!adb_spake2_start(&client, 0, password, 70, a));
	password[0] = '1';
	assert(!adb_spake2_start(&server, 1, password, 70, b));
	memcpy(client_message, client.message, 32);
	assert(!adb_spake2_finish(&client, server.message, key));
	assert(!adb_spake2_finish(&server, client_message, other));
	assert(memcmp(key, other, 64));
	assert(!adb_pairing_crypt(1, key, (const uint8_t *)"DroidDeck", 9,
				  encrypted));
	assert(adb_pairing_crypt(0, other, encrypted, 25, plain));
	assert(!adb_spake2_start(&client, 0, password, 70, a));
	memset(expected, 0xff, 32);
	assert(adb_spake2_finish(&client, expected, key));
}
static uint8_t *saved_pem;
static size_t saved_size;
static bool load_fails;
static int load_key(void *unused, uint8_t **data, size_t *size)
{
	(void)unused;
	*data = NULL;
	*size = saved_size;
	if (saved_pem) {
		*data = malloc(saved_size);
		assert(*data);
		memcpy(*data, saved_pem, saved_size);
	}
	return load_fails ? -1 : 0;
}
static int save_key(void *unused, const uint8_t *data, size_t size)
{
	(void)unused;
	saved_pem = malloc(size);
	assert(saved_pem);
	memcpy(saved_pem, data, size);
	saved_size = size;
	return 0;
}
static void identities(void)
{
	struct adb_store store = { .load_key = load_key, .save_key = save_key };
	struct adb_identity a, b;
	struct adb_operation op;
	adb_operation_init(&op, NULL, 120000);
	adb_identity_init(&a);
	adb_identity_init(&b);
	assert(!adb_identity_load(&a, &store, true, &op));
	assert(!adb_identity_load(&b, &store, false, &op));
	char ka[1024], kb[1024];
	size_t na, nb;
	assert(!adb_public_key(&a, ka, &na));
	assert(!adb_public_key(&b, kb, &nb));
	assert(na == nb && !memcmp(ka, kb, na));
	assert(ka[na - 1] == 0);
	uint8_t public_wire[524];
	size_t wire_size;
	const char *space = strchr(ka, ' ');
	assert(space);
	assert(!mbedtls_base64_decode(public_wire, sizeof(public_wire),
				      &wire_size, (const uint8_t *)ka,
				      (size_t)(space - ka)));
	assert(wire_size == 524 && adb_get_le32(public_wire) == 64 &&
	       adb_get_le32(public_wire + 520) == 65537);
	assert(adb_get_le32(public_wire + 4) * adb_get_le32(public_wire + 8) ==
	       UINT32_MAX);
	mbedtls_mpi modulus, wire_modulus;
	mbedtls_mpi_init(&modulus);
	mbedtls_mpi_init(&wire_modulus);
	assert(!mbedtls_rsa_export(mbedtls_pk_rsa(a.key), &modulus, NULL, NULL,
				   NULL, NULL));
	assert(!mbedtls_mpi_read_binary_le(&wire_modulus, public_wire + 8,
					   256));
	assert(!mbedtls_mpi_cmp_mpi(&modulus, &wire_modulus));
	mbedtls_mpi_free(&modulus);
	mbedtls_mpi_free(&wire_modulus);
	/* Rust stored PKCS#8. Wrap this generated PKCS#1 key as PrivateKeyInfo
     * and restore it through the same production persistence callback. */
	uint8_t der[4096], pkcs8[4096], encoded[8192];
	int der_size = mbedtls_pk_write_key_der(&a.key, der, sizeof(der));
	assert(der_size > 0 && der_size < 4000);
	size_t body = (size_t)der_size + 22;
	const uint8_t prefix[] = { 0x30,
				   0x82,
				   (uint8_t)(body >> 8),
				   (uint8_t)body,
				   2,
				   1,
				   0,
				   0x30,
				   0x0d,
				   6,
				   9,
				   0x2a,
				   0x86,
				   0x48,
				   0x86,
				   0xf7,
				   0x0d,
				   1,
				   1,
				   1,
				   5,
				   0,
				   4,
				   0x82,
				   (uint8_t)(der_size >> 8),
				   (uint8_t)der_size };
	memcpy(pkcs8, prefix, sizeof(prefix));
	memcpy(pkcs8 + sizeof(prefix), der + sizeof(der) - der_size,
	       (size_t)der_size);
	size_t encoded_size;
	assert(!mbedtls_pem_write_buffer(
		"-----BEGIN PRIVATE KEY-----\n", "-----END PRIVATE KEY-----\n",
		pkcs8, sizeof(prefix) + (size_t)der_size, encoded,
		sizeof(encoded), &encoded_size));
	adb_wipe(saved_pem, saved_size);
	free(saved_pem);
	saved_pem = NULL;
	saved_size = 0;
	save_key(NULL, encoded, encoded_size - 1);
	struct adb_identity restored;
	adb_identity_init(&restored);
	assert(!adb_identity_load(&restored, &store, false, &op));
	assert(!adb_public_key(&restored, kb, &nb) && na == nb &&
	       !memcmp(ka, kb, na));
	adb_identity_free(&restored);
	load_fails = true;
	adb_identity_init(&restored);
	assert(adb_identity_load(&restored, &store, false, &op));
	assert(strstr(op.error, "credential store"));
	adb_identity_free(&restored);
	load_fails = false;
	adb_wipe(der, sizeof(der));
	adb_wipe(pkcs8, sizeof(pkcs8));
	adb_wipe(encoded, sizeof(encoded));
	uint8_t token[20] = { 42 }, signature[256];
	assert(!adb_sign_token(&a, token, signature));
	assert(!mbedtls_pk_verify(&b.key, MBEDTLS_MD_SHA1, token, 20, signature,
				  256));
	token[0] ^= 1;
	assert(mbedtls_pk_verify(&b.key, MBEDTLS_MD_SHA1, token, 20, signature,
				 256));
	adb_identity_free(&a);
	adb_identity_free(&b);
	adb_wipe(saved_pem, saved_size);
	free(saved_pem);
	saved_pem = NULL;
	saved_size = 0;
}
static void validation(void)
{
	assert(adb_pairing_code_valid("004219"));
	const char *bad[] = { "12345", "1234567", "12a456", "１２３４５６",
			      NULL };
	for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); i++)
		assert(!adb_pairing_code_valid(bad[i]));
	assert(adb_numeric_address("127.0.0.1") && adb_numeric_address("::1"));
	assert(!adb_numeric_address("localhost") &&
	       !adb_numeric_address("bad"));
	assert(adb_device_valid("adb-phone") &&
	       !adb_device_valid("abc\nsecret"));
	assert(adb_device_valid("phone-😀") &&
	       !adb_device_valid("bad\xc2\x80") &&
	       !adb_device_valid("bad\xed\xa0\x80"));
	assert(!adb_device_valid("bad\xf0\x80\x80\x80") &&
	       !adb_device_valid("bad\xf4\x90\x80\x80"));
	assert(!adb_device_valid("bad\xe2") &&
	       !adb_device_valid("bad\xc0\x80"));
	assert(!adb_digest_valid("../../bad"));
	char digest[65];
	adb_sha256_hex("abc", 3, digest);
	assert(!strcmp(
		digest,
		"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
	assert(adb_digest_valid(digest));
	atomic_bool cancel = false;
	struct adb_operation op;
	adb_operation_init(&op, &cancel, 100);
	assert(!adb_check(&op));
	atomic_store(&cancel, true);
	assert(adb_check(&op));
	assert(strstr(op.error, "cancelled"));
	assert(op.error_kind == ADB_ERROR_CANCELLED);
	adb_operation_init(&op, NULL, 1);
	op.deadline_ms = adb_now_ms() - 1;
	assert(adb_check(&op));
	assert(strstr(op.error, "timed out"));
	assert(op.error_kind == ADB_ERROR_TIMEOUT);
	adb_operation_init(&op, &cancel, 100);
	assert(adb_fail(&op, "First failure"));
	assert(adb_check(&op));
	assert(op.error_kind == ADB_ERROR_FAILURE && !strcmp(op.error, "First failure"));
}
int main(void)
{
	validation();
	spake_vectors();
	identities();
	puts("C ADB: validation, SPAKE2/AES reference vectors, tampering and persistent RSA identity passed");
	return 0;
}
