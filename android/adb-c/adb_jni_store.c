/* SPDX-License-Identifier: GPL-2.0-only */
#include "adb_jni_store.h"
#include <stdlib.h>
#include <string.h>
int adb_jni_store_init(JNIEnv *env, jobject object, struct adb_jni_store *store)
{
	memset(store, 0, sizeof(*store));
	if (!object || (*env)->GetJavaVM(env, &store->vm))
		return -1;
	store->object = (*env)->NewGlobalRef(env, object);
	if (!store->object || adb_jni_clear_exception(env))
		goto fail;
	jclass type = (*env)->GetObjectClass(env, object);
	if (!type || adb_jni_clear_exception(env))
		goto fail;
	store->load_key =
		(*env)->GetMethodID(env, type, "loadHostCredential", "()[B");
	if (adb_jni_clear_exception(env))
		goto fail_type;
	store->save_key =
		(*env)->GetMethodID(env, type, "saveHostCredential", "([B)V");
	if (adb_jni_clear_exception(env))
		goto fail_type;
	store->save_device = (*env)->GetMethodID(
		env, type, "savePairedDevice",
		"(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V");
	if (adb_jni_clear_exception(env))
		goto fail_type;
	store->load_device =
		(*env)->GetMethodID(env, type, "loadPairedDevice",
				    "(Ljava/lang/String;)[Ljava/lang/String;");
	if (adb_jni_clear_exception(env))
		goto fail_type;
	store->is_local = (*env)->GetMethodID(env, type, "isLocalAddress",
					      "(Ljava/lang/String;)Z");
	if (adb_jni_clear_exception(env))
		goto fail_type;
	(*env)->DeleteLocalRef(env, type);
	return 0;
fail_type:
	(*env)->DeleteLocalRef(env, type);
fail:
	adb_jni_clear_exception(env);
	adb_jni_store_free(env, store);
	return -1;
}
void adb_jni_store_free(JNIEnv *env, struct adb_jni_store *store)
{
	if (store->object)
		(*env)->DeleteGlobalRef(env, store->object);
	memset(store, 0, sizeof(*store));
}
int adb_jni_local(JNIEnv *env, struct adb_jni_store *store, const char *host)
{
	jstring text = adb_jni_string(env, host);
	if (!text) {
		adb_jni_clear_exception(env);
		return -1;
	}
	jboolean allowed = (*env)->CallBooleanMethod(env, store->object,
						     store->is_local, text);
	(*env)->DeleteLocalRef(env, text);
	return adb_jni_clear_exception(env) ? -1 : allowed == JNI_TRUE;
}
static int load_key(void *context, uint8_t **bytes, size_t *length)
{
	struct adb_jni_store *store = context;
	JNIEnv *env;
	bool attached;
	*bytes = NULL;
	*length = 0;
	if (adb_jni_attach(store->vm, &env, &attached))
		return -1;
	jbyteArray value =
		(*env)->CallObjectMethod(env, store->object, store->load_key);
	int result = -1;
	if (adb_jni_clear_exception(env))
		goto done;
	if (!value) {
		result = 0;
		goto done;
	}
	jsize size = (*env)->GetArrayLength(env, value);
	if (size <= 0 || size > 16384)
		goto done;
	uint8_t *copy = calloc((size_t)size + 1, 1);
	if (!copy)
		goto done;
	(*env)->GetByteArrayRegion(env, value, 0, size, (jbyte *)copy);
	if (adb_jni_clear_exception(env)) {
		adb_wipe(copy, (size_t)size + 1);
		free(copy);
		goto done;
	}
	*bytes = copy;
	*length = (size_t)size;
	result = 0;
done:
	adb_jni_clear_exception(env);
	if (value) {
		adb_jni_erase_array(env, value);
		(*env)->DeleteLocalRef(env, value);
	}
	if (attached)
		(*store->vm)->DetachCurrentThread(store->vm);
	return result;
}
static int save_key(void *context, const uint8_t *bytes, size_t length)
{
	struct adb_jni_store *store = context;
	JNIEnv *env;
	bool attached;
	if (adb_jni_attach(store->vm, &env, &attached))
		return -1;
	jbyteArray value = (*env)->NewByteArray(env, (jsize)length);
	int result = -1;
	if (!value || adb_jni_clear_exception(env))
		goto done;
	(*env)->SetByteArrayRegion(env, value, 0, (jsize)length,
				   (const jbyte *)bytes);
	if (adb_jni_clear_exception(env))
		goto done;
	(*env)->CallVoidMethod(env, store->object, store->save_key, value);
	result = adb_jni_clear_exception(env) ? -1 : 0;
done:
	adb_jni_clear_exception(env);
	if (value) {
		adb_jni_erase_array(env, value);
		(*env)->DeleteLocalRef(env, value);
	}
	if (attached)
		(*store->vm)->DetachCurrentThread(store->vm);
	return result;
}
static int save_device(void *context, const char *device, const char *host,
		       const char *fingerprint)
{
	struct adb_jni_store *store = context;
	JNIEnv *env;
	bool attached;
	if (adb_jni_attach(store->vm, &env, &attached))
		return -1;
	jstring id = adb_jni_string(env, device), address = NULL, hash = NULL;
	int result = -1;
	if (!id || adb_jni_clear_exception(env))
		goto done;
	address = (*env)->NewStringUTF(env, host);
	if (!address || adb_jni_clear_exception(env))
		goto done;
	hash = (*env)->NewStringUTF(env, fingerprint);
	if (!hash || adb_jni_clear_exception(env))
		goto done;
	(*env)->CallVoidMethod(env, store->object, store->save_device, id,
			       address, hash);
	result = adb_jni_clear_exception(env) ? -1 : 0;
done:
	adb_jni_clear_exception(env);
	if (id)
		(*env)->DeleteLocalRef(env, id);
	if (address)
		(*env)->DeleteLocalRef(env, address);
	if (hash)
		(*env)->DeleteLocalRef(env, hash);
	if (attached)
		(*store->vm)->DetachCurrentThread(store->vm);
	return result;
}
static int has_device(void *context, const char *device)
{
	struct adb_jni_store *store = context;
	JNIEnv *env;
	bool attached;
	if (adb_jni_attach(store->vm, &env, &attached))
		return -1;
	int result = -1;
	jstring id = adb_jni_string(env, device);
	jobjectArray value = NULL;
	if (!id || adb_jni_clear_exception(env))
		goto done;
	value = (*env)->CallObjectMethod(env, store->object, store->load_device,
					 id);
	if (adb_jni_clear_exception(env))
		goto done;
	if (!value) {
		result = 0;
		goto done;
	}
	if ((*env)->GetArrayLength(env, value) != 3)
		goto done;
	result = 1;
	for (jsize i = 0; i < 3; i++) {
		jobject field = (*env)->GetObjectArrayElement(env, value, i);
		if (!field || adb_jni_clear_exception(env))
			result = -1;
		if (field)
			(*env)->DeleteLocalRef(env, field);
	}
done:
	adb_jni_clear_exception(env);
	if (value)
		(*env)->DeleteLocalRef(env, value);
	if (id)
		(*env)->DeleteLocalRef(env, id);
	if (attached)
		(*store->vm)->DetachCurrentThread(store->vm);
	return result;
}
struct adb_store adb_jni_store_callbacks(struct adb_jni_store *store)
{
	return (struct adb_store){ store, load_key, save_key, save_device,
				   has_device };
}
