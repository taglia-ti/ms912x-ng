/* SPDX-License-Identifier: GPL-2.0-only */
#include "adb.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include <mbedtls/net_sockets.h>

static int wait_socket(struct adb_connection *c, short events)
{
	for (;;) {
		if (adb_check(c->op))
			return -1;
		int timeout = 25;
		if (c->op->deadline_ms) {
			int64_t remaining = c->op->deadline_ms - adb_now_ms();
			if (remaining < timeout)
				timeout = remaining > 0 ? (int)remaining : 0;
		}
		struct pollfd pfd = { .fd = c->fd, .events = events };
		int result = poll(&pfd, 1, timeout);
		if (result > 0) {
			if (pfd.revents & events)
				return 0;
			c->failed = true;
			return adb_fail(c->op, "ADB connection disconnected");
		}
		if (result < 0 && errno != EINTR) {
			c->failed = true;
			return adb_fail(c->op, "ADB socket polling failed");
		}
	}
}
void adb_connection_init(struct adb_connection *c, struct adb_operation *op)
{
	memset(c, 0, sizeof(*c));
	c->fd = -1;
	c->op = op;
	c->version = 0x01000001;
	c->max_payload = ADB_MAX_PAYLOAD;
	c->next_id = 1;
	mbedtls_ssl_init(&c->ssl);
	mbedtls_ssl_config_init(&c->config);
}
void adb_connection_free(struct adb_connection *c)
{
	if (c->fd >= 0)
		close(c->fd);
	mbedtls_ssl_free(&c->ssl);
	mbedtls_ssl_config_free(&c->config);
	if (c->tx_buffer) {
		adb_wipe(c->tx_buffer, ADB_MAX_PAYLOAD + 24);
		free(c->tx_buffer);
		c->tx_buffer = NULL;
	}
	if (c->rx_packet) {
		adb_wipe(c->rx_packet, sizeof(*c->rx_packet));
		free(c->rx_packet);
		c->rx_packet = NULL;
	}
	c->fd = -1;
	c->tls = false;
}
int adb_connect_socket(struct adb_connection *c, const char *host,
		       uint16_t port)
{
	struct sockaddr_storage storage = { 0 };
	struct sockaddr_in *v4 = (struct sockaddr_in *)&storage;
	struct sockaddr_in6 *v6 = (struct sockaddr_in6 *)&storage;
	socklen_t size;
	if (inet_pton(AF_INET, host, &v4->sin_addr) == 1) {
		v4->sin_family = AF_INET;
		v4->sin_port = htons(port);
		size = sizeof(*v4);
	} else if (inet_pton(AF_INET6, host, &v6->sin6_addr) == 1) {
		v6->sin6_family = AF_INET6;
		v6->sin6_port = htons(port);
		size = sizeof(*v6);
	} else
		return adb_fail(c->op,
				"ADB endpoint requires a numeric IP address");
	if (adb_check(c->op))
		return -1;
	c->fd = socket(storage.ss_family, SOCK_STREAM, 0);
	if (c->fd < 0)
		return adb_fail(c->op, "Could not open ADB socket");
	int one = 1;
	if (fcntl(c->fd, F_SETFL, O_NONBLOCK) < 0 ||
	    fcntl(c->fd, F_SETFD, FD_CLOEXEC) < 0 ||
	    setsockopt(c->fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one)))
		return adb_fail(c->op, "Could not configure ADB socket");
	int result = connect(c->fd, (struct sockaddr *)&storage, size);
	if (result < 0 && errno != EINPROGRESS)
		return adb_fail(c->op, "Could not connect to ADB endpoint");
	if (result < 0) {
		if (wait_socket(c, POLLOUT))
			return -1;
		int error = 0;
		socklen_t length = sizeof(error);
		if (getsockopt(c->fd, SOL_SOCKET, SO_ERROR, &error, &length) ||
		    error)
			return adb_fail(c->op,
					"Could not connect to ADB endpoint");
	}
	return 0;
}
static int socket_send(void *context, const unsigned char *data, size_t size)
{
	struct adb_connection *c = context;
	ssize_t n = send(c->fd, data, size, MSG_NOSIGNAL);
	if (n >= 0)
		return (int)n;
	if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
		return MBEDTLS_ERR_SSL_WANT_WRITE;
	return MBEDTLS_ERR_NET_SEND_FAILED;
}
static int socket_receive(void *context, unsigned char *data, size_t size)
{
	struct adb_connection *c = context;
	ssize_t n = recv(c->fd, data, size, 0);
	if (n >= 0)
		return (int)n;
	if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
		return MBEDTLS_ERR_SSL_WANT_READ;
	return MBEDTLS_ERR_NET_RECV_FAILED;
}
static int retry_tls(struct adb_connection *c, int result)
{
	if (result == MBEDTLS_ERR_SSL_WANT_READ)
		return wait_socket(c, POLLIN);
	if (result == MBEDTLS_ERR_SSL_WANT_WRITE)
		return wait_socket(c, POLLOUT);
	if (result == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET)
		return 0;
	c->failed = true;
	return adb_fail(c->op, "ADB TLS or socket operation failed");
}
int adb_start_tls(struct adb_connection *c, struct adb_identity *id)
{
	if (c->tls)
		return adb_fail(c->op, "Duplicate ADB TLS upgrade");
	if (mbedtls_ssl_config_defaults(&c->config, MBEDTLS_SSL_IS_CLIENT,
					MBEDTLS_SSL_TRANSPORT_STREAM,
					MBEDTLS_SSL_PRESET_DEFAULT))
		return adb_fail(c->op, "Could not configure ADB TLS");
	/* SPAKE2 authenticates pairing. adbd authorizes the saved client RSA key
     * on reconnect, matching the previous provider's trust policy. TLS still
     * verifies CertificateVerify; public Web PKI cannot verify adbd's cert. */
	mbedtls_ssl_conf_authmode(&c->config, MBEDTLS_SSL_VERIFY_NONE);
	mbedtls_ssl_conf_min_tls_version(&c->config,
					 MBEDTLS_SSL_VERSION_TLS1_3);
	mbedtls_ssl_conf_max_tls_version(&c->config,
					 MBEDTLS_SSL_VERSION_TLS1_3);
	mbedtls_ssl_conf_rng(&c->config, mbedtls_ctr_drbg_random, &id->rng);
	if (mbedtls_ssl_conf_own_cert(&c->config, &id->cert, &id->key) ||
	    mbedtls_ssl_setup(&c->ssl, &c->config))
		return adb_fail(c->op, "Could not initialize ADB TLS");
	mbedtls_ssl_set_bio(&c->ssl, c, socket_send, socket_receive, NULL);
	for (;;) {
		if (adb_check(c->op))
			return -1;
		int result = mbedtls_ssl_handshake(&c->ssl);
		if (!result) {
			c->tls = true;
			return 0;
		}
		if (retry_tls(c, result))
			return -1;
	}
}
static int transfer(struct adb_connection *c, void *buffer, size_t length,
		    bool write)
{
	uint8_t *data = buffer;
	size_t requested = length;
	if (c->failed)
		return adb_fail(c->op, "ADB connection is no longer usable");
	while (length) {
		if (adb_check(c->op))
			goto failed;
		int n;
		if (c->tls)
			n = write ? mbedtls_ssl_write(&c->ssl, data, length) :
				    mbedtls_ssl_read(&c->ssl, data, length);
		else
			n = write ? socket_send(c, data, length) :
				    socket_receive(c, data, length);
		if (n > 0) {
			if (!write)
				c->received_bytes += (size_t)n;
			data += n;
			length -= (size_t)n;
			continue;
		}
		if (!n) {
			c->failed = true;
			adb_fail(c->op, "ADB connection disconnected");
			goto failed;
		}
		if (retry_tls(c, n))
			goto failed;
	}
	return 0;
failed:
	/* A cancelled TLS write can still own a pending record referencing its
	 * original buffer. Never reuse that context or replace the buffer. */
	if (write || length != requested)
		c->failed = true;
	return -1;
}
int adb_read(struct adb_connection *c, void *data, size_t length)
{
	return transfer(c, data, length, false);
}
int adb_write(struct adb_connection *c, const void *data, size_t length)
{
	return transfer(c, (void *)data, length, true);
}
