/* SPDX-License-Identifier: GPL-2.0-only */
#include "adb_spake2.h"
#include <string.h>
#include <mbedtls/gcm.h>
#include <mbedtls/hkdf.h>
#include <mbedtls/sha512.h>
/* Deliberate single-TU adapter to the pinned, unchanged Edwards primitives.
 * Keep arithmetic in Monocypher; ADB's masks/cofactor/transcript live here. */
#include "third_party/monocypher/monocypher.c"

static const uint8_t mask_m[32] = { 0x5a, 0xda, 0x7e, 0x4b, 0xf6, 0xdd, 0xd9,
				    0xad, 0xb6, 0x62, 0x6d, 0x32, 0x13, 0x1c,
				    0x6b, 0x5c, 0x51, 0xa1, 0xe3, 0x47, 0xa3,
				    0x47, 0x8f, 0x53, 0xcf, 0xcf, 0x44, 0x1b,
				    0x88, 0xee, 0xd1, 0x2e };
static const uint8_t mask_n[32] = { 0x10, 0xe3, 0xdf, 0x0a, 0xe3, 0x7d, 0x8e,
				    0x7a, 0x99, 0xb5, 0xfe, 0x74, 0xb4, 0x46,
				    0x72, 0x10, 0x3d, 0xbd, 0xdc, 0xbd, 0x06,
				    0xaf, 0x68, 0x0d, 0x71, 0x32, 0x9a, 0x11,
				    0x69, 0x3b, 0xc7, 0x78 };
static const uint8_t order[32] = { 0xed, 0xd3, 0xf5, 0x5c, 0x1a,       0x63,
				   0x12, 0x58, 0xd6, 0x9c, 0xf7,       0xa2,
				   0xde, 0xf9, 0xde, 0x14, [31] = 0x10 };

static int point_decode(ge *p, const uint8_t encoded[32])
{
	if (ge_frombytes_neg_vartime(p, encoded))
		return -1;
	fe_neg(p->X, p->X);
	fe_neg(p->T, p->T);
	uint8_t canonical[32];
	ge_tobytes(canonical, p);
	return crypto_verify32(encoded, canonical);
}

/* Complete extended Edwards addition, with all secret temporaries wiped. */
static void point_add_cached(ge *out, const ge *p, const ge_cached *cache)
{
	fe a, b;
	fe_add(a, p->Y, p->X);
	fe_sub(b, p->Y, p->X);
	fe_mul(a, a, cache->Yp);
	fe_mul(b, b, cache->Ym);
	fe_add(out->Y, a, b);
	fe_sub(out->X, a, b);
	fe_add(out->Z, p->Z, p->Z);
	fe_mul(out->Z, out->Z, cache->Z);
	fe_mul(out->T, p->T, cache->T2);
	fe_add(a, out->Z, out->T);
	fe_sub(b, out->Z, out->T);
	fe_mul(out->T, out->X, out->Y);
	fe_mul(out->X, out->X, b);
	fe_mul(out->Y, out->Y, a);
	fe_mul(out->Z, a, b);
	crypto_wipe(a, sizeof(a));
	crypto_wipe(b, sizeof(b));
}
static void point_add(ge *out, const ge *p, const ge *q)
{
	ge_cached cache;
	ge_cache(&cache, q);
	point_add_cached(out, p, &cache);
	crypto_wipe(&cache, sizeof(cache));
}

static void point_multiply(ge *out, const ge *point, const uint8_t scalar[32])
{
	ge sum, candidate, scratch;
	ge_cached cache;
	ge_cache(&cache, point);
	ge_zero(&sum);
	/* The entire 256-bit scalar matters: reducing modulo L loses torsion. */
	for (int bit = 255; bit >= 0; bit--) {
		ge_double(&sum, &sum, &scratch);
		point_add_cached(&candidate, &sum, &cache);
		int select = (scalar[bit / 8] >> (bit % 8)) & 1;
		fe_ccopy(sum.X, candidate.X, select);
		fe_ccopy(sum.Y, candidate.Y, select);
		fe_ccopy(sum.Z, candidate.Z, select);
		fe_ccopy(sum.T, candidate.T, select);
	}
	*out = sum;
	crypto_wipe(&sum, sizeof(sum));
	crypto_wipe(&candidate, sizeof(candidate));
	crypto_wipe(&scratch, sizeof(scratch));
	crypto_wipe(&cache, sizeof(cache));
}

static void scalar_add(uint8_t out[32], const uint8_t a[32],
		       const uint8_t b[32])
{
	unsigned carry = 0;
	for (size_t i = 0; i < 32; i++) {
		carry += (unsigned)a[i] + b[i];
		out[i] = (uint8_t)carry;
		carry >>= 8;
	}
}

int adb_spake2_start(struct adb_spake2 *s, int server, const uint8_t *password,
		     size_t length, const uint8_t random[64])
{
	if (!length)
		return -1;
	memset(s, 0, sizeof(*s));
	s->server = !!server;
	crypto_eddsa_reduce(s->private_scalar, random);
	unsigned carry = 0;
	for (size_t i = 0; i < 32; i++) {
		unsigned v = (unsigned)s->private_scalar[i] * 8 + carry;
		s->private_scalar[i] = (uint8_t)v;
		carry = v >> 8;
	}
	mbedtls_sha512(password, length, s->password_hash, 0);
	crypto_eddsa_reduce(s->password_scalar, s->password_hash);
	uint8_t multiple[32], candidate[32];
	memcpy(multiple, order, 32);
	for (int bit = 0; bit < 3; bit++) {
		scalar_add(candidate, s->password_scalar, multiple);
		unsigned select = 0u - ((s->password_scalar[0] >> bit) & 1u);
		for (size_t i = 0; i < 32; i++)
			s->password_scalar[i] =
				(uint8_t)((s->password_scalar[i] & ~select) |
					  (candidate[i] & select));
		scalar_add(multiple, multiple, multiple);
	}
	ge mask, masked, public_point;
	int result = point_decode(&mask, server ? mask_n : mask_m);
	if (!result) {
		point_multiply(&masked, &mask, s->password_scalar);
		ge_scalarmult_base(&public_point, s->private_scalar);
		point_add(&mask, &public_point, &masked);
		ge_tobytes(s->message, &mask);
	}
	crypto_wipe(multiple, 32);
	crypto_wipe(candidate, 32);
	crypto_wipe(&mask, sizeof(mask));
	crypto_wipe(&masked, sizeof(masked));
	crypto_wipe(&public_point, sizeof(public_point));
	return result;
}

static void transcript_part(mbedtls_sha512_context *hash, const void *data,
			    size_t length)
{
	uint8_t prefix[8];
	for (int i = 0; i < 8; i++)
		prefix[i] = (uint8_t)((uint64_t)length >> (i * 8));
	mbedtls_sha512_update(hash, prefix, 8);
	mbedtls_sha512_update(hash, data, length);
}

int adb_spake2_finish(struct adb_spake2 *s, const uint8_t peer[32],
		      uint8_t key[64])
{
	ge point, mask, masked, public_point, shared;
	uint8_t encoded[32];
	int result = -1;
	if (point_decode(&point, peer) ||
	    point_decode(&mask, s->server ? mask_m : mask_n))
		goto done;
	point_multiply(&masked, &mask, s->password_scalar);
	fe_neg(masked.X, masked.X);
	fe_neg(masked.T, masked.T);
	point_add(&public_point, &point, &masked);
	point_multiply(&shared, &public_point, s->private_scalar);
	ge_tobytes(encoded, &shared);
	mbedtls_sha512_context hash;
	mbedtls_sha512_init(&hash);
	mbedtls_sha512_starts(&hash, 0);
	transcript_part(&hash, "adb pair client", sizeof("adb pair client"));
	transcript_part(&hash, "adb pair server", sizeof("adb pair server"));
	transcript_part(&hash, s->server ? peer : s->message, 32);
	transcript_part(&hash, s->server ? s->message : peer, 32);
	transcript_part(&hash, encoded, 32);
	transcript_part(&hash, s->password_hash, 64);
	result = mbedtls_sha512_finish(&hash, key);
	mbedtls_sha512_free(&hash);
done:
	crypto_wipe(&point, sizeof(point));
	crypto_wipe(&mask, sizeof(mask));
	crypto_wipe(&masked, sizeof(masked));
	crypto_wipe(&public_point, sizeof(public_point));
	crypto_wipe(&shared, sizeof(shared));
	crypto_wipe(encoded, sizeof(encoded));
	crypto_wipe(s, sizeof(*s));
	return result;
}

int adb_pairing_crypt(int encrypt, const uint8_t material[64],
		      const uint8_t *input, size_t length, uint8_t *output)
{
	static const uint8_t info[] = "adb pairing_auth aes-128-gcm key";
	uint8_t key[16], nonce[12] = { 0 };
	mbedtls_gcm_context gcm;
	mbedtls_gcm_init(&gcm);
	int result =
		mbedtls_hkdf(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), NULL,
			     0, material, 64, info, sizeof(info) - 1, key, 16);
	if (!result)
		result = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key,
					    128);
	if (!result && encrypt)
		result = mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT,
						   length, nonce, 12, NULL, 0,
						   input, output, 16,
						   output + length);
	if (!result && !encrypt)
		result = length < 16 ?
				 -1 :
				 mbedtls_gcm_auth_decrypt(&gcm, length - 16,
							  nonce, 12, NULL, 0,
							  input + length - 16,
							  16, input, output);
	mbedtls_gcm_free(&gcm);
	crypto_wipe(key, sizeof(key));
	return result;
}
