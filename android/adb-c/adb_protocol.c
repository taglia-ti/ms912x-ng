/* SPDX-License-Identifier: GPL-2.0-only */
#include "adb.h"
#include <stdlib.h>
#include <string.h>
uint32_t adb_get_le32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
	       (uint32_t)p[3] << 24;
}
void adb_put_le32(uint8_t *p, uint32_t v)
{
	for (int i = 0; i < 4; i++)
		p[i] = (uint8_t)(v >> (8 * i));
}
static uint32_t checksum(const uint8_t *p, size_t length)
{
	uint32_t sum = 0;
	for (size_t i = 0; i < length; i++)
		sum += p[i];
	return sum;
}
int adb_packet_send(struct adb_connection *c, uint32_t command, uint32_t arg0,
		    uint32_t arg1, const void *data, size_t length)
{
	const struct adb_buffer buffer = { data, length };
	return adb_packet_sendv(c, command, arg0, arg1, &buffer, 1);
}
int adb_packet_sendv(struct adb_connection *c, uint32_t command, uint32_t arg0,
		     uint32_t arg1, const struct adb_buffer *buffers,
		     size_t count)
{
	size_t length = 0;
	size_t limit = c->max_payload < ADB_MAX_PAYLOAD ? c->max_payload :
							  ADB_MAX_PAYLOAD;
	for (size_t i = 0; i < count; i++) {
		if (buffers[i].size > limit - length ||
		    (buffers[i].size && !buffers[i].data))
			return adb_fail(
				c->op,
				"ADB outgoing packet exceeded negotiated limit");
		length += buffers[i].size;
	}
	if (!c->tx_buffer)
		c->tx_buffer = malloc(ADB_MAX_PAYLOAD + 24);
	if (!c->tx_buffer)
		return adb_fail(c->op, "Could not allocate ADB send buffer");
	uint8_t *header = c->tx_buffer;
	adb_put_le32(header, command);
	adb_put_le32(header + 4, arg0);
	adb_put_le32(header + 8, arg1);
	adb_put_le32(header + 12, (uint32_t)length);
	adb_put_le32(header + 20, command ^ UINT32_MAX);
	/* One TLS write/record sequence avoids a tiny header record and a second
     * socket operation per packet. Reuse bounded storage across transfers. */
	size_t offset = 24;
	for (size_t i = 0; i < count; i++) {
		if (buffers[i].size)
			memcpy(header + offset, buffers[i].data,
			       buffers[i].size);
		offset += buffers[i].size;
	}
	adb_put_le32(header + 16, c->version >= 0x01000001 ?
					  0 :
					  checksum(header + 24, length));
	int result = adb_write(c, header, 24 + length);
	adb_wipe(header, 24 + length);
	return result;
}
int adb_packet_receive(struct adb_connection *c, struct adb_packet *p)
{
	uint8_t header[24];
	uint64_t received = c->received_bytes;
	p->length = 0;
	if (adb_read(c, header, sizeof(header)))
		goto failed;
	p->command = adb_get_le32(header);
	p->arg0 = adb_get_le32(header + 4);
	p->arg1 = adb_get_le32(header + 8);
	p->length = adb_get_le32(header + 12);
	if (p->length > ADB_MAX_PAYLOAD ||
	    adb_get_le32(header + 20) != (p->command ^ UINT32_MAX)) {
		c->failed = true;
		return adb_fail(c->op,
				"Invalid ADB packet header or payload length");
	}
	if (adb_read(c, p->data, p->length))
		goto failed;
	uint32_t sum = adb_get_le32(header + 16);
	if ((c->version < 0x01000001 || sum) &&
	    sum != checksum(p->data, p->length)) {
		c->failed = true;
		return adb_fail(c->op, "Invalid ADB packet checksum");
	}
	return 0;
failed:
	/* Exact reads use caller-owned cursors. Losing any portion of this packet
	 * makes another protocol operation unsafe, including graceful shutdown. */
	if (c->received_bytes != received)
		c->failed = true;
	return -1;
}
/* The packet layer owns the reusable receive buffer and secret cleanup.
 * A borrowed packet is valid until the next receive or release. */
void adb_packet_release(struct adb_connection *c)
{
	struct adb_packet *p = c->rx_packet;
	if (!p)
		return;
	size_t size = p->length > ADB_MAX_PAYLOAD ? ADB_MAX_PAYLOAD : p->length;
	adb_wipe(p, offsetof(struct adb_packet, data) + size);
}
const struct adb_packet *adb_packet_next(struct adb_connection *c)
{
	adb_packet_release(c);
	if (!c->rx_packet)
		c->rx_packet = calloc(1, sizeof(*c->rx_packet));
	if (!c->rx_packet) {
		adb_fail(c->op, "Could not allocate ADB receive buffer");
		return NULL;
	}
	if (adb_packet_receive(c, c->rx_packet)) {
		adb_packet_release(c);
		return NULL;
	}
	return c->rx_packet;
}
