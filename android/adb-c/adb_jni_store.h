/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MS912X_ADB_JNI_STORE_H
#define MS912X_ADB_JNI_STORE_H
#include "adb_jni_util.h"
struct adb_jni_store {
	JavaVM *vm;
	jobject object;
	jmethodID load_key, save_key, save_device, load_device, is_local;
};
int adb_jni_store_init(JNIEnv *, jobject, struct adb_jni_store *);
void adb_jni_store_free(JNIEnv *, struct adb_jni_store *);
struct adb_store adb_jni_store_callbacks(struct adb_jni_store *);
int adb_jni_local(JNIEnv *, struct adb_jni_store *, const char *);
#endif
