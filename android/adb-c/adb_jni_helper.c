/* SPDX-License-Identifier: GPL-2.0-only */
#include "adb_jni_store.h"
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_EVENTS 16
struct helper_session {
	uint64_t id;
	pthread_t worker;
	atomic_bool cancel, done;
	pthread_mutex_t events_lock;
	const char *events[MAX_EVENTS];
	size_t first, count;
	struct adb_jni_store store;
	char *device, *host, *digest;
	uint16_t port;
	uint8_t *helper, *envelope;
	size_t helper_size, envelope_size;
};
static pthread_mutex_t registry_lock = PTHREAD_MUTEX_INITIALIZER;
static struct helper_session *current_session;
static uint64_t next_session = 1;
static void helper_event(void *context, const char *message)
{
	struct helper_session *s = context;
	pthread_mutex_lock(&s->events_lock);
	if (s->count == MAX_EVENTS) {
		s->first = (s->first + 1) % MAX_EVENTS;
		s->count--;
	}
	s->events[(s->first + s->count) % MAX_EVENTS] = message;
	s->count++;
	pthread_mutex_unlock(&s->events_lock);
}
static void free_launch(JNIEnv *env, struct helper_session *s)
{
	if (s->envelope) {
		adb_wipe(s->envelope, s->envelope_size);
		free(s->envelope);
		s->envelope = NULL;
	}
	free(s->helper);
	s->helper = NULL;
	free(s->device);
	free(s->host);
	free(s->digest);
	s->device = s->host = s->digest = NULL;
	adb_jni_store_free(env, &s->store);
}
static void *helper_worker(void *context)
{
	struct helper_session *s = context;
	JavaVM *vm = s->store.vm;
	JNIEnv *env;
	bool attached;
	if (adb_jni_attach(vm, &env, &attached)) {
		/* Release the retained Java ref from nativeReleaseHelper on its Java thread. */
		helper_event(
			s, "Could not attach ADB helper worker to Android VM");
	} else {
		struct adb_operation op;
		adb_operation_init(&op, &s->cancel, 25000);
		struct adb_store store = adb_jni_store_callbacks(&s->store);
		adb_helper_run(&op, &store, s->device, s->host, s->port,
			       s->helper, s->helper_size, s->digest,
			       s->envelope, s->envelope_size, helper_event, s);
		free_launch(env, s);
		if (attached)
			(*vm)->DetachCurrentThread(vm);
	}
	atomic_store_explicit(&s->done, true, memory_order_release);
	return NULL;
}
static uint8_t *copy_array(JNIEnv *env, jbyteArray array, size_t limit,
			   size_t *size)
{
	if (!array)
		return NULL;
	jsize length = (*env)->GetArrayLength(env, array);
	if (length <= 0 || (size_t)length > limit)
		return NULL;
	uint8_t *bytes = malloc((size_t)length);
	if (!bytes)
		return NULL;
	(*env)->GetByteArrayRegion(env, array, 0, length, (jbyte *)bytes);
	if (adb_jni_clear_exception(env)) {
		adb_wipe(bytes, (size_t)length);
		free(bytes);
		return NULL;
	}
	*size = (size_t)length;
	return bytes;
}
JNIEXPORT jlong JNICALL JNI_NAME(nativeStartHelper)(
	JNIEnv *env, jclass type, jobject object, jstring device, jstring host,
	jint port, jbyteArray helper, jstring digest, jbyteArray envelope)
{
	(void)type;
	const char *error = "Could not allocate ADB helper session";
	struct helper_session *s = calloc(1, sizeof(*s));
	if (!s)
		goto fail;
	atomic_init(&s->cancel, false);
	atomic_init(&s->done, false);
	if (!object) {
		error = "ADB credential store is required";
		goto fail;
	}
	if (adb_jni_store_init(env, object, &s->store)) {
		error = "Android credential store unavailable";
		goto fail;
	}
	s->device = adb_jni_text(env, device);
	s->host = adb_jni_text(env, host);
	s->digest = adb_jni_text(env, digest);
	if (!s->device || !s->host || !s->digest) {
		error = "Invalid text passed to the ADB bridge";
		goto fail;
	}
	if (!adb_numeric_address(s->host) || port < 1 || port > 65535 ||
	    adb_jni_local(env, &s->store, s->host) != 1) {
		error = "Helper endpoint must be a local numeric Wireless ADB address";
		goto fail;
	}
	s->port = (uint16_t)port;
	if (!adb_device_valid(s->device)) {
		error = "Invalid paired device identity";
		goto fail;
	}
	if (!adb_digest_valid(s->digest)) {
		error = "Invalid helper digest";
		goto fail;
	}
	s->helper = copy_array(env, helper, ADB_MAX_HELPER, &s->helper_size);
	s->envelope =
		copy_array(env, envelope, ADB_MAX_ENVELOPE, &s->envelope_size);
	if (!s->helper || !s->envelope) {
		error = "Helper/bootstrap size is invalid";
		goto fail;
	}
	char actual[65];
	adb_sha256_hex(s->helper, s->helper_size, actual);
	if (strcmp(actual, s->digest)) {
		error = "Bundled helper digest mismatch";
		goto fail;
	}
	if (pthread_mutex_init(&s->events_lock, NULL)) {
		error = "Could not create helper event queue";
		goto fail;
	}
	pthread_mutex_lock(&registry_lock);
	if (current_session || next_session > INT64_MAX) {
		pthread_mutex_unlock(&registry_lock);
		pthread_mutex_destroy(&s->events_lock);
		error = "The previous ADB helper session is still closing";
		goto fail;
	}
	s->id = next_session++;
	if (pthread_create(&s->worker, NULL, helper_worker, s)) {
		pthread_mutex_unlock(&registry_lock);
		pthread_mutex_destroy(&s->events_lock);
		error = "Could not start ADB helper worker";
		goto fail;
	}
	current_session = s;
	jlong id = (jlong)s->id;
	pthread_mutex_unlock(&registry_lock);
	return id;
fail:
	if (s) {
		free_launch(env, s);
		free(s);
	}
	adb_jni_clear_exception(env);
	jclass exception =
		(*env)->FindClass(env, "java/lang/IllegalStateException");
	if (exception) {
		(*env)->ThrowNew(env, exception, error);
		(*env)->DeleteLocalRef(env, exception);
	}
	return 0;
}
JNIEXPORT jstring JNICALL JNI_NAME(nativePollHelper)(JNIEnv *env, jclass type,
						     jlong id)
{
	(void)type;
	char text[4096] = { 0 };
	size_t used = 0;
	pthread_mutex_lock(&registry_lock);
	struct helper_session *s = current_session;
	if (!s || id <= 0 || s->id != (uint64_t)id) {
		pthread_mutex_unlock(&registry_lock);
		return NULL;
	}
	pthread_mutex_lock(&s->events_lock);
	while (s->count) {
		const char *message = s->events[s->first];
		size_t n = strlen(message);
		if (used + n + 2 >= sizeof(text))
			break;
		if (used)
			text[used++] = '\n';
		memcpy(text + used, message, n);
		used += n;
		text[used] = 0;
		s->first = (s->first + 1) % MAX_EVENTS;
		s->count--;
	}
	if (atomic_load_explicit(&s->done, memory_order_acquire))
		snprintf(text + used, sizeof(text) - used, "%sSESSION_CLOSED",
			 used ? "\n" : "");
	pthread_mutex_unlock(&s->events_lock);
	pthread_mutex_unlock(&registry_lock);
	return (*env)->NewStringUTF(env, text);
}
JNIEXPORT void JNICALL JNI_NAME(nativeCloseHelper)(JNIEnv *env, jclass type,
						   jlong id)
{
	(void)env;
	(void)type;
	pthread_mutex_lock(&registry_lock);
	if (current_session && id > 0 && current_session->id == (uint64_t)id)
		atomic_store_explicit(&current_session->cancel, true,
				      memory_order_release);
	pthread_mutex_unlock(&registry_lock);
}
JNIEXPORT jboolean JNICALL JNI_NAME(nativeReleaseHelper)(JNIEnv *env,
							 jclass type, jlong id)
{
	(void)type;
	pthread_mutex_lock(&registry_lock);
	struct helper_session *s = current_session;
	if (!s || id <= 0 || s->id != (uint64_t)id ||
	    !atomic_load_explicit(&s->done, memory_order_acquire)) {
		pthread_mutex_unlock(&registry_lock);
		return JNI_FALSE;
	}
	pthread_join(s->worker, NULL);
	current_session = NULL;
	free_launch(env, s);
	pthread_mutex_destroy(&s->events_lock);
	free(s);
	pthread_mutex_unlock(&registry_lock);
	return JNI_TRUE;
}
