/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MS912X_ADB_JNI_UTIL_H
#define MS912X_ADB_JNI_UTIL_H
#include "adb.h"
#include <jni.h>
#define JNI_NAME(name)                                                         \
	Java_io_github_taglia_1ti_ms912x_1ng_desktop_NativeAdbBridge_##name
char *adb_jni_text(JNIEnv *, jstring);
bool adb_jni_clear_exception(JNIEnv *);
jstring adb_jni_result(JNIEnv *, const char *);
jstring adb_jni_string(JNIEnv *, const char *);
int adb_jni_attach(JavaVM *, JNIEnv **, bool *);
void adb_jni_erase_array(JNIEnv *, jbyteArray);
#endif
