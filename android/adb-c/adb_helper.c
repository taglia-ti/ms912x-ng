/* SPDX-License-Identifier: GPL-2.0-only */
#include "adb.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int prepare(struct adb_connection *c, const uint8_t *helper,
		   size_t helper_size, const char *digest, uint8_t *envelope,
		   size_t envelope_size, struct adb_shell *shell,
		   adb_event_fn event, void *context)
{
	char output[1024], path[160], command[512];
	if (adb_shell_execute(c, "id -u", output, sizeof(output)))
		return -1;
	if (strcmp(output, "2000\n"))
		return adb_fail(c->op, "Helper requires shell UID 2000");
	event(context, "Shell UID 2000 verified");
	/* digest validation makes the entire generated path shell-safe. */
	snprintf(path, sizeof(path), "/data/local/tmp/ms912x-helper-%s.jar",
		 digest);
	if (adb_sync_push(c, path, helper, helper_size))
		return -1;
	snprintf(command, sizeof(command), "sha256sum '%s'", path);
	if (adb_shell_execute(c, command, output, sizeof(output)))
		return -1;
	if (strncmp(output, digest, 64) ||
	    (output[64] != ' ' && output[64] != '\t'))
		return adb_fail(c->op, "Transferred helper digest mismatch");
	event(context, "Helper transferred and digest verified");
	snprintf(
		command, sizeof(command),
		"CLASSPATH='%s' app_process / --nice-name=ms912x-helper "
		"io.github.taglia_ti.ms912x_ng.helper.HelperMain --bootstrap-stdin",
		path);
	if (adb_shell_open(shell, c, command) ||
	    adb_shell_stdin(shell, envelope, envelope_size))
		return -1;
	adb_wipe(envelope, envelope_size);
	event(context,
	      "Helper launched; awaiting authenticated Binder enrollment");
	return 0;
}
static int drain(struct adb_shell *shell, struct adb_operation *op)
{
	uint8_t *output = malloc(ADB_MAX_OUTPUT);
	if (!output)
		return adb_fail(op, "Could not allocate helper output buffer");
	int result = 0;
	for (;;) {
		uint8_t channel;
		size_t length;
		int next =
			adb_shell_next(shell, &channel, output, ADB_MAX_OUTPUT,
				       &length, ADB_MAX_OUTPUT);
		adb_wipe(output, length);
		if (next <= 0) {
			result = next;
			break;
		}
	}
	if (result < 0 && op->error_kind == ADB_ERROR_CANCELLED) {
		/* Ignore the cancelled flag during bounded graceful EOF shutdown. */
		adb_operation_init(op, NULL, 4000);
		if (!shell->failed && !shell->stream.failed &&
		    !shell->stream.connection->failed &&
		    !adb_shell_close_stdin(shell)) {
			while (!shell->exited) {
				uint8_t channel;
				size_t length;
				if (adb_shell_next(shell, &channel, output,
						   ADB_MAX_OUTPUT, &length,
						   ADB_MAX_OUTPUT) < 0)
					break;
				adb_wipe(output, length);
			}
		}
		result = 0;
	}
	adb_wipe(output, ADB_MAX_OUTPUT);
	free(output);
	return result;
}
int adb_helper_run(struct adb_operation *op, const struct adb_store *store,
		   const char *device, const char *host, uint16_t port,
		   const uint8_t *helper, size_t helper_size,
		   const char *digest, uint8_t *envelope, size_t envelope_size,
		   adb_event_fn event, void *context)
{
	struct adb_identity identity;
	struct adb_connection connection;
	adb_identity_init(&identity);
	adb_connection_init(&connection, op);
	struct adb_shell *shell = calloc(1, sizeof(*shell));
	int result = -1;
	char actual[65];
	adb_sha256_hex(helper, helper_size, actual);
	if (!shell) {
		adb_fail(op, "Could not allocate helper shell");
		goto done;
	}
	if (!adb_digest_valid(digest) || strcmp(actual, digest)) {
		adb_fail(op, "Bundled helper digest mismatch");
		goto done;
	}
	int paired = store->has_device(store->context, device);
	if (paired >= 0 && adb_check(op))
		goto done;
	if (paired != 1) {
		adb_fail(op,
			 paired < 0 ?
				 "Android credential store operation failed" :
				 "Device has no saved pairing");
		goto done;
	}
	event(context,
	      "Connecting to this phone's paired Wireless ADB endpoint");
	if (adb_identity_load(&identity, store, false, op) ||
	    adb_connect_socket(&connection, host, port) ||
	    adb_handshake(&connection, &identity, true))
		goto done;
	op->deadline_ms = adb_now_ms() + 25000;
	if (prepare(&connection, helper, helper_size, digest, envelope,
		    envelope_size, shell, event, context))
		goto done;
	op->deadline_ms = 0;
	result = drain(shell, op);
done:
	adb_wipe(envelope, envelope_size);
	if (result)
		event(context,
		      op->error ? op->error : "ADB helper stream disconnected");
	adb_connection_free(&connection);
	adb_identity_free(&identity);
	if (shell) {
		adb_wipe(shell, sizeof(*shell));
		free(shell);
	}
	return result;
}
