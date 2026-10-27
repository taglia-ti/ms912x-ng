/* SPDX-License-Identifier: GPL-2.0-only */
#include "adb.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static int sync_record(struct adb_stream *s, const char tag[4], uint32_t value,
		       const void *data, size_t size)
{
	uint8_t header[8];
	memcpy(header, tag, 4);
	adb_put_le32(header + 4, value);
	const struct adb_buffer buffers[] = { { header, sizeof(header) },
					      { data, size } };
	return adb_stream_writev(s, buffers, 2);
}
int adb_sync_push(struct adb_connection *c, const char *path,
		  const uint8_t *bytes, size_t size)
{
	struct adb_stream *s = malloc(sizeof(*s));
	if (!s)
		return adb_fail(c->op,
				"Could not allocate ADB transfer stream");
	char destination[512];
	int length = snprintf(destination, sizeof(destination), "%s,384", path);
	int result = -1;
	if (length < 0 || (size_t)length >= sizeof(destination)) {
		adb_fail(c->op, "Invalid helper destination");
		goto done;
	}
	if (adb_stream_open(s, c, "sync:") ||
	    sync_record(s, "SEND", (uint32_t)length, destination,
			(size_t)length))
		goto done;
	/* Fit each DATA header and payload into a single negotiated WRTE.
	 * This avoids a separate stop-and-wait round trip for every header. */
	size_t payload =
		c->max_payload > 8 ? c->max_payload - 8 : c->max_payload;
	while (size) {
		size_t chunk = size > payload ? payload : size;
		if (sync_record(s, "DATA", (uint32_t)chunk, bytes, chunk))
			goto done;
		bytes += chunk;
		size -= chunk;
	}
	uint8_t response[8];
	if (sync_record(s, "DONE", (uint32_t)time(NULL), NULL, 0) ||
	    adb_stream_read(s, response, 8))
		goto done;
	if (memcmp(response, "OKAY", 4) || adb_get_le32(response + 4)) {
		adb_fail(c->op, "Helper transfer failed");
		goto done;
	}
	result = adb_stream_close(s);
done:
	adb_wipe(s, sizeof(*s));
	free(s);
	return result;
}
