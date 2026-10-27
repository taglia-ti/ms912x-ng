/* SPDX-License-Identifier: GPL-2.0-only */
#define _POSIX_C_SOURCE 200809L
#include "adb.h"
#include "adb_spake2.h"
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <mbedtls/net_sockets.h>

/* Independent wire fixture. Production owns the complete client; only the
 * remote adbd is simulated. Never call client serializers on this side. */
struct peer {
	int fd;
	mbedtls_ssl_context ssl;
	mbedtls_ssl_config config;
	bool tls, late_closes;
	unsigned cancel_fragment, wrte_count;
	struct adb_identity *identity;
	uint32_t remote;
	uint8_t data[ADB_MAX_PAYLOAD];
	size_t position, size;
	atomic_bool *cancel;
	atomic_bool *launched;
	const uint8_t *helper;
	size_t helper_size;
	const char *digest;
};
static uint32_t get32(const uint8_t *p)
{
	return (uint32_t)p[0] + ((uint32_t)p[1] << 8) + ((uint32_t)p[2] << 16) +
	       ((uint32_t)p[3] << 24);
}
static void put32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
}
static int tls_send(void *context, const unsigned char *p, size_t n)
{
	struct peer *peer = context;
	ssize_t result = send(peer->fd, p, n, MSG_NOSIGNAL);
	return result >= 0 ? (int)result : MBEDTLS_ERR_NET_SEND_FAILED;
}
static int tls_recv(void *context, unsigned char *p, size_t n)
{
	struct peer *peer = context;
	ssize_t result = recv(peer->fd, p, n, 0);
	return result >= 0 ? (int)result : MBEDTLS_ERR_NET_RECV_FAILED;
}
static void transfer(struct peer *p, void *data, size_t n, bool write)
{
	uint8_t *bytes = data;
	while (n) {
		int count =
			p->tls ? (write ? mbedtls_ssl_write(&p->ssl, bytes, n) :
					  mbedtls_ssl_read(&p->ssl, bytes, n)) :
				 (write ? tls_send(p, bytes, n) :
					  tls_recv(p, bytes, n));
		if (count == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET)
			continue;
		assert(count > 0);
		bytes += count;
		n -= (size_t)count;
	}
}
static void send_packet(struct peer *p, const char command[4], uint32_t a,
			uint32_t b, const void *data, size_t size)
{
	uint8_t header[24] = { 0 };
	memcpy(header, command, 4);
	put32(header + 4, a);
	put32(header + 8, b);
	put32(header + 12, (uint32_t)size);
	for (int i = 0; i < 4; i++)
		header[20 + i] = header[i] ^ 255;
	/* Split the header so the client cannot assume one recv is one packet. */
	transfer(p, header, 7, true);
	transfer(p, header + 7, 17, true);
	if (size)
		transfer(p, (void *)data, size, true);
}
static size_t receive_packet(struct peer *p, const char expected[4],
			     uint32_t *a, uint32_t *b, uint8_t *out)
{
	uint8_t header[24];
	transfer(p, header, 24, false);
	assert(!memcmp(header, expected, 4));
	for (int i = 0; i < 4; i++)
		assert((header[i] ^ header[20 + i]) == 255);
	*a = get32(header + 4);
	*b = get32(header + 8);
	size_t n = get32(header + 12);
	if (!memcmp(header, "WRTE", 4))
		p->wrte_count++;
	assert(n <= ADB_MAX_PAYLOAD);
	if (n)
		transfer(p, out, n, false);
	return n;
}
static void receive_empty(struct peer *p, const char command[4])
{
	uint32_t a, b;
	assert(!receive_packet(p, command, &a, &b, p->data));
	assert(a == p->remote && b == 42);
}
static void open_stream(struct peer *p, const char *service)
{
	uint32_t a, b;
	size_t n = receive_packet(p, "OPEN", &a, &b, p->data);
	assert(b == 0 && n == strlen(service) + 1 &&
	       !memcmp(p->data, service, n));
	p->remote = a;
	send_packet(p, "OKAY", 42, a, NULL, 0);
	p->position = p->size = 0;
}
static void get_bytes(struct peer *p, void *out, size_t size)
{
	uint8_t *bytes = out;
	while (size) {
		if (p->position == p->size) {
			uint32_t a, b;
			p->size = receive_packet(p, "WRTE", &a, &b, p->data);
			p->position = 0;
			assert(a == p->remote && b == 42 && p->size);
			send_packet(p, "OKAY", 42, a, NULL, 0);
		}
		size_t n = p->size - p->position;
		if (n > size)
			n = size;
		memcpy(bytes, p->data + p->position, n);
		p->position += n;
		bytes += n;
		size -= n;
	}
}
static void put_bytes(struct peer *p, const void *data, size_t size)
{
	send_packet(p, "WRTE", 42, p->remote, data, size);
	receive_empty(p, "OKAY");
}
static void close_stream(struct peer *p)
{
	receive_empty(p, "CLSE");
	send_packet(p, "CLSE", 42, p->remote, NULL, 0);
	/* A simultaneous adbd/client close may produce a second reply for the
     * retired local ID before the next service's OPEN/OKAY exchange. */
	if (p->late_closes)
		send_packet(p, "CLSE", 0, p->remote, NULL, 0);
}
static void shell_output(struct peer *p, const char *text)
{
	uint8_t frame[1024];
	size_t length = strlen(text);
	assert(length + 11 <= sizeof(frame));
	frame[0] = 1;
	put32(frame + 1, (uint32_t)length);
	memcpy(frame + 5, text, length);
	frame[5 + length] = 3;
	put32(frame + 6 + length, 1);
	frame[10 + length] = 0;
	/* Split a shell header across ADB WRTE packets and combine stdout/exit. */
	put_bytes(p, frame, 3);
	put_bytes(p, frame + 3, length + 8);
}
static void shell_command(struct peer *p, const char *command, const char *text)
{
	char service[1024];
	snprintf(service, sizeof(service), "shell,v2,raw:%s", command);
	open_stream(p, service);
	uint8_t eof[5];
	get_bytes(p, eof, 5);
	assert(eof[0] == 4 && !get32(eof + 1));
	shell_output(p, text);
	close_stream(p);
}
static void server_handshake(struct peer *p)
{
	uint32_t version, max;
	size_t n = receive_packet(p, "CNXN", &version, &max, p->data);
	assert(version == 0x01000001 && max == ADB_MAX_PAYLOAD && n > 1);
	send_packet(p, "STLS", 0x01000000, 0, NULL, 0);
	assert(!receive_packet(p, "STLS", &version, &max, p->data));
	assert(version == 0x01000000 && !max);
}
static void server_tls(struct peer *p)
{
	mbedtls_ssl_init(&p->ssl);
	mbedtls_ssl_config_init(&p->config);
	assert(!mbedtls_ssl_config_defaults(&p->config, MBEDTLS_SSL_IS_SERVER,
					    MBEDTLS_SSL_TRANSPORT_STREAM,
					    MBEDTLS_SSL_PRESET_DEFAULT));
	mbedtls_ssl_conf_min_tls_version(&p->config,
					 MBEDTLS_SSL_VERSION_TLS1_3);
	mbedtls_ssl_conf_max_tls_version(&p->config,
					 MBEDTLS_SSL_VERSION_TLS1_3);
	mbedtls_ssl_conf_rng(&p->config, mbedtls_ctr_drbg_random,
			     &p->identity->rng);
	mbedtls_ssl_conf_authmode(&p->config, MBEDTLS_SSL_VERIFY_OPTIONAL);
	assert(!mbedtls_ssl_conf_own_cert(&p->config, &p->identity->cert,
					  &p->identity->key));
	assert(!mbedtls_ssl_setup(&p->ssl, &p->config));
	mbedtls_ssl_set_bio(&p->ssl, p, tls_send, tls_recv, NULL);
	int result = mbedtls_ssl_handshake(&p->ssl);
	if (result)
		fprintf(stderr, "Server TLS failure: -0x%x\n", -result);
	assert(!result);
	assert(mbedtls_ssl_get_peer_cert(&p->ssl));
	p->tls = true;
}
static void free_peer(struct peer *p)
{
	close(p->fd);
	if (p->tls) {
		mbedtls_ssl_free(&p->ssl);
		mbedtls_ssl_config_free(&p->config);
	}
}
static uint8_t *pem;
static size_t pem_size;
static int load(void *unused, uint8_t **out, size_t *size)
{
	(void)unused;
	*size = pem_size;
	*out = NULL;
	if (pem) {
		*out = calloc(pem_size + 1, 1);
		assert(*out);
		memcpy(*out, pem, pem_size);
	}
	return 0;
}
static int save(void *unused, const uint8_t *data, size_t size)
{
	(void)unused;
	pem = malloc(size);
	assert(pem);
	memcpy(pem, data, size);
	pem_size = size;
	return 0;
}
static int paired(void *unused, const char *device)
{
	(void)unused;
	return !strcmp(device, "fixture-phone");
}
static void pairing_packet(struct peer *p, bool write, uint8_t type,
			   uint8_t *data, size_t length)
{
	uint8_t header[6] = {
		1, type, 0, 0, (uint8_t)(length >> 8), (uint8_t)length
	};
	if (write) {
		transfer(p, header, 6, true);
		transfer(p, data, length, true);
	} else {
		uint8_t received[6];
		transfer(p, received, 6, false);
		assert(!memcmp(received, header, 6));
		transfer(p, data, length, false);
	}
}
static void *pairing_server(void *context)
{
	struct peer *p = context;
	server_tls(p);
	uint8_t password[70], random[64] = { 42 }, client[32], key[64],
			      info[8192], encrypted[8208];
	memcpy(password, "123456", 6);
	assert(!mbedtls_ssl_export_keying_material(
		&p->ssl, password + 6, 64, "adb-label", sizeof("adb-label"),
		NULL, 0, 0));
	struct adb_spake2 s;
	assert(!adb_spake2_start(&s, 1, password, 70, random));
	pairing_packet(p, false, 0, client, 32);
	pairing_packet(p, true, 0, s.message, 32);
	assert(!adb_spake2_finish(&s, client, key));
	pairing_packet(p, false, 1, encrypted, sizeof(encrypted));
	assert(!adb_pairing_crypt(0, key, encrypted, sizeof(encrypted), info));
	assert(info[0] == 0 && strstr((char *)info + 1, ADB_CLIENT_NAME));
	memset(info, 0, sizeof(info));
	info[0] = 1;
	memcpy(info + 1, "fixture-phone", 14);
	assert(!adb_pairing_crypt(1, key, info, sizeof(info), encrypted));
	pairing_packet(p, true, 1, encrypted, sizeof(encrypted));
	free_peer(p);
	return NULL;
}
static void *helper_server(void *context)
{
	struct peer *p = context;
	int accepted = accept(p->fd, NULL, NULL);
	assert(accepted >= 0);
	close(p->fd);
	p->fd = accepted;
	server_handshake(p);
	server_tls(p);
	const char banner[] = "device::features=shell_v2,cmd";
	send_packet(p, "CNXN", 0x01000001, 4096, banner, sizeof(banner));
	shell_command(p, "id -u", "2000\n");
	open_stream(p, "sync:");
	p->wrte_count = 0;
	uint8_t header[8], chunk[65536];
	get_bytes(p, header, 8);
	assert(!memcmp(header, "SEND", 4));
	size_t length = get32(header + 4);
	assert(length < 512);
	char path[512] = { 0 };
	get_bytes(p, path, length);
	char expected[512];
	snprintf(expected, sizeof(expected),
		 "/data/local/tmp/ms912x-helper-%s.jar,384", p->digest);
	assert(!strcmp(path, expected));
	size_t transferred = 0;
	for (;;) {
		get_bytes(p, header, 8);
		length = get32(header + 4);
		if (!memcmp(header, "DONE", 4))
			break;
		assert(!memcmp(header, "DATA", 4) && length <= sizeof(chunk) &&
		       length <= p->helper_size - transferred);
		get_bytes(p, chunk, length);
		assert(!memcmp(chunk, p->helper + transferred, length));
		transferred += length;
	}
	assert(transferred == p->helper_size);
	assert(p->wrte_count ==
	       39); /* SEND + 37 framed DATA + DONE at 4 KiB. */
	uint8_t okay[8] = { 'O', 'K', 'A', 'Y', 0, 0, 0, 0 };
	put_bytes(p, okay, 8);
	close_stream(p);
	snprintf(path, sizeof(path),
		 "sha256sum '/data/local/tmp/ms912x-helper-%s.jar'", p->digest);
	snprintf(expected, sizeof(expected), "%s  helper.jar\n", p->digest);
	shell_command(p, path, expected);
	snprintf(
		path, sizeof(path),
		"shell,v2,raw:CLASSPATH='/data/local/tmp/ms912x-helper-%s.jar' app_process / --nice-name=ms912x-helper "
		"io.github.taglia_ti.ms912x_ng.helper.HelperMain --bootstrap-stdin",
		p->digest);
	open_stream(p, path);
	uint8_t bootstrap[32];
	get_bytes(p, bootstrap, 5);
	assert(bootstrap[0] == 0 && get32(bootstrap + 1) == 15);
	get_bytes(p, bootstrap, 15);
	assert(!memcmp(bootstrap, "secret envelope", 15));
	int64_t deadline = adb_now_ms() + 3000;
	while (!atomic_load(p->launched)) {
		assert(adb_now_ms() < deadline);
		struct timespec pause = { .tv_nsec = 1000000 };
		nanosleep(&pause, NULL);
	}
	if (p->cancel_fragment == 1) {
		/* Two acknowledged packets prove the client consumed the first
		 * fragment of a shell header before cancellation. */
		put_bytes(p, "\1\4", 2);
		put_bytes(p, "\0", 1);
	} else if (p->cancel_fragment == 2) {
		const uint8_t frame[] = { 1, 4, 0, 0, 0, 'A' };
		put_bytes(p, frame, sizeof(frame));
		put_bytes(p, "B", 1);
	}
	if (p->cancel_fragment == 4) {
		const uint8_t invalid[] = { 9, 0, 0, 0, 0 };
		put_bytes(p, invalid, sizeof(invalid));
		assert(mbedtls_ssl_read(&p->ssl, bootstrap, sizeof(bootstrap)) <= 0);
		free_peer(p);
		return NULL;
	}
	atomic_store(p->cancel, true);
	if (p->cancel_fragment == 1 || p->cancel_fragment == 2) {
		/* Corrupt framing must cause connection close, never a new WRTE
		 * interpreted using the discarded header/payload cursor. */
		assert(mbedtls_ssl_read(&p->ssl, bootstrap,
					sizeof(bootstrap)) <= 0);
	} else {
		get_bytes(p, bootstrap, 5);
		assert(bootstrap[0] == 4 && !get32(bootstrap + 1));
		if (p->cancel_fragment == 3)
			assert(mbedtls_ssl_read(&p->ssl, bootstrap,
						sizeof(bootstrap)) <= 0);
		else
			shell_output(p, "private helper output");
	}
	free_peer(p);
	return NULL;
}
struct events {
	unsigned count;
	atomic_bool launched;
};
static void event(void *context, const char *message)
{
	struct events *events = context;
	assert(!strstr(message, "private") && !strstr(message, "secret"));
	events->count++;
	if (strstr(message, "Helper launched"))
		atomic_store(&events->launched, true);
}
static void tls_and_helper(void)
{
	struct adb_operation op;
	adb_operation_init(&op, NULL, 120000);
	struct adb_store store = { .load_key = load,
				   .save_key = save,
				   .has_device = paired };
	struct adb_identity client, server;
	adb_identity_init(&client);
	adb_identity_init(&server);
	assert(!adb_identity_load(&client, &store, true, &op));
	assert(!adb_identity_load(&server, &store, false, &op));
	int sockets[2];
	assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, sockets));
	struct peer p = { .fd = sockets[1], .identity = &server };
	pthread_t thread;
	assert(!pthread_create(&thread, NULL, pairing_server, &p));
	struct adb_connection c;
	adb_connection_init(&c, &op);
	c.fd = sockets[0];
	assert(!fcntl(c.fd, F_SETFL, O_NONBLOCK));
	char device[256], fingerprint[96];
	int result = adb_pair(&c, &client, "123456", device, fingerprint);
	if (result)
		fprintf(stderr, "Pairing failure: %s\n", op.error);
	assert(!result && !strcmp(device, "fixture-phone") &&
	       strlen(fingerprint) == 95);
	adb_connection_free(&c);
	pthread_join(thread, NULL);
	for (unsigned mode = 0; mode < 5; mode++) {
		int listener = socket(AF_INET, SOCK_STREAM, 0);
		assert(listener >= 0);
		struct sockaddr_in address = {
			.sin_family = AF_INET,
			.sin_addr = { .s_addr = htonl(INADDR_LOOPBACK) }
		};
		assert(!bind(listener, (struct sockaddr *)&address,
			     sizeof(address)));
		assert(!listen(listener, 1));
		socklen_t size = sizeof(address);
		assert(!getsockname(listener, (struct sockaddr *)&address,
				    &size));
		uint8_t *helper = malloc(150000);
		assert(helper);
		for (size_t i = 0; i < 150000; i++)
			helper[i] = (uint8_t)i;
		char digest[65];
		adb_sha256_hex(helper, 150000, digest);
		atomic_bool cancel = false;
		struct events events = { .count = 0 };
		atomic_init(&events.launched, false);
		p = (struct peer){ .fd = listener,
				   .cancel_fragment = mode,
				   .identity = &server,
				   .cancel = &cancel,
				   .launched = &events.launched,
				   .helper = helper,
				   .helper_size = 150000,
				   .late_closes = true,
				   .digest = digest };
		assert(!pthread_create(&thread, NULL, helper_server, &p));
		adb_operation_init(&op, &cancel, 10000);
		uint8_t envelope[] = "secret envelope";
		int64_t started = adb_now_ms();
		result = adb_helper_run(&op, &store, "fixture-phone",
					"127.0.0.1", ntohs(address.sin_port),
					helper, 150000, digest, envelope, 15,
					event, &events);
		if (result && mode != 4)
			fprintf(stderr, "Helper failure: %s\n", op.error);
		if (mode == 4)
			assert(result && events.count == 5 &&
			       op.error_kind == ADB_ERROR_FAILURE &&
			       strstr(op.error, "Invalid ADB shell frame"));
		else
			assert(!result && events.count == 4);
		if (mode == 3)
			assert(adb_now_ms() - started >= 3500);
		for (size_t i = 0; i < 15; i++)
			assert(envelope[i] == 0);
		pthread_join(thread, NULL);
		free(helper);
	}
	adb_identity_free(&client);
	adb_identity_free(&server);
	adb_wipe(pem, pem_size);
	free(pem);
	pem = NULL;
}
static void malformed_and_deadlines(void)
{
	for (int test = 0; test < 4; test++) {
		int fds[2];
		assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, fds));
		struct adb_operation op;
		atomic_bool cancel = false;
		adb_operation_init(&op, &cancel, 20);
		struct adb_connection c;
		adb_connection_init(&c, &op);
		c.fd = fds[0];
		assert(!fcntl(c.fd, F_SETFL, O_NONBLOCK));
		uint8_t header[24] = { 0 };
		memcpy(header, "WRTE", 4);
		for (int i = 0; i < 4; i++)
			header[20 + i] = header[i] ^ 255;
		if (test == 0) {
			put32(header + 12, ADB_MAX_PAYLOAD + 1);
			assert(send(fds[1], header, 24, MSG_NOSIGNAL) == 24);
		}
		if (test == 1) {
			header[20] ^= 1;
			assert(send(fds[1], header, 24, MSG_NOSIGNAL) == 24);
		}
		if (test == 3)
			atomic_store(&cancel, true);
		struct adb_packet packet;
		assert(adb_packet_receive(&c, &packet));
		assert(op.error);
		if (test == 2)
			assert(strstr(op.error, "timed out"));
		if (test == 3)
			assert(strstr(op.error, "cancelled"));
		adb_connection_free(&c);
		close(fds[1]);
	}
}
static void *auth_and_flow_server(void *context)
{
	struct peer *p = context;
	uint32_t a, b;
	assert(receive_packet(p, "CNXN", &a, &b, p->data) > 1);
	uint8_t token[20] = { 42 };
	send_packet(p, "AUTH", 1, 0, token, 20);
	assert(receive_packet(p, "AUTH", &a, &b, p->data) == 256 && a == 2 &&
	       !b);
	assert(!mbedtls_pk_verify(&p->identity->key, MBEDTLS_MD_SHA1, token, 20,
				  p->data, 256));
	send_packet(p, "AUTH", 1, 0, token, 20);
	size_t n = receive_packet(p, "AUTH", &a, &b, p->data);
	assert(a == 3 && !b && n > 700 && p->data[n - 1] == 0 &&
	       strstr((char *)p->data, ADB_CLIENT_NAME));
	const char banner[] = "device::features=shell_v2";
	send_packet(p, "CNXN", 0x01000001, 4096, banner, sizeof(banner));
	open_stream(p, "shell,v2,raw:flow");
	n = receive_packet(p, "WRTE", &a, &b, p->data);
	assert(n == 8 && !memcmp(p->data, "outgoing", 8));
	/* Output arrives before acknowledgement of the client's stdin write. */
	send_packet(p, "WRTE", 42, p->remote, "incoming", 8);
	receive_empty(p, "OKAY");
	send_packet(p, "OKAY", 42, p->remote, NULL, 0);
	close_stream(p);
	free_peer(p);
	return NULL;
}
static void *invalid_shell_server(void *context)
{
	struct peer *p = context;
	open_stream(p, "shell,v2,raw:invalid");
	uint8_t header[5] = { 1, 0, 0, 0, 0 };
	put32(header + 1, ADB_MAX_OUTPUT + 1);
	put_bytes(p, header, 5);
	free_peer(p);
	return NULL;
}
static void auth_and_flow(void)
{
	struct adb_operation op;
	adb_operation_init(&op, NULL, 120000);
	struct adb_store store = { .load_key = load, .save_key = save };
	struct adb_identity identity;
	adb_identity_init(&identity);
	assert(!adb_identity_load(&identity, &store, true, &op));
	int fds[2];
	assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, fds));
	struct peer p = { .fd = fds[1], .identity = &identity };
	pthread_t worker;
	assert(!pthread_create(&worker, NULL, auth_and_flow_server, &p));
	struct adb_connection c;
	adb_connection_init(&c, &op);
	c.fd = fds[0];
	assert(!fcntl(c.fd, F_SETFL, O_NONBLOCK));
	assert(!adb_handshake(&c, &identity, false));
	struct adb_stream stream;
	assert(!adb_stream_open(&stream, &c, "shell,v2,raw:flow"));
	assert(!adb_stream_write(&stream, "outgoing", 8));
	char output[8];
	assert(!adb_stream_read(&stream, output, 8) &&
	       !memcmp(output, "incoming", 8));
	assert(!adb_stream_close(&stream));
	adb_connection_free(&c);
	pthread_join(worker, NULL);
	adb_identity_free(&identity);
	adb_wipe(pem, pem_size);
	free(pem);
	pem = NULL;
	assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, fds));
	p = (struct peer){ .fd = fds[1] };
	assert(!pthread_create(&worker, NULL, invalid_shell_server, &p));
	adb_operation_init(&op, NULL, 1000);
	adb_connection_init(&c, &op);
	c.fd = fds[0];
	struct adb_shell shell;
	assert(!adb_shell_open(&shell, &c, "invalid"));
	uint8_t channel;
	size_t length;
	assert(adb_shell_next(&shell, &channel, output, 8, &length,
			      ADB_MAX_OUTPUT) < 0);
	assert(strstr(op.error, "limit"));
	adb_connection_free(&c);
	pthread_join(worker, NULL);
}
int main(void)
{
	malformed_and_deadlines();
	auth_and_flow();
	tls_and_helper();
	puts("C ADB integration: TLS 1.3 pairing/exporter, STLS, fragmented Shell v2, 150k Sync, bootstrap, cancellation and EOF passed");
	return 0;
}
