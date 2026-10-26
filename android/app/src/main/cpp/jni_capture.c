#include "frame_pipeline.h"
#include "ms912x_mode3_core.h"

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
Java_io_github_ms912x_desktop_NativeCapture_nativeSetOptions(
    JNIEnv *env, jclass clazz, jlong handle, jint profile, jint stride, jint tolerance_min,
    jint tolerance_max, jboolean scan_enabled)
{
    (void)env; (void)clazz;
    if (profile < 0 || stride < 0) return -1;
    struct capture_entry *entry = acquire_entry(handle);
    int result = entry ? ms912x_android_frame_pipeline_set_tolerance_range(entry->pipeline,
        (unsigned int)profile, (unsigned int)stride, tolerance_min, tolerance_max,
        scan_enabled != JNI_FALSE) : -4;
    release_entry(entry);
    return result;
}

JNIEXPORT jintArray JNICALL
Java_io_github_ms912x_desktop_NativeCapture_nativeProfileDefaults(JNIEnv *env, jclass clazz, jint profile)
{
    (void)clazz;
    struct ms912x_android_scan_options options;
    if (profile < 0 || ms912x_android_profile_defaults((unsigned int)profile, &options)) return NULL;
    jint values[] = {(jint)options.profile, (jint)options.stride,
        (jint)options.tolerance_min, (jint)options.tolerance_max};
    jintArray result = (*env)->NewIntArray(env, 4);
    if (result) (*env)->SetIntArrayRegion(env, result, 0, 4, values);
    return result;
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
