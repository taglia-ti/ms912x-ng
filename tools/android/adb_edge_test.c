/* SPDX-License-Identifier: GPL-2.0-only */
#define _POSIX_C_SOURCE 200809L
#include "adb.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

struct remote {
	int fd;
	const char *banner;
	size_t banner_size;
	bool vectors;
};
static void put32(uint8_t *p, uint32_t value)
{
	for (int i = 0; i < 4; i++)
		p[i] = (uint8_t)(value >> (8 * i));
}
static uint32_t get32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
	       (uint32_t)p[3] << 24;
}
static bool receive(int fd, void *out, size_t size)
{
	uint8_t *p = out;
	size_t used = 0;
	while (used < size) {
		ssize_t n = recv(fd, p + used, size - used, 0);
		if (!n && !used)
			return false;
		assert(n > 0);
		used += (size_t)n;
	}
	return true;
}
static void send_bytes(int fd, const void *data, size_t size)
{
	const uint8_t *p = data;
	while (size) {
		ssize_t n = send(fd, p, size, MSG_NOSIGNAL);
		assert(n > 0);
		p += n;
		size -= (size_t)n;
	}
}
static void send_packet(int fd, const char command[4], uint32_t a, uint32_t b,
			const void *data, size_t size)
{
	uint8_t header[24] = { 0 };
	memcpy(header, command, 4);
	put32(header + 4, a);
	put32(header + 8, b);
	put32(header + 12, (uint32_t)size);
	for (int i = 0; i < 4; i++)
		header[20 + i] = header[i] ^ 255;
	send_bytes(fd, header, 24);
	send_bytes(fd, data, size);
}
static bool packet(int fd, const char command[4], uint32_t *a, uint32_t *b,
		   uint8_t *data, size_t *size)
{
	uint8_t header[24];
	if (!receive(fd, header, 24))
		return false;
	assert(!memcmp(header, command, 4));
	*a = get32(header + 4);
	*b = get32(header + 8);
	*size = get32(header + 12);
	assert(*size <= 1024);
	assert(receive(fd, data, *size));
	return true;
}
static void connection(struct adb_connection *c, struct adb_operation *op,
		       int fds[2])
{
	assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, fds));
	adb_operation_init(op, NULL, 1000);
	adb_connection_init(c, op);
	c->fd = fds[0];
	assert(!fcntl(c->fd, F_SETFL, O_NONBLOCK));
}
static void *banner_server(void *context)
{
	struct remote *r = context;
	uint8_t data[1024];
	uint32_t a, b;
	size_t size;
	assert(packet(r->fd, "CNXN", &a, &b, data, &size));
	send_packet(r->fd, "CNXN", 0x01000001, 4096, r->banner, r->banner_size);
	close(r->fd);
	return NULL;
}
static void features(void)
{
	const struct {
		const char *text;
		size_t size;
		bool valid;
	} cases[] = {
		{ "device::features=cmd,shell_v2",
		  sizeof("device::features=cmd,shell_v2"), true },
		{ "recovery::product=x;features=shell_v2;model=y",
		  sizeof("recovery::product=x;features=shell_v2;model=y"),
		  true },
		{ "device::features=not_shell_v2",
		  sizeof("device::features=not_shell_v2"), false },
		{ "device::features=shell_v2_suffix",
		  sizeof("device::features=shell_v2_suffix"), false },
		{ "device::model=shell_v2;features=cmd",
		  sizeof("device::model=shell_v2;features=cmd"), false },
		{ "device::features=cmd\0features=shell_v2",
		  sizeof("device::features=cmd\0features=shell_v2"), false },
		{ "device::features=shell_v2",
		  sizeof("device::features=shell_v2") - 1, true },
	};
	for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++) {
		struct adb_connection c;
		struct adb_operation op;
		int fds[2];
		connection(&c, &op, fds);
		struct remote r = { .fd = fds[1],
				    .banner = cases[i].text,
				    .banner_size = cases[i].size };
		pthread_t worker;
		assert(!pthread_create(&worker, NULL, banner_server, &r));
		assert((adb_handshake(&c, NULL, false) == 0) == cases[i].valid);
		adb_connection_free(&c);
		pthread_join(worker, NULL);
	}
}
static void *stream_server(void *context)
{
	struct remote *r = context;
	uint8_t data[1024];
	uint32_t a, b;
	size_t size;
	assert(packet(r->fd, "OPEN", &a, &b, data, &size));
	uint32_t local = a;
	send_packet(r->fd, "OKAY", 42, local, NULL, 0);
	if (r->vectors) {
		size_t used = 0;
		while (used < 18) {
			assert(packet(r->fd, "WRTE", &a, &b, data, &size));
			assert(a == local && b == 42 &&
			       size == (18 - used > 7 ? 7 : 18 - used));
			assert(!memcmp(data, "abcdefghijklmnopqr" + used,
				       size));
			used += size;
			send_packet(r->fd, "OKAY", 42, local, NULL, 0);
		}
	} else {
		assert(packet(r->fd, "WRTE", &a, &b, data, &size));
		assert(size == 5 && data[0] == 4);
		send_packet(r->fd, "OKAY", 42, local, NULL, 0);
		const uint8_t frame[] = { 1,   7,   0,	 0,    0, '2',
					  '0', '0', '0', '\n', 0, 'x',
					  3,   1,   0,	 0,    0, 0 };
		send_packet(r->fd, "WRTE", 42, local, frame, sizeof(frame));
		assert(packet(r->fd, "OKAY", &a, &b, data, &size));
	}
	if (packet(r->fd, "CLSE", &a, &b, data, &size))
		send_packet(r->fd, "CLSE", 42, local, NULL, 0);
	close(r->fd);
	return NULL;
}
static void stream_edges(void)
{
	for (int vectors = 0; vectors < 2; vectors++) {
		struct adb_connection c;
		struct adb_operation op;
		int fds[2];
		connection(&c, &op, fds);
		struct remote r = { .fd = fds[1], .vectors = vectors };
		pthread_t worker;
		assert(!pthread_create(&worker, NULL, stream_server, &r));
		if (vectors) {
			c.max_payload = 7;
			struct adb_stream stream;
			assert(!adb_stream_open(&stream, &c, "x:"));
			const struct adb_buffer buffers[] = { { "abc", 3 },
							      { NULL, 0 },
							      { "defghijk", 8 },
							      { "lmnopqr",
								7 } };
			assert(!adb_stream_writev(&stream, buffers, 4));
			assert(!adb_stream_close(&stream));
		} else {
			char output[64];
			assert(adb_shell_execute(&c, "verify", output,
						 sizeof(output)));
			assert(strstr(op.error, "NUL byte"));
		}
		adb_connection_free(&c);
		pthread_join(worker, NULL);
	}
	struct adb_operation op;
	struct adb_connection c;
	adb_operation_init(&op, NULL, 1000);
	adb_connection_init(&c, &op);
	char sentinel = 'X';
	assert(adb_shell_execute(&c, "unused", &sentinel, 0));
	assert(sentinel == 'X');
	op.error = NULL;
	c.next_id = 0;
	struct adb_stream s;
	assert(adb_stream_open(&s, &c, "x:") && !c.next_id);
	assert(strstr(op.error, "exhausted"));
	assert(adb_stream_open(&s, &c, "x:") && !c.next_id);
	const struct adb_buffer overflow = { "", SIZE_MAX };
	assert(adb_packet_sendv(&c, ADB_WRTE, 1, 42, &overflow, 1));
	adb_connection_free(&c);
}
static void interrupted_packets(void)
{
	const size_t prefixes[] = { 0, 7, 24, 27 };
	for (size_t i = 0; i < 4; i++) {
		struct adb_operation op;
		struct adb_connection c;
		int fds[2];
		connection(&c, &op, fds);
		op.deadline_ms = adb_now_ms() + 15;
		uint8_t raw[29] = { 'W', 'R', 'T', 'E' };
		put32(raw + 4, 42);
		put32(raw + 8, 1);
		put32(raw + 12, 5);
		for (int n = 0; n < 4; n++)
			raw[20 + n] = raw[n] ^ 255;
		send_bytes(fds[1], raw, prefixes[i]);
		struct adb_packet packet;
		assert(adb_packet_receive(&c, &packet));
		assert(c.failed == (prefixes[i] != 0));
		adb_operation_init(&op, NULL, 1000);
		int result = adb_packet_send(&c, ADB_CLSE, 1, 42, NULL, 0);
		if (prefixes[i]) {
			assert(result);
			uint8_t byte;
			assert(recv(fds[1], &byte, 1, MSG_DONTWAIT) < 0);
			assert(errno == EAGAIN || errno == EWOULDBLOCK);
		} else {
			assert(!result);
			uint8_t header[24];
			assert(receive(fds[1], header, 24));
		}
		adb_connection_free(&c);
		close(fds[1]);
	}
	struct adb_operation op;
	struct adb_connection c;
	int fds[2];
	connection(&c, &op, fds);
	int small = 4096;
	assert(!setsockopt(c.fd, SOL_SOCKET, SO_SNDBUF, &small, sizeof(small)));
	op.deadline_ms = adb_now_ms() + 15;
	uint8_t payload[ADB_MAX_PAYLOAD] = { 0 };
	assert(adb_write(&c, payload, sizeof(payload)) && c.failed);
	adb_operation_init(&op, NULL, 1000);
	assert(adb_write(&c, "x", 1));
	adb_connection_free(&c);
	close(fds[1]);
}
int main(void)
{
	features();
	stream_edges();
	interrupted_packets();
	puts("C ADB edges: exact features, NUL output, vectored framing, exhausted IDs and interrupted transport passed");
	return 0;
}
