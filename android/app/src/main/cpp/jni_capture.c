#include "frame_pipeline.h"

#include <jni.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>

struct capture_entry {
	jlong handle;
	struct ms912x_android_frame_pipeline *pipeline;
	struct capture_entry *next;
};

static pthread_mutex_t entries_lock = PTHREAD_MUTEX_INITIALIZER;
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
	return entry->handle;
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
	pthread_mutex_lock(&entries_lock);
	entry = find_entry(handle);
	result = entry ? ms912x_android_frame_pipeline_reconfigure(
		entry->pipeline, (uint32_t)width, (uint32_t)height,
		(uint64_t)generation) : -4;
	pthread_mutex_unlock(&entries_lock);
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
	pthread_mutex_lock(&entries_lock);
	entry = find_entry(handle);
	result = entry ? ms912x_android_frame_pipeline_offer_rgba(
		entry->pipeline, data, (size_t)capacity, (size_t)position,
		(size_t)row_stride, (size_t)pixel_stride, (uint32_t)crop_x,
		(uint32_t)crop_y, (uint32_t)width, (uint32_t)height,
		(uint64_t)generation) : -4;
	pthread_mutex_unlock(&entries_lock);
	return result;
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
	pthread_mutex_unlock(&entries_lock);
	if (entry) {
		ms912x_android_frame_pipeline_destroy(entry->pipeline);
		free(entry);
	}
}
