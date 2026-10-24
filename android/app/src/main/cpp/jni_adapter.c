#include "../../../../../ms912x_usb_ids.h"
#include "ms912x_modes_core.h"
#include "ms912x_edid_core.h"
#include "ms912x_control_core.h"
#include "ms912x_timing_core.h"
#include "../../../../../ms912x_capabilities.h"

#include <errno.h>
#include <jni.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

JNIEXPORT jboolean JNICALL
Java_io_github_ms912x_desktop_NativeAdapter_nativeRepairEdidBaseHeader(
	JNIEnv *env, jclass clazz, jbyteArray block)
{
	unsigned char bytes[128];
	jbyte repaired = (jbyte)0xff;
	(void)clazz;
	if (!block || (*env)->GetArrayLength(env, block) != (jsize)sizeof(bytes)) return JNI_FALSE;
	(*env)->GetByteArrayRegion(env, block, 0, sizeof(bytes), (jbyte *)bytes);
	if ((*env)->ExceptionCheck(env) ||
	    !ms912x_core_edid_repair_header_byte1(bytes, sizeof(bytes))) return JNI_FALSE;
	(*env)->SetByteArrayRegion(env, block, 1, 1, &repaired);
	return (*env)->ExceptionCheck(env) ? JNI_FALSE : JNI_TRUE;
}

JNIEXPORT jintArray JNICALL
Java_io_github_ms912x_desktop_NativeAdapter_nativeModeCatalog(
	JNIEnv *env, jclass clazz)
{
	size_t count = ms912x_core_mode_count();
	jintArray result;
	jint *values;
	size_t i;
	(void)clazz;
	if (count > (size_t)(INT32_MAX / 12))
		return NULL;
	values = calloc(count * 12, sizeof(*values));
	if (!values)
		return NULL;
	for (i = 0; i < count; ++i) {
		const struct ms912x_mode *mode = ms912x_core_mode_at(i);
		values[i * 12] = mode->vic;
		values[i * 12 + 1] = mode->width;
		values[i * 12 + 2] = mode->height;
		values[i * 12 + 3] = mode->hz;
		values[i * 12 + 4] = mode->clock_khz;
		values[i * 12 + 5] = mode->htotal;
		values[i * 12 + 6] = mode->vtotal;
		values[i * 12 + 7] = mode->hoffset;
		values[i * 12 + 8] = mode->voffset;
		values[i * 12 + 9] = mode->hsync_width;
		values[i * 12 + 10] = mode->vsync_width;
		values[i * 12 + 11] = mode->polarity;
	}
	result = (*env)->NewIntArray(env, (jsize)(count * 12));
	if (result)
		(*env)->SetIntArrayRegion(env, result, 0, (jsize)(count * 12), values);
	free(values);
	return result;
}

JNIEXPORT jboolean JNICALL
Java_io_github_ms912x_desktop_NativeAdapter_nativeIsSupportedDevice(
    JNIEnv *env, jclass clazz, jint vendor, jint product)
{
    (void)env; (void)clazz;
    return ms912x_usb_device_supported(vendor, product) ? JNI_TRUE : JNI_FALSE;
}

struct mode_plan { unsigned char reports[64]; unsigned int size; };
static int plan_write(void *context, unsigned char command, const unsigned char data[6])
{
    struct mode_plan *plan = context;
    if (plan->size + 8 > sizeof(plan->reports)) return -1;
    ms912x_build_control_write(plan->reports + plan->size, command, data);
    plan->size += 8;
    return 0;
}
static int plan_read(void *context, unsigned int address)
{
    struct mode_plan *plan = context;
    if (plan->size + 8 > sizeof(plan->reports)) return -1;
    ms912x_build_register_read(plan->reports + plan->size, address);
    plan->size += 8;
    return 0;
}
static jbyteArray control_bytes(JNIEnv *env, const unsigned char *bytes, unsigned int length)
{
    jbyteArray result = (*env)->NewByteArray(env, length);
    if (result) (*env)->SetByteArrayRegion(env, result, 0, length, (const jbyte *)bytes);
    return result;
}
JNIEXPORT jbyteArray JNICALL
Java_io_github_ms912x_desktop_NativeAdapter_nativeModePlan(JNIEnv *env, jclass clazz,
    jint vic, jint width, jint height)
{
    struct mode_plan plan = {0};
    const struct ms912x_control_ops ops = {plan_write, plan_read};
    (void)clazz;
    if (vic < 0 || vic > 255 || width <= 0 || width > 4095 || (width & 1) ||
        height <= 0 || height > 4095 || ms912x_program_mode(&plan, &ops, vic, width, height))
        return NULL;
    return control_bytes(env, plan.reports, plan.size);
}
JNIEXPORT jbyteArray JNICALL
Java_io_github_ms912x_desktop_NativeAdapter_nativeControlWrite(JNIEnv *env, jclass clazz,
    jint command, jbyteArray data)
{
    unsigned char payload[6], report[8];
    (void)clazz;
    if (command < 0 || command > 255 || !data || (*env)->GetArrayLength(env, data) != 6) return NULL;
    (*env)->GetByteArrayRegion(env, data, 0, 6, (jbyte *)payload);
    if ((*env)->ExceptionCheck(env)) return NULL;
    ms912x_build_control_write(report, command, payload);
    return control_bytes(env, report, 8);
}
JNIEXPORT jbyteArray JNICALL
Java_io_github_ms912x_desktop_NativeAdapter_nativeRegisterRead(JNIEnv *env, jclass clazz, jint address)
{
    unsigned char report[8];
    (void)clazz;
    if (address < 0 || address > 65535) return NULL;
    ms912x_build_register_read(report, address);
    return control_bytes(env, report, 8);
}
JNIEXPORT jboolean JNICALL
Java_io_github_ms912x_desktop_NativeAdapter_nativeSupportsSdram(JNIEnv *env, jclass clazz, jint value)
{
    enum ms912x_sdram_type type = ms912x_sdram_type_from_register(value);
    (void)env; (void)clazz;
    return type != MS912X_SDRAM_UNKNOWN && type != MS912X_SDRAM_NONE;
}

struct timing_java_io { JNIEnv *env; jobject session; jmethodID read, flash; };
static int java_register_read(void *context, unsigned int address)
{
    struct timing_java_io *io = context;
    jint value = (*io->env)->CallIntMethod(io->env, io->session, io->read, (jint)address);
    return (*io->env)->ExceptionCheck(io->env) ? -EIO : value;
}
static int java_flash_read(void *context, unsigned int address, void *data, unsigned int length)
{
    struct timing_java_io *io = context;
    jbyteArray bytes = (*io->env)->CallObjectMethod(io->env, io->session, io->flash,
                                                  (jint)address, (jint)length);
    if ((*io->env)->ExceptionCheck(io->env)) return -EIO;
    if (!bytes || (*io->env)->GetArrayLength(io->env, bytes) != (jsize)length) {
        if (bytes) (*io->env)->DeleteLocalRef(io->env, bytes);
        return -EIO;
    }
    (*io->env)->GetByteArrayRegion(io->env, bytes, 0, length, data);
    (*io->env)->DeleteLocalRef(io->env, bytes);
    return (*io->env)->ExceptionCheck(io->env) ? -EIO : 0;
}
static int java_timing_io(JNIEnv *env, jobject session, struct timing_java_io *io)
{
    jclass type;
    if (!session) return 0;
    type = (*env)->GetObjectClass(env, session);
    io->env = env; io->session = session;
    io->read = (*env)->GetMethodID(env, type, "readRegister", "(I)I");
    io->flash = (*env)->GetMethodID(env, type, "readFlash", "(II)[B");
    (*env)->DeleteLocalRef(env, type);
    return io->read && io->flash && !(*env)->ExceptionCheck(env);
}
JNIEXPORT jbyteArray JNICALL
Java_io_github_ms912x_desktop_NativeAdapter_nativeReadEdidBlock(JNIEnv *env, jclass clazz,
                                                       jobject session, jint block)
{
    struct timing_java_io io;
    unsigned char bytes[128];
    (void)clazz;
    if (block < 0 || block > 8 || !java_timing_io(env, session, &io)) return NULL;
    if (ms912x_core_read_edid_bytes(&io, java_register_read, block, bytes, sizeof(bytes))) return NULL;
    if (block == 0 && !ms912x_core_edid_header_valid(bytes, sizeof(bytes)) &&
        !ms912x_core_edid_repair_header_byte1(bytes, sizeof(bytes))) return NULL;
    if (!ms912x_core_edid_checksum_valid(bytes, sizeof(bytes))) return NULL;
    return control_bytes(env, bytes, sizeof(bytes));
}
JNIEXPORT jbyteArray JNICALL
Java_io_github_ms912x_desktop_NativeAdapter_nativeFlashReadRequest(JNIEnv *env, jclass clazz, jint address)
{
    unsigned char report[8];
    (void)clazz;
    if (address < 0 || address > 0xffffff) return NULL;
    ms912x_build_flash_read(report, address);
    return control_bytes(env, report, 8);
}
JNIEXPORT jintArray JNICALL
Java_io_github_ms912x_desktop_NativeAdapter_nativeCustomModes(JNIEnv *env, jclass clazz, jobject session)
{
    struct timing_java_io io;
    const struct ms912x_timing_io ops = {java_register_read, java_flash_read};
    struct ms912x_mode modes[2];
    jint values[24];
    int count, i;
    jintArray result;
    (void)clazz;
    if (!java_timing_io(env, session, &io)) return NULL;
    count = ms912x_custom_timing_load(&io, &ops, modes);
    if ((*env)->ExceptionCheck(env)) return NULL;
    if (count < 0) count = 0;
    for (i = 0; i < count; ++i) {
        const struct ms912x_mode *m = &modes[i];
        jint fields[12] = {m->vic, m->width, m->height, m->hz, m->clock_khz, m->htotal,
                          m->vtotal, m->hoffset, m->voffset, m->hsync_width, m->vsync_width, m->polarity};
        memcpy(values + i * 12, fields, sizeof(fields));
    }
    result = (*env)->NewIntArray(env, count * 12);
    if (result && count) (*env)->SetIntArrayRegion(env, result, 0, count * 12, values);
    return result;
}
