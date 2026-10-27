/* SPDX-License-Identifier: GPL-2.0-only */
#include "adb.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int adb_shell_open(struct adb_shell *shell, struct adb_connection *c,
		   const char *command)
{
	char service[2048];
	memset(shell, 0, sizeof(*shell));
	shell->stream.connection = c;
	int length =
		snprintf(service, sizeof(service), "shell,v2,raw:%s", command);
	if (length < 0 || (size_t)length >= sizeof(service))
		return adb_fail(c->op, "ADB shell command exceeded its limit");
	return adb_stream_open(&shell->stream, c, service);
}
static int shell_send(struct adb_shell *shell, uint8_t channel,
		      const void *data, size_t size)
{
	uint8_t header[5] = { channel };
	adb_put_le32(header + 1, (uint32_t)size);
	const struct adb_buffer buffers[] = { { header, sizeof(header) },
					      { data, size } };
	return adb_stream_writev(&shell->stream, buffers, 2);
}
int adb_shell_stdin(struct adb_shell *shell, const void *data, size_t size)
{
	if (size > ADB_MAX_ENVELOPE)
		return adb_fail(shell->stream.connection->op,
				"ADB stdin exceeded its limit");
	return shell_send(shell, 0, data, size);
}
int adb_shell_close_stdin(struct adb_shell *shell)
{
	return shell_send(shell, 4, NULL, 0);
}
int adb_shell_next(struct adb_shell *shell, uint8_t *channel, void *output,
		   size_t capacity, size_t *length, size_t limit)
{
	*length = 0;
	*channel = 0;
	if (shell->failed)
		return adb_fail(shell->stream.connection->op,
				"ADB shell framing is no longer usable");
	if (shell->exited)
		return 0;
	uint8_t header[5];
	if (adb_stream_read(&shell->stream, header, 5))
		return -1;
	size_t size = adb_get_le32(header + 1);
	*channel = header[0];
	if (*channel == 3 && size == 1) {
		if (adb_stream_read(&shell->stream, &shell->exit_code, 1))
			goto failed;
		shell->exited = true;
		return 0;
	}
	if ((*channel != 1 && *channel != 2) || size > capacity ||
	    shell->output_bytes > limit || size > limit - shell->output_bytes) {
		adb_fail(
			shell->stream.connection->op,
			"Invalid ADB shell frame or output exceeded its limit");
		goto failed;
	}
	if (adb_stream_read(&shell->stream, output, size))
		goto failed;
	shell->output_bytes += size;
	*length = size;
	return 1;
failed:
	/* A decoded header has already been consumed. A failed payload read
	 * cannot be resumed by interpreting remaining bytes as another header. */
	shell->failed = true;
	return -1;
}
int adb_shell_execute(struct adb_connection *c, const char *command,
		      char *output, size_t capacity)
{
	if (!capacity || !output)
		return adb_fail(c->op, "ADB shell output buffer is empty");
	struct adb_shell *shell = malloc(sizeof(*shell));
	uint8_t *chunk = malloc(capacity);
	if (!shell || !chunk) {
		free(shell);
		free(chunk);
		return adb_fail(c->op, "Could not allocate ADB shell buffers");
	}
	int result = -1;
	size_t used = 0;
	output[0] = 0;
	if (adb_shell_open(shell, c, command) || adb_shell_close_stdin(shell))
		goto done;
	for (;;) {
		uint8_t channel;
		size_t length;
		int next = adb_shell_next(shell, &channel, chunk, capacity,
					  &length, capacity - 1);
		if (next < 0)
			goto done;
		if (!next)
			break;
		if (memchr(chunk, 0, length)) {
			adb_fail(c->op,
				 "ADB verification output contains a NUL byte");
			goto done;
		}
		if (channel == 1) {
			memcpy(output + used, chunk, length);
			used += length;
			output[used] = 0;
		}
	}
	if (shell->exit_code) {
		adb_fail(c->op, "ADB verification returned failure");
		goto done;
	}
	result = adb_stream_close(&shell->stream);
done:
	adb_wipe(shell, sizeof(*shell));
	adb_wipe(chunk, capacity);
	free(shell);
	free(chunk);
	return result;
}
