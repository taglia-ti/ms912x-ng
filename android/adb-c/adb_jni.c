/* SPDX-License-Identifier: GPL-2.0-only */
#include "adb_jni_store.h"
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static pthread_mutex_t operation_lock = PTHREAD_MUTEX_INITIALIZER;
static bool operation_active;
static atomic_bool operation_cancelled;
static bool begin_operation(void)
{
	pthread_mutex_lock(&operation_lock);
	bool allowed = !operation_active;
	if (allowed) {
		operation_active = true;
		atomic_store(&operation_cancelled, false);
	}
	pthread_mutex_unlock(&operation_lock);
	return allowed;
}
static void end_operation(void)
{
	pthread_mutex_lock(&operation_lock);
	operation_active = false;
	pthread_mutex_unlock(&operation_lock);
}
JNIEXPORT const char *ms912x_adb_backend_version(void)
{
	return ADB_BACKEND_VERSION;
}
JNIEXPORT jstring JNICALL JNI_NAME(nativeBackendVersion)(JNIEnv *env,
							 jclass type)
{
	(void)type;
	return adb_jni_result(env, ADB_BACKEND_VERSION);
}
JNIEXPORT void JNICALL JNI_NAME(nativeCancelCurrent)(JNIEnv *env, jclass type)
{
	(void)env;
	(void)type;
	atomic_store_explicit(&operation_cancelled, true, memory_order_release);
}
JNIEXPORT jboolean JNICALL JNI_NAME(nativeIsLocalAddress)(JNIEnv *env,
							  jclass type,
							  jobject object,
							  jstring host)
{
	(void)type;
	struct adb_jni_store store;
	if (adb_jni_store_init(env, object, &store))
		return JNI_FALSE;
	char *text = adb_jni_text(env, host);
	int allowed = text ? adb_jni_local(env, &store, text) : 0;
	free(text);
	adb_jni_store_free(env, &store);
	return allowed == 1 ? JNI_TRUE : JNI_FALSE;
}
static int check_local(JNIEnv *env, struct adb_jni_store *store,
		       const char *host, struct adb_operation *op)
{
	/* Reject names before the Java policy callback can invoke DNS. */
	if (!adb_numeric_address(host))
		return adb_fail(op,
				"ADB endpoint requires a numeric IP address");
	int allowed = adb_jni_local(env, store, host);
	if (allowed < 0)
		return adb_fail(op, "Could not validate endpoint");
	if (!allowed)
		return adb_fail(
			op,
			"ADB endpoint is not an address assigned to this phone");
	return 0;
}
static int verify_shell(struct adb_connection *c, char *output, size_t size)
{
	if (adb_shell_execute(c, "id", output, size))
		return -1;
	size_t length = strlen(output);
	while (length &&
	       (output[length - 1] == '\n' || output[length - 1] == '\r' ||
		output[length - 1] == ' ' || output[length - 1] == '\t'))
		output[--length] = 0;
	/* Result text is public diagnostic output, but controls never reach JNI. */
	for (size_t i = 0; i < length; i++)
		if ((unsigned char)output[i] < 32 ||
		    (unsigned char)output[i] >= 127)
			output[i] = ' ';
	return 0;
}
JNIEXPORT jstring JNICALL JNI_NAME(nativePairAndVerify)(
	JNIEnv *env, jclass type, jobject object, jstring pairing_host,
	jint pairing_port, jstring connect_host, jint connect_port,
	jstring pairing_code)
{
	(void)type;
	if (!begin_operation())
		return adb_jni_result(
			env, "Another ADB operation is already running");
	struct adb_operation op;
	adb_operation_init(&op, &operation_cancelled, 75000);
	struct adb_jni_store store = { 0 };
	struct adb_identity identity;
	struct adb_connection pair, connect;
	adb_identity_init(&identity);
	adb_connection_init(&pair, &op);
	adb_connection_init(&connect, &op);
	char *ph = NULL, *ch = NULL, *code = NULL;
	char output[1024], message[1536], device[256], fingerprint[96];
	bool success = false;
	if (!object) {
		adb_fail(&op, "ADB credential store is required");
		goto done;
	}
	if (adb_jni_store_init(env, object, &store)) {
		adb_fail(&op, "Android credential store unavailable");
		goto done;
	}
	ph = adb_jni_text(env, pairing_host);
	ch = adb_jni_text(env, connect_host);
	code = adb_jni_text(env, pairing_code);
	if (!ph || !ch || !code) {
		adb_fail(&op, "Invalid text passed to the ADB bridge");
		goto done;
	}
	if (pairing_port < 1 || pairing_port > 65535 || connect_port < 1 ||
	    connect_port > 65535) {
		adb_fail(&op, "ADB endpoint port is invalid");
		goto done;
	}
	if (check_local(env, &store, ph, &op) ||
	    check_local(env, &store, ch, &op))
		goto done;
	if (strcmp(ph, ch)) {
		adb_fail(
			&op,
			"Pairing and TLS endpoints must use the same local address");
		goto done;
	}
	if (!adb_pairing_code_valid(code)) {
		adb_fail(&op, "Pairing code must contain six digits");
		goto done;
	}
	struct adb_store callbacks = adb_jni_store_callbacks(&store);
	if (adb_identity_load(&identity, &callbacks, true, &op) ||
	    adb_connect_socket(&pair, ph, (uint16_t)pairing_port) ||
	    adb_pair(&pair, &identity, code, device, fingerprint))
		goto done;
	adb_connection_free(&pair);
	if (callbacks.save_device(callbacks.context, device, ph, fingerprint)) {
		adb_fail(&op, "Could not save paired device");
		goto done;
	}
	if (adb_connect_socket(&connect, ch, (uint16_t)connect_port) ||
	    adb_handshake(&connect, &identity, true) ||
	    verify_shell(&connect, output, sizeof(output)))
		goto done;
	snprintf(message, sizeof(message), "Paired device %s; shell: %s",
		 device, output);
	success = true;
done:
	adb_connection_free(&pair);
	adb_connection_free(&connect);
	adb_identity_free(&identity);
	adb_jni_store_free(env, &store);
	free(ph);
	free(ch);
	if (code) {
		adb_wipe(code, strlen(code));
		free(code);
	}
	end_operation();
	return adb_jni_result(env,
			      success  ? message :
			      op.error ? op.error :
					 "ADB pairing or connection failed");
}
JNIEXPORT jstring JNICALL JNI_NAME(nativeConnectLocalTcpAndVerify)(
	JNIEnv *env, jclass type, jobject object, jstring host, jint port)
{
	(void)type;
	if (!begin_operation())
		return adb_jni_result(
			env, "Another ADB operation is already running");
	struct adb_operation op;
	adb_operation_init(&op, &operation_cancelled, 75000);
	struct adb_identity identity;
	struct adb_connection connection;
	struct adb_jni_store store = { 0 };
	adb_identity_init(&identity);
	adb_connection_init(&connection, &op);
	char *text = NULL, output[1024], message[1200];
	bool success = false;
	if (!object) {
		adb_fail(&op, "ADB credential store is required");
		goto done;
	}
	if (adb_jni_store_init(env, object, &store)) {
		adb_fail(&op, "Android credential store unavailable");
		goto done;
	}
	text = adb_jni_text(env, host);
	if (!text) {
		adb_fail(&op, "Invalid text passed to the ADB bridge");
		goto done;
	}
	if (!adb_numeric_address(text)) {
		adb_fail(&op,
			 "Debug TCP probe only accepts a numeric IP address");
		goto done;
	}
	if (port != 5555) {
		adb_fail(&op, "Debug TCP probe only accepts port 5555");
		goto done;
	}
	int local = adb_jni_local(env, &store, text);
	if (local < 0) {
		adb_fail(&op, "Could not validate endpoint");
		goto done;
	}
	if (!local) {
		adb_fail(&op,
			 "Debug TCP endpoint is not assigned to this phone");
		goto done;
	}
	struct adb_store callbacks = adb_jni_store_callbacks(&store);
	if (adb_identity_load(&identity, &callbacks, true, &op) ||
	    adb_connect_socket(&connection, text, 5555) ||
	    adb_handshake(&connection, &identity, false) ||
	    verify_shell(&connection, output, sizeof(output)))
		goto done;
	snprintf(message, sizeof(message), "Local adbd TCP shell verified: %s",
		 output);
	success = true;
done:
	adb_connection_free(&connection);
	adb_identity_free(&identity);
	adb_jni_store_free(env, &store);
	free(text);
	end_operation();
	return adb_jni_result(env, success ? message :
				   op.error ?
					     op.error :
					     "Local ADB TCP connection failed");
}
