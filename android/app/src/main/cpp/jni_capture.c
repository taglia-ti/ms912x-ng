#include "frame_pipeline.h"
#include "../../../../../ms912x_usb_ids.h"
#include "ms912x_mode3_core.h"
#include "ms912x_modes_core.h"
#include "ms912x_edid_core.h"
#include "ms912x_control_core.h"
#include "ms912x_timing_core.h"
#include "../../../../../ms912x_capabilities.h"

#include <jni.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>

struct capture_entry {
	jlong handle;
	struct ms912x_android_frame_pipeline *pipeline;
	struct capture_entry *next;
	unsigned int users;
};

static pthread_mutex_t entries_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t entries_idle = PTHREAD_COND_INITIALIZER;
static struct capture_entry *entries;
static jlong next_handle = 1;

JNIEXPORT jboolean JNICALL
Java_io_github_ms912x_desktop_NativeCapture_repairEdidBaseHeader(
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

static struct capture_entry *find_entry(jlong handle)
{
	struct capture_entry *entry;

	for (entry = entries; entry; entry = entry->next) {
		if (entry->handle == handle)
			return entry;
	}
	return NULL;
}

/* The registry pins lifetimes, never pixel work. Each pipeline owns its own locks. */
static struct capture_entry *acquire_entry(jlong handle)
{
	struct capture_entry *entry;

	pthread_mutex_lock(&entries_lock);
	entry = find_entry(handle);
	if (entry)
		++entry->users;
	pthread_mutex_unlock(&entries_lock);
	return entry;
}

static void release_entry(struct capture_entry *entry)
{
	if (!entry)
		return;
	pthread_mutex_lock(&entries_lock);
	if (--entry->users == 0)
		pthread_cond_broadcast(&entries_idle);
	pthread_mutex_unlock(&entries_lock);
}

JNIEXPORT jlong JNICALL
Java_io_github_ms912x_desktop_NativeCapture_nativeCreate(
	JNIEnv *env, jclass clazz, jint width, jint height, jlong generation)
{
	struct capture_entry *entry;
	jlong candidate;
	(void)env;
	(void)clazz;
	if (width <= 0 || height <= 0 || generation <= 0)
		return 0;
	entry = calloc(1, sizeof(*entry));
	if (!entry)
		return 0;
	entry->pipeline = ms912x_android_frame_pipeline_create(
		(uint32_t)width, (uint32_t)height, (uint64_t)generation);
	if (!entry->pipeline) {
		free(entry);
		return 0;
	}
	pthread_mutex_lock(&entries_lock);
	candidate = next_handle;
	if (candidate <= 0 || find_entry(candidate))
		candidate = 1;
	while (find_entry(candidate)) {
		if (candidate == INT64_MAX) {
			candidate = 1;
		} else {
			++candidate;
		}
		if (candidate == next_handle) {
			pthread_mutex_unlock(&entries_lock);
			ms912x_android_frame_pipeline_destroy(entry->pipeline);
			free(entry);
			return 0;
		}
	}
	entry->handle = candidate;
	next_handle = candidate == INT64_MAX ? 1 : candidate + 1;
	entry->next = entries;
	entries = entry;
	pthread_mutex_unlock(&entries_lock);
	return candidate;
}

JNIEXPORT jint JNICALL
Java_io_github_ms912x_desktop_NativeCapture_nativeReconfigure(
	JNIEnv *env, jclass clazz, jlong handle, jint width, jint height,
	jlong generation)
{
	struct capture_entry *entry;
	int result;
	(void)env;
	(void)clazz;
	if (width <= 0 || height <= 0 || generation <= 0)
		return -1;
	entry = acquire_entry(handle);
	result = entry ? ms912x_android_frame_pipeline_reconfigure(
		entry->pipeline, (uint32_t)width, (uint32_t)height,
		(uint64_t)generation) : -4;
	release_entry(entry);
	return result;
}

JNIEXPORT jint JNICALL
Java_io_github_ms912x_desktop_NativeCapture_nativeOfferRgba(
	JNIEnv *env, jclass clazz, jlong handle, jobject buffer,
	jint position, jint capacity, jint row_stride, jint pixel_stride,
	jint crop_x, jint crop_y, jint width, jint height, jlong generation)
{
	struct capture_entry *entry;
	uint8_t *data;
	int result;
	(void)clazz;
	if (!buffer || position < 0 || capacity < 0 || position > capacity ||
	    row_stride <= 0 || pixel_stride <= 0 || crop_x < 0 || crop_y < 0 ||
	    width <= 0 || height <= 0 || generation <= 0)
		return -1;
	data = (*env)->GetDirectBufferAddress(env, buffer);
	if (!data || (*env)->GetDirectBufferCapacity(env, buffer) < capacity)
		return -1;
	entry = acquire_entry(handle);
	result = entry ? ms912x_android_frame_pipeline_offer_rgba(
		entry->pipeline, data, (size_t)capacity, (size_t)position,
		(size_t)row_stride, (size_t)pixel_stride, (uint32_t)crop_x,
		(uint32_t)crop_y, (uint32_t)width, (uint32_t)height,
		(uint64_t)generation) : -4;
	release_entry(entry);
	return result;
}

static jint encode_java(JNIEnv *env, jlong handle, jobject buffer, jlongArray sequence_out,
                        jlong now_ns, jint refresh, int paced)
{
	struct capture_entry *entry;
	struct ms912x_android_frame_info info;
	uint8_t *destination;
	jlong capacity;
	int result;

	if (!buffer || !sequence_out || (*env)->GetArrayLength(env, sequence_out) < (paced ? 2 : 1))
		return -1;
	destination = (*env)->GetDirectBufferAddress(env, buffer);
	capacity = (*env)->GetDirectBufferCapacity(env, buffer);
	if (!destination || capacity <= 0)
		return -1;
	entry = acquire_entry(handle);
	if (!entry)
		return -4;
    if (paced) {
        uint64_t wait = 0;
        result = ms912x_android_frame_pipeline_encode_paced(entry->pipeline, destination,
            capacity, &info, now_ns, refresh, &wait);
        if (result >= 0) {
            jlong values[2] = {result > 0 ? (jlong)info.sequence : 0, (jlong)wait};
            (*env)->SetLongArrayRegion(env, sequence_out, 0, 2, values);
        }
    } else {
        result = ms912x_android_frame_pipeline_encode_latest(entry->pipeline, destination, capacity, &info);
        if (result > 0) {
            jlong sequence = info.sequence;
            (*env)->SetLongArrayRegion(env, sequence_out, 0, 1, &sequence);
        }
    }
    if ((*env)->ExceptionCheck(env)) {
        if (result > 0) ms912x_android_frame_pipeline_complete(entry->pipeline, info.sequence, 0);
        result = -1;
    }

	release_entry(entry);
	return result;
}

JNIEXPORT jint JNICALL
Java_io_github_ms912x_desktop_NativeCapture_nativeEncodeLatest(
    JNIEnv *env, jclass clazz, jlong handle, jobject buffer, jlongArray sequence_out)
{
    (void)clazz;
    return encode_java(env, handle, buffer, sequence_out, 0, 0, 0);
}
JNIEXPORT jint JNICALL
Java_io_github_ms912x_desktop_NativeCapture_nativeEncodePaced(
    JNIEnv *env, jclass clazz, jlong handle, jobject buffer, jlongArray sequence_out,
    jlong now_ns, jint refresh)
{
    (void)clazz;
    return encode_java(env, handle, buffer, sequence_out, now_ns, refresh, 1);
}
JNIEXPORT void JNICALL
Java_io_github_ms912x_desktop_NativeCapture_nativeRecordUsb(
    JNIEnv *env, jclass clazz, jlong handle, jint bytes, jlong duration)
{
    struct capture_entry *entry = acquire_entry(handle);
    (void)env; (void)clazz;
    if (entry && bytes > 0 && duration > 0)
        ms912x_android_frame_pipeline_record_usb(entry->pipeline, bytes, duration);
    release_entry(entry);
}

JNIEXPORT jint JNICALL
Java_io_github_ms912x_desktop_NativeCapture_nativeCompleteBatch(
	JNIEnv *env, jclass clazz, jlong handle, jlong sequence, jboolean transferred)
{
	struct capture_entry *entry = acquire_entry(handle);
	int result;
	(void)env;
	(void)clazz;
	result = entry ? ms912x_android_frame_pipeline_complete(
		entry->pipeline, (uint64_t)sequence, transferred == JNI_TRUE) : -4;
	release_entry(entry);
	return result;
}

JNIEXPORT jstring JNICALL
Java_io_github_ms912x_desktop_NativeCapture_nativeConversionBackend(
	JNIEnv *env, jclass clazz, jlong handle)
{
	struct capture_entry *entry = acquire_entry(handle);
	const char *name = entry ? ms912x_android_frame_pipeline_backend(entry->pipeline) : "closed";
	(void)clazz;
	release_entry(entry);
	return (*env)->NewStringUTF(env, name);
}

JNIEXPORT jint JNICALL
Java_io_github_ms912x_desktop_NativeCapture_nativePacketCapacity(
	JNIEnv *env, jclass clazz, jlong handle)
{
	struct capture_entry *entry = acquire_entry(handle);
	size_t capacity = entry ? ms912x_android_frame_pipeline_packet_capacity(entry->pipeline) : 0;
	(void)env;
	(void)clazz;
	release_entry(entry);
	return capacity <= INT32_MAX ? (jint)capacity : 0;
}

JNIEXPORT void JNICALL
Java_io_github_ms912x_desktop_NativeCapture_nativeInvalidateBaseline(
	JNIEnv *env, jclass clazz, jlong handle)
{
	struct capture_entry *entry;
	(void)env;
	(void)clazz;
	entry = acquire_entry(handle);
	if (entry)
		ms912x_android_frame_pipeline_invalidate_baseline(entry->pipeline);
	release_entry(entry);
}

JNIEXPORT void JNICALL
Java_io_github_ms912x_desktop_NativeCapture_nativeSetShowDraws(
	JNIEnv *env, jclass clazz, jlong handle, jboolean enabled)
{
	struct capture_entry *entry = acquire_entry(handle);
	(void)env;
	(void)clazz;
	if (entry) ms912x_android_frame_pipeline_set_show_draws(entry->pipeline, enabled == JNI_TRUE);
	release_entry(entry);
}

JNIEXPORT void JNICALL
Java_io_github_ms912x_desktop_NativeCapture_nativeDestroy(
	JNIEnv *env, jclass clazz, jlong handle)
{
	struct capture_entry **link;
	struct capture_entry *entry = NULL;
	(void)env;
	(void)clazz;
	pthread_mutex_lock(&entries_lock);
	for (link = &entries; *link; link = &(*link)->next) {
		if ((*link)->handle == handle) {
			entry = *link;
			*link = entry->next;
			break;
		}
	}
	while (entry && entry->users)
		pthread_cond_wait(&entries_idle, &entries_lock);
	pthread_mutex_unlock(&entries_lock);
	if (entry) {
		ms912x_android_frame_pipeline_destroy(entry->pipeline);
		free(entry);
	}
}

JNIEXPORT jintArray JNICALL
Java_io_github_ms912x_desktop_NativeCapture_nativeModeCatalog(
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

JNIEXPORT jint JNICALL
Java_io_github_ms912x_desktop_NativeTestPattern_nativeEncode640x480(
	JNIEnv *env, jclass clazz, jobject buffer)
{
	const uint32_t width = 640;
	const uint32_t height = 480;
	const size_t packet_bytes = ms912x_core_mode3_frame_size(width, height);
	uint8_t *destination;
	jlong capacity;
	int result;
	(void)clazz;
	if (!buffer)
		return -1;
	destination = (*env)->GetDirectBufferAddress(env, buffer);
	capacity = (*env)->GetDirectBufferCapacity(env, buffer);
	if (!destination || capacity < 0 || (uint64_t)capacity < packet_bytes)
		return -1;
	result = ms912x_core_mode3_generate_color_bars(destination,
							(size_t)capacity,
							width, height);
	return result;
}

JNIEXPORT jboolean JNICALL
Java_io_github_ms912x_desktop_NativeCapture_isSupportedDevice(
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
Java_io_github_ms912x_desktop_NativeCapture_modePlan(JNIEnv *env, jclass clazz,
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
Java_io_github_ms912x_desktop_NativeCapture_controlWrite(JNIEnv *env, jclass clazz,
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
Java_io_github_ms912x_desktop_NativeCapture_registerRead(JNIEnv *env, jclass clazz, jint address)
{
    unsigned char report[8];
    (void)clazz;
    if (address < 0 || address > 65535) return NULL;
    ms912x_build_register_read(report, address);
    return control_bytes(env, report, 8);
}
JNIEXPORT jboolean JNICALL
Java_io_github_ms912x_desktop_NativeCapture_supportsSdram(JNIEnv *env, jclass clazz, jint value)
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
Java_io_github_ms912x_desktop_NativeCapture_readEdidBlock(JNIEnv *env, jclass clazz,
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
Java_io_github_ms912x_desktop_NativeCapture_flashReadRequest(JNIEnv *env, jclass clazz, jint address)
{
    unsigned char report[8];
    (void)clazz;
    if (address < 0 || address > 0xffffff) return NULL;
    ms912x_build_flash_read(report, address);
    return control_bytes(env, report, 8);
}
JNIEXPORT jintArray JNICALL
Java_io_github_ms912x_desktop_NativeCapture_nativeCustomModes(JNIEnv *env, jclass clazz, jobject session)
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
