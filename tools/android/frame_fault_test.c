/* SPDX-License-Identifier: GPL-2.0-only */
#include "frame_pipeline.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

void *__real_malloc(size_t);
void *__real_calloc(size_t, size_t);
int __real_pthread_mutex_init(pthread_mutex_t *, const pthread_mutexattr_t *);
static size_t allocation, fail_allocation, mutex_call, fail_mutex;
static bool tracking;

void *__wrap_malloc(size_t bytes)
{
    if (tracking && ++allocation == fail_allocation) return NULL;
    return __real_malloc(bytes);
}
void *__wrap_calloc(size_t n, size_t bytes)
{
    if (tracking && ++allocation == fail_allocation) return NULL;
    return __real_calloc(n, bytes);
}
int __wrap_pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr)
{
    if (tracking && ++mutex_call == fail_mutex) return EAGAIN;
    return __real_pthread_mutex_init(mutex, attr);
}
static void begin(size_t fail, size_t mutex)
{
    allocation = mutex_call = 0; fail_allocation = fail; fail_mutex = mutex; tracking = true;
}
static void verify_old_session(struct ms912x_android_frame_pipeline *pipeline)
{
    uint8_t rgba[32] = {0}, packet[64];
    struct ms912x_android_frame_info info;
    assert(ms912x_android_frame_pipeline_offer_rgba(pipeline, rgba, 32, 0, 16, 4, 0, 0, 4, 2, 1) == 0);
    assert(ms912x_android_frame_pipeline_encode_latest(pipeline, packet, 64, &info) == 32);
    assert(info.width == 4 && info.height == 2 && info.generation == 1);
    assert(ms912x_android_frame_pipeline_complete(pipeline, info.sequence, 1) == 0);
}
int main(void)
{
    begin(0, 0);
    struct ms912x_android_frame_pipeline *pipeline = ms912x_android_frame_pipeline_create(4, 2, 1);
    size_t create_allocations = allocation;
    tracking = false; assert(pipeline && create_allocations > 0);
    begin(0, 0);
    assert(ms912x_android_frame_pipeline_reconfigure(pipeline, 2, 2, 2) == 0);
    size_t reconfigure_allocations = allocation;
    tracking = false; ms912x_android_frame_pipeline_destroy(pipeline);
    for (size_t fail = 1; fail <= create_allocations; ++fail) {
        begin(fail, 0);
        pipeline = ms912x_android_frame_pipeline_create(4, 2, 1);
        tracking = false; assert(!pipeline);
    }
    for (size_t mutex = 1; mutex <= 2; ++mutex) {
        begin(0, mutex);
        pipeline = ms912x_android_frame_pipeline_create(4, 2, 1);
        tracking = false; assert(!pipeline);
    }
    for (size_t fail = 1; fail <= reconfigure_allocations; ++fail) {
        pipeline = ms912x_android_frame_pipeline_create(4, 2, 1); assert(pipeline);
        begin(fail, 0);
        assert(ms912x_android_frame_pipeline_reconfigure(pipeline, 2, 2, 2) == -3);
        tracking = false;
        verify_old_session(pipeline);
        assert(ms912x_android_frame_pipeline_reconfigure(pipeline, 2, 2, 2) == 0);
        ms912x_android_frame_pipeline_destroy(pipeline);
    }
    printf("Native allocation and mutex fault fixtures passed: %zu create, %zu reconfigure, 2 mutex failures\n",
           create_allocations, reconfigure_allocations);
    return 0;
}
