/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MS912X_ADB_H
#define MS912X_ADB_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/pk.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#define ADB_MAX_PAYLOAD 65536u
#define ADB_MAX_HELPER (2u * 1024u * 1024u)
#define ADB_MAX_ENVELOPE 2048u
#define ADB_MAX_OUTPUT 65536u
#define ADB_CLIENT_NAME "ms912x@android"
#define ADB_BACKEND_VERSION "ms912x C ADB 1; Mbed TLS 3.6.7; Monocypher 4.0.2"
struct adb_operation {
	const atomic_bool *cancel;
	int64_t deadline_ms;
	const char *error;
};
int64_t adb_now_ms(void);
void adb_operation_init(struct adb_operation *, const atomic_bool *,
			int timeout_ms);
int adb_check(struct adb_operation *);
int adb_fail(struct adb_operation *, const char *);
bool adb_numeric_address(const char *);
bool adb_pairing_code_valid(const char *);
bool adb_device_valid(const char *);
int adb_utf8_next(const unsigned char **, uint32_t *);
bool adb_digest_valid(const char *);
void adb_wipe(void *, size_t);
void adb_sha256_hex(const void *, size_t, char digest[65]);
/* Loaded PEM contains length heap-allocated bytes, with no NUL required.
 * The identity loader consumes and wipes it on success and failure. */
struct adb_store {
	void *context;
	int (*load_key)(void *, uint8_t **, size_t *);
	int (*save_key)(void *, const uint8_t *, size_t);
	int (*save_device)(void *, const char *, const char *, const char *);
	int (*has_device)(void *, const char *);
};
struct adb_identity {
	mbedtls_entropy_context entropy;
	mbedtls_ctr_drbg_context rng;
	mbedtls_pk_context key;
	mbedtls_x509_crt cert;
};
void adb_identity_init(struct adb_identity *);
int adb_identity_load(struct adb_identity *, const struct adb_store *,
		      bool create, struct adb_operation *);
void adb_identity_free(struct adb_identity *);
int adb_public_key(struct adb_identity *, char output[1024], size_t *);
int adb_sign_token(struct adb_identity *, const uint8_t token[20],
		   uint8_t signature[256]);
/* One connection and its sequential streams belong exclusively to one worker. */
struct adb_connection {
	int fd;
	bool tls, failed;
	uint64_t received_bytes;
	uint32_t version, max_payload, next_id;
	struct adb_operation *op;
	mbedtls_ssl_context ssl;
	mbedtls_ssl_config config;
	uint8_t *tx_buffer;
	struct adb_packet *rx_packet;
};
void adb_connection_init(struct adb_connection *, struct adb_operation *);
int adb_connect_socket(struct adb_connection *, const char *, uint16_t);
int adb_start_tls(struct adb_connection *, struct adb_identity *);
int adb_read(struct adb_connection *, void *, size_t);
int adb_write(struct adb_connection *, const void *, size_t);
void adb_connection_free(struct adb_connection *);
#define ADB_COMMAND(a, b, c, d)                                                \
	((uint32_t)(a) | (uint32_t)(b) << 8 | (uint32_t)(c) << 16 |            \
	 (uint32_t)(d) << 24)
#define ADB_CNXN ADB_COMMAND('C', 'N', 'X', 'N')
#define ADB_AUTH ADB_COMMAND('A', 'U', 'T', 'H')
#define ADB_STLS ADB_COMMAND('S', 'T', 'L', 'S')
#define ADB_OPEN ADB_COMMAND('O', 'P', 'E', 'N')
#define ADB_OKAY ADB_COMMAND('O', 'K', 'A', 'Y')
#define ADB_WRTE ADB_COMMAND('W', 'R', 'T', 'E')
#define ADB_CLSE ADB_COMMAND('C', 'L', 'S', 'E')
uint32_t adb_get_le32(const uint8_t *);
void adb_put_le32(uint8_t *, uint32_t);
struct adb_packet {
	uint32_t command, arg0, arg1, length;
	uint8_t data[ADB_MAX_PAYLOAD];
};
struct adb_buffer {
	const void *data;
	size_t size;
};
int adb_packet_send(struct adb_connection *, uint32_t, uint32_t, uint32_t,
		    const void *, size_t);
int adb_packet_sendv(struct adb_connection *, uint32_t, uint32_t, uint32_t,
		     const struct adb_buffer *, size_t);
int adb_packet_receive(struct adb_connection *, struct adb_packet *);
/* Borrowed connection-owned packet, valid until the next call or release. */
const struct adb_packet *adb_packet_next(struct adb_connection *);
void adb_packet_release(struct adb_connection *);
int adb_handshake(struct adb_connection *, struct adb_identity *,
		  bool wireless);
int adb_pair(struct adb_connection *, struct adb_identity *, const char *,
	     char device[256], char fingerprint[96]);
struct adb_stream {
	struct adb_connection *connection;
	uint32_t local, remote;
	bool closed, close_sent;
	bool failed;
	uint64_t consumed_bytes;
	size_t offset, available;
	uint8_t data[ADB_MAX_PAYLOAD];
};
int adb_stream_open(struct adb_stream *, struct adb_connection *, const char *);
int adb_stream_write(struct adb_stream *, const void *, size_t);
int adb_stream_writev(struct adb_stream *, const struct adb_buffer *, size_t);
int adb_stream_read(struct adb_stream *, void *, size_t);
int adb_stream_close(struct adb_stream *);
struct adb_shell {
	struct adb_stream stream;
	size_t output_bytes;
	bool exited;
	bool failed;
	uint8_t exit_code;
};
int adb_shell_open(struct adb_shell *, struct adb_connection *, const char *);
int adb_shell_stdin(struct adb_shell *, const void *, size_t);
int adb_shell_close_stdin(struct adb_shell *);
/* Returns 1 for output, 0 for exit, -1 for malformed/disconnected/limited output. */
int adb_shell_next(struct adb_shell *, uint8_t *, void *, size_t, size_t *,
		   size_t limit);
int adb_shell_execute(struct adb_connection *, const char *, char *, size_t);
int adb_sync_push(struct adb_connection *, const char *, const uint8_t *,
		  size_t);
typedef void (*adb_event_fn)(void *, const char *);
int adb_helper_run(struct adb_operation *, const struct adb_store *,
		   const char *, const char *, uint16_t, const uint8_t *,
		   size_t, const char *, uint8_t *, size_t, adb_event_fn,
		   void *);
#endif
