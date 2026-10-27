/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MS912X_ADB_SPAKE2_H
#define MS912X_ADB_SPAKE2_H
#include <stddef.h>
#include <stdint.h>
struct adb_spake2 {
	uint8_t private_scalar[32], password_scalar[32], password_hash[64],
		message[32];
	int server;
};
/* Injected randomness lets reference vectors exercise the production algorithm. */
int adb_spake2_start(struct adb_spake2 *, int, const uint8_t *, size_t,
		     const uint8_t random[64]);
int adb_spake2_finish(struct adb_spake2 *, const uint8_t peer[32],
		      uint8_t key[64]);
int adb_pairing_crypt(int, const uint8_t material[64], const uint8_t *, size_t,
		      uint8_t *);
#endif
