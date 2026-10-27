/* SPDX-License-Identifier: GPL-2.0-only */
#include "adb.h"
#include <string.h>

/* ADB permits incoming WRTE while an outgoing WRTE is awaiting OKAY. Retain
 * that data within a fixed budget, acknowledge it, and keep waiting for OKAY. */
static int stream_packet(struct adb_stream *s)
{
	struct adb_connection *c = s->connection;
	int result = -1;
	unsigned retired = 0;
	const struct adb_packet *p;
receive:
	p = adb_packet_next(c);
	if (!p)
		goto done;
	/* adbd can send CLSE after a service's exit frame, then send another
     * CLSE(0, old_local) when our concurrent close reaches its retired socket.
     * Monotonic IDs let us discard those replies without touching a new stream. */
	if (p->command == ADB_CLSE && !p->length && p->arg1 &&
	    p->arg1 < s->local && retired++ < 64)
		goto receive;
	if (p->arg1 != s->local || (s->remote && p->arg0 != s->remote &&
				    !(p->command == ADB_CLSE && !p->arg0))) {
		adb_fail(c->op, "Invalid ADB stream identifiers");
		goto done;
	}
	if (p->command == ADB_OKAY && !p->length && p->arg0) {
		if (!s->remote)
			s->remote = p->arg0;
		result = 1;
	} else if (p->command == ADB_WRTE && s->remote && p->length) {
		size_t retained = s->available - s->offset;
		if (p->length > sizeof(s->data) - retained) {
			adb_fail(
				c->op,
				"ADB stream receive buffer exceeded its limit");
			goto done;
		}
		memmove(s->data, s->data + s->offset, retained);
		memcpy(s->data + retained, p->data, p->length);
		s->offset = 0;
		s->available = retained + p->length;
		result = adb_packet_send(c, ADB_OKAY, s->local, s->remote, NULL,
					 0) ?
				 -1 :
				 2;
	} else if (p->command == ADB_CLSE && !p->length) {
		s->closed = true;
		result =
			!s->close_sent && adb_packet_send(c, ADB_CLSE, s->local,
							  p->arg0, NULL, 0) ?
				-1 :
				0;
	} else
		adb_fail(c->op, "Unexpected ADB stream packet");
done:
	adb_packet_release(c);
	return result;
}
int adb_stream_open(struct adb_stream *s, struct adb_connection *c,
		    const char *service)
{
	memset(s, 0, sizeof(*s));
	s->connection = c;
	if (!c->next_id)
		return adb_fail(c->op, "ADB stream identifiers exhausted");
	s->local = c->next_id++;
	if (adb_packet_send(c, ADB_OPEN, s->local, 0, service,
			    strlen(service) + 1))
		return -1;
	if (stream_packet(s) != 1)
		return adb_fail(c->op, "ADB service open failed");
	return 0;
}
int adb_stream_write(struct adb_stream *s, const void *data, size_t size)
{
	const struct adb_buffer buffer = { data, size };
	return adb_stream_writev(s, &buffer, 1);
}
int adb_stream_writev(struct adb_stream *s, const struct adb_buffer *buffers,
		      size_t count)
{
	struct adb_connection *c = s->connection;
	if (!c->max_payload || c->max_payload > ADB_MAX_PAYLOAD || s->failed)
		return adb_fail(c->op, "ADB stream is no longer usable");
	size_t current = 0, offset = 0;
	while (current < count) {
		struct adb_buffer packet[16];
		size_t parts = 0, length = 0;
		while (current < count && parts < 16 &&
		       length < c->max_payload) {
			size_t size = buffers[current].size - offset;
			if (!size) {
				current++;
				offset = 0;
				continue;
			}
			if (!buffers[current].data)
				return adb_fail(c->op,
						"Invalid ADB stream buffer");
			if (size > c->max_payload - length)
				size = c->max_payload - length;
			packet[parts++] = (struct adb_buffer){
				(const uint8_t *)buffers[current].data + offset,
				size
			};
			length += size;
			offset += size;
			if (offset == buffers[current].size) {
				current++;
				offset = 0;
			}
		}
		if (!length)
			break;
		if (s->closed || adb_packet_sendv(c, ADB_WRTE, s->local,
						  s->remote, packet, parts))
			return adb_fail(s->connection->op,
					"ADB stream write failed");
		int result;
		do {
			result = stream_packet(s);
		} while (result == 2);
		if (result != 1)
			return adb_fail(
				s->connection->op,
				"ADB stream write was not acknowledged");
	}
	return 0;
}
int adb_stream_read(struct adb_stream *s, void *data, size_t size)
{
	uint8_t *p = data;
	uint64_t consumed = s->consumed_bytes;
	if (s->failed)
		return adb_fail(s->connection->op,
				"ADB stream is no longer usable");
	while (size) {
		if (adb_check(s->connection->op))
			goto failed;
		if (s->offset == s->available) {
			if (s->closed) {
				adb_fail(
					s->connection->op,
					"ADB stream closed before expected data");
				goto failed;
			}
			int result = stream_packet(s);
			if (result < 0)
				goto failed;
			if (result == 1)
				return adb_fail(
					s->connection->op,
					"Unexpected ADB stream acknowledgement");
			continue;
		}
		size_t n = s->available - s->offset;
		if (n > size)
			n = size;
		memcpy(p, s->data + s->offset, n);
		adb_wipe(s->data + s->offset, n);
		s->offset += n;
		s->consumed_bytes += n;
		p += n;
		size -= n;
	}
	return 0;
failed:
	if (s->consumed_bytes != consumed)
		s->failed = true;
	return -1;
}
int adb_stream_close(struct adb_stream *s)
{
	if (s->closed)
		return 0;
	s->close_sent = true;
	if (adb_packet_send(s->connection, ADB_CLSE, s->local, s->remote, NULL,
			    0))
		return -1;
	while (!s->closed) {
		s->offset = s->available = 0;
		if (stream_packet(s) < 0)
			return -1;
	}
	adb_wipe(s->data, sizeof(s->data));
	return 0;
}
