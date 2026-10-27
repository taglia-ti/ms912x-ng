/* SPDX-License-Identifier: GPL-2.0-only */
#include "adb_jni_util.h"
#include <stdlib.h>
#include <string.h>

bool adb_jni_clear_exception(JNIEnv *env)
{
	if (!(*env)->ExceptionCheck(env))
		return false;
	(*env)->ExceptionClear(env);
	return true;
}
jstring adb_jni_result(JNIEnv *env, const char *message)
{
	adb_jni_clear_exception(env);
	return adb_jni_string(env, message);
}
jstring adb_jni_string(JNIEnv *env, const char *text)
{
	size_t bytes = strlen(text), size = 0;
	jchar *utf16 = malloc((bytes + 1) * sizeof(*utf16));
	if (!utf16)
		return NULL;
	const unsigned char *p = (const unsigned char *)text;
	while (*p) {
		uint32_t codepoint;
		if (adb_utf8_next(&p, &codepoint)) {
			free(utf16);
			return NULL;
		}
		if (codepoint >= 65536) {
			codepoint -= 65536;
			utf16[size++] = (jchar)(0xd800 + (codepoint >> 10));
			utf16[size++] = (jchar)(0xdc00 + (codepoint & 1023));
		} else
			utf16[size++] = (jchar)codepoint;
	}
	jstring result = (*env)->NewString(env, utf16, (jsize)size);
	free(utf16);
	return result;
}
int adb_jni_attach(JavaVM *vm, JNIEnv **env, bool *attached)
{
	*attached = false;
	jint result = (*vm)->GetEnv(vm, (void **)env, JNI_VERSION_1_6);
	if (result == JNI_OK)
		return 0;
	if (result != JNI_EDETACHED)
		return -1;
#ifdef __ANDROID__
	result = (*vm)->AttachCurrentThread(vm, env, NULL);
#else
	result = (*vm)->AttachCurrentThread(vm, (void **)env, NULL);
#endif
	*attached = result == JNI_OK;
	return result == JNI_OK ? 0 : -1;
}
char *adb_jni_text(JNIEnv *env, jstring value)
{
	if (!value)
		return NULL;
	jsize length = (*env)->GetStringLength(env, value);
	if (length > 4096)
		return NULL;
	const jchar *text = (*env)->GetStringChars(env, value, NULL);
	if (!text) {
		adb_jni_clear_exception(env);
		return NULL;
	}
	char *copy = malloc((size_t)length * 3 + 1);
	size_t used = 0;
	if (!copy)
		goto done;
	for (jsize i = 0; i < length; i++) {
		uint32_t cp = text[i];
		if (!cp) {
			adb_wipe(copy, used);
			free(copy);
			copy = NULL;
			goto done;
		}
		if (cp >= 0xd800 && cp <= 0xdbff && i + 1 < length &&
		    text[i + 1] >= 0xdc00 && text[i + 1] <= 0xdfff) {
			cp = 0x10000 + ((cp - 0xd800) << 10) +
			     (text[++i] - 0xdc00);
		} else if (cp >= 0xd800 && cp <= 0xdfff)
			cp = 0xfffd;
		if (cp < 128)
			copy[used++] = (char)cp;
		else if (cp < 2048) {
			copy[used++] = (char)(0xc0 | (cp >> 6));
			copy[used++] = (char)(0x80 | (cp & 63));
		} else if (cp < 65536) {
			copy[used++] = (char)(0xe0 | (cp >> 12));
			copy[used++] = (char)(0x80 | ((cp >> 6) & 63));
			copy[used++] = (char)(0x80 | (cp & 63));
		} else {
			copy[used++] = (char)(0xf0 | (cp >> 18));
			copy[used++] = (char)(0x80 | ((cp >> 12) & 63));
			copy[used++] = (char)(0x80 | ((cp >> 6) & 63));
			copy[used++] = (char)(0x80 | (cp & 63));
		}
	}
	copy[used] = 0;
	if (used > 4096) {
		adb_wipe(copy, used);
		free(copy);
		copy = NULL;
	}
done:
	(*env)->ReleaseStringChars(env, value, text);
	return copy;
}
void adb_jni_erase_array(JNIEnv *env, jbyteArray value)
{
	if (!value)
		return;
	jsize size = (*env)->GetArrayLength(env, value);
	jbyte zero[256] = { 0 };
	for (jsize offset = 0; offset < size; offset += 256) {
		jsize length = size - offset > 256 ? 256 : size - offset;
		(*env)->SetByteArrayRegion(env, value, offset, length, zero);
		if (adb_jni_clear_exception(env))
			break;
	}
}
