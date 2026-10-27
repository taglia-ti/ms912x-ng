/* SPDX-License-Identifier: GPL-2.0-only */
#include "adb.h"
#include <stdlib.h>
#include <string.h>
#include <mbedtls/base64.h>
#include <mbedtls/platform_util.h>
#include <mbedtls/rsa.h>
#include <mbedtls/sha256.h>
#include <mbedtls/x509.h>
#include <psa/crypto.h>

void adb_wipe(void *p, size_t n)
{
	mbedtls_platform_zeroize(p, n);
}

void adb_sha256_hex(const void *bytes, size_t n, char out[65])
{
	static const char hex[] = "0123456789abcdef";
	uint8_t hash[32];
	mbedtls_sha256(bytes, n, hash, 0);
	for (size_t i = 0; i < sizeof(hash); i++) {
		out[2 * i] = hex[hash[i] >> 4];
		out[2 * i + 1] = hex[hash[i] & 15];
	}
	out[64] = 0;
	adb_wipe(hash, sizeof(hash));
}

void adb_identity_init(struct adb_identity *id)
{
	mbedtls_entropy_init(&id->entropy);
	mbedtls_ctr_drbg_init(&id->rng);
	mbedtls_pk_init(&id->key);
	mbedtls_x509_crt_init(&id->cert);
}

void adb_identity_free(struct adb_identity *id)
{
	mbedtls_x509_crt_free(&id->cert);
	mbedtls_pk_free(&id->key);
	mbedtls_ctr_drbg_free(&id->rng);
	mbedtls_entropy_free(&id->entropy);
	adb_wipe(id, sizeof(*id));
}

static int make_certificate(struct adb_identity *id)
{
	mbedtls_x509write_cert writer;
	mbedtls_mpi serial;
	uint8_t der[4096];
	mbedtls_x509write_crt_init(&writer);
	mbedtls_mpi_init(&serial);
	mbedtls_x509write_crt_set_version(&writer, MBEDTLS_X509_CRT_VERSION_3);
	mbedtls_x509write_crt_set_md_alg(&writer, MBEDTLS_MD_SHA256);
	mbedtls_x509write_crt_set_subject_key(&writer, &id->key);
	mbedtls_x509write_crt_set_issuer_key(&writer, &id->key);
	int result = -1;
	if (mbedtls_mpi_lset(&serial, 1) ||
	    mbedtls_x509write_crt_set_serial(&writer, &serial) ||
	    mbedtls_x509write_crt_set_subject_name(&writer,
						   "C=US,O=Android,CN=Adb") ||
	    mbedtls_x509write_crt_set_issuer_name(&writer,
						  "C=US,O=Android,CN=Adb") ||
	    mbedtls_x509write_crt_set_validity(&writer, "20200101000000",
					       "99991231235959") ||
	    mbedtls_x509write_crt_set_basic_constraints(&writer, 1, -1) ||
	    mbedtls_x509write_crt_set_key_usage(
		    &writer, MBEDTLS_X509_KU_DIGITAL_SIGNATURE |
				     MBEDTLS_X509_KU_KEY_CERT_SIGN |
				     MBEDTLS_X509_KU_CRL_SIGN))
		goto done;
	int length = mbedtls_x509write_crt_der(
		&writer, der, sizeof(der), mbedtls_ctr_drbg_random, &id->rng);
	if (length > 0)
		result = mbedtls_x509_crt_parse_der(
			&id->cert, der + sizeof(der) - length, (size_t)length);
done:
	mbedtls_x509write_crt_free(&writer);
	mbedtls_mpi_free(&serial);
	return result;
}

int adb_identity_load(struct adb_identity *id, const struct adb_store *store,
		      bool create, struct adb_operation *op)
{
	uint8_t *pem = NULL;
	size_t length = 0;
	int result = -1;
	if (adb_check(op))
		return -1;
	if (psa_crypto_init() != PSA_SUCCESS ||
	    mbedtls_ctr_drbg_seed(&id->rng, mbedtls_entropy_func, &id->entropy,
				  (const uint8_t *)ADB_CLIENT_NAME,
				  strlen(ADB_CLIENT_NAME)))
		return adb_fail(op, "Could not initialize ADB cryptography");
	if (store->load_key(store->context, &pem, &length)) {
		if (pem) {
			adb_wipe(pem, length);
			free(pem);
		}
		return adb_fail(op,
				"Android credential store operation failed");
	}
	if (pem) {
		/* Persistence owns raw bytes. Only the parser's private copy needs
		 * a terminator, so an adapter cannot cause a one-byte overread. */
		uint8_t *terminated = length && length <= 16384 ?
					      calloc(length + 1, 1) :
					      NULL;
		if (terminated) {
			memcpy(terminated, pem, length);
			result = mbedtls_pk_parse_key(&id->key, terminated,
						      length + 1, NULL, 0,
						      mbedtls_ctr_drbg_random,
						      &id->rng);
			adb_wipe(terminated, length + 1);
			free(terminated);
		}
		adb_wipe(pem, length);
		free(pem);
		if (result)
			return adb_fail(op,
					"Stored ADB host identity is invalid");
	} else {
		if (!create)
			return adb_fail(op, "Missing saved ADB host identity");
		uint8_t encoded[4096] = { 0 };
		result = mbedtls_pk_setup(
			&id->key, mbedtls_pk_info_from_type(MBEDTLS_PK_RSA));
		if (!result)
			result = mbedtls_rsa_gen_key(mbedtls_pk_rsa(id->key),
						     mbedtls_ctr_drbg_random,
						     &id->rng, 2048, 65537);
		if (!result)
			result = mbedtls_pk_write_key_pem(&id->key, encoded,
							  sizeof(encoded));
		if (!result)
			result = store->save_key(store->context, encoded,
						 strlen((char *)encoded));
		adb_wipe(encoded, sizeof(encoded));
		if (result)
			return adb_fail(
				op,
				"Could not create or save ADB host identity");
	}
	if (!mbedtls_pk_can_do(&id->key, MBEDTLS_PK_RSA) ||
	    mbedtls_pk_get_bitlen(&id->key) != 2048 ||
	    mbedtls_rsa_check_privkey(mbedtls_pk_rsa(id->key)))
		return adb_fail(
			op, "ADB identity must contain a valid RSA-2048 key");
	if (make_certificate(id))
		return adb_fail(op, "Could not create ADB TLS identity");
	return adb_check(op);
}

int adb_public_key(struct adb_identity *id, char output[1024], size_t *length)
{
	mbedtls_mpi n, e, rr;
	mbedtls_mpi_init(&n);
	mbedtls_mpi_init(&e);
	mbedtls_mpi_init(&rr);
	uint8_t wire[524] = { 0 }, exponent[4];
	int result = mbedtls_rsa_export(mbedtls_pk_rsa(id->key), &n, NULL, NULL,
					NULL, &e);
	if (result || mbedtls_mpi_write_binary_le(&n, wire + 8, 256) ||
	    mbedtls_mpi_write_binary_le(&e, exponent, sizeof(exponent)))
		goto fail;
	uint32_t exp = adb_get_le32(exponent);
	if (exp != 3 && exp != 65537)
		goto fail;
	uint32_t low = adb_get_le32(wire + 8), inverse = 1;
	for (int i = 0; i < 5; i++)
		inverse *= 2u - low * inverse;
	adb_put_le32(wire, 64);
	adb_put_le32(wire + 4, 0u - inverse);
	adb_put_le32(wire + 520, exp);
	if (mbedtls_mpi_lset(&rr, 1) || mbedtls_mpi_shift_l(&rr, 4096) ||
	    mbedtls_mpi_mod_mpi(&rr, &rr, &n) ||
	    mbedtls_mpi_write_binary_le(&rr, wire + 264, 256))
		goto fail;
	size_t encoded = 0;
	if (mbedtls_base64_encode((uint8_t *)output, 1024, &encoded, wire,
				  sizeof(wire)))
		goto fail;
	memcpy(output + encoded, " " ADB_CLIENT_NAME,
	       sizeof(" " ADB_CLIENT_NAME));
	*length = encoded + sizeof(" " ADB_CLIENT_NAME);
	result = 0;
	goto done;
fail:
	result = -1;
done:
	mbedtls_mpi_free(&n);
	mbedtls_mpi_free(&e);
	mbedtls_mpi_free(&rr);
	return result;
}

int adb_sign_token(struct adb_identity *id, const uint8_t token[20],
		   uint8_t signature[256])
{
	size_t length = 0;
	return mbedtls_pk_sign(&id->key, MBEDTLS_MD_SHA1, token, 20, signature,
			       256, &length, mbedtls_ctr_drbg_random,
			       &id->rng) ||
			       length != 256 ?
		       -1 :
		       0;
}
