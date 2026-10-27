/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef DAMAGE_TEST_COMPAT_H
#define DAMAGE_TEST_COMPAT_H
#define MS912X_H
#include <assert.h>
#include <errno.h>
#include <stdio.h>
typedef uint16_t u16;
typedef uint32_t u32;
typedef u32 __le32;
typedef u16 __be16;
#define __packed __attribute__((packed))
#define MS912X_MAX_WIDTH 1920
#define MS912X_MAX_HEIGHT 1200
#define MS912X_CURSOR_MAX_WIDTH 128
#define MS912X_CURSOR_MAX_HEIGHT 128
#define MS912X_FRAME_OVERHEAD 16
#define MS912X_MAX_TRANSFER_LEN (MS912X_MAX_WIDTH * MS912X_MAX_HEIGHT * 2 + MS912X_FRAME_OVERHEAD)
#define NSEC_PER_SEC 1000000000ULL
#define div64_u64(a, b) ((a) / (b))
#define clamp_t(type, value, low, high) min_t(type, max_t(type, value, low), high)
#define GFP_KERNEL 0
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define READ_ONCE(a) (a)
#define kvzalloc(n, f) calloc(1, n)
#define kvrealloc(old, n, flags) realloc((void *)(old), n)
#define ms912x_kvrealloc(old, old_size, n, flags) kvrealloc(old, n, flags)
#define kvfree(p) free(p)
struct drm_rect *ms912x_rect_vector_grow(struct drm_rect *, unsigned int *, unsigned int, unsigned int);
enum ms912x_request_state { MS912X_REQUEST_FREE, MS912X_REQUEST_PREPARING,
                           MS912X_REQUEST_CONVERTING,
                           MS912X_REQUEST_USB_QUEUED, MS912X_REQUEST_USB_ACTIVE };
struct work_struct { int unused; };
struct delayed_work { struct work_struct work; };
struct usb_device { int unused; };
struct usb_sg_request { int unused; };
struct sg_table { int unused; };
struct ms912x_device;
struct ms912x_usb_request {
    struct ms912x_device *ms912x;
    struct drm_rect *rects;
    struct drm_rect *current_rects;
    struct ms912x_compact_index *compact_index;
    unsigned int current_rect_count, current_rect_capacity;
    unsigned int rect_count, rect_capacity, width, height, generation;
    u64 content_generation, source_generation;
    enum ms912x_request_state state;
    struct work_struct work;
    struct sg_table transfer_sgt;
    struct usb_sg_request sgr;
    void *transfer_buffer;
    size_t transfer_len;
    bool cursor_included, cursor_enabled, cursor_premultiplied;
    bool replay_included;
    int cursor_x, cursor_y;
    unsigned int cursor_width, cursor_height, cursor_alpha_x, cursor_alpha_y, cursor_source_height;
    u32 cursor_pixels[128 * 128];
};
struct ms912x_device {
    struct ms912x_usb_request requests[2];
    u32 frame_generation, *latest_pixels, *conversion_pixels;
    u64 published_frame_sequence;
    u64 usb_speed_mean_bytes_per_sec, usb_speed_samples, usb_successful_transfers;
    bool transfer_stopping, conversion_busy, cursor_commit_pending, pending_valid;
    bool pending_force_full;
    struct drm_rect *pending_rects, *previous_damage_rects;
    struct drm_rect *payload_compare_rects;
    unsigned int pending_rect_count, pending_rect_capacity, pending_width, pending_height;
    unsigned int previous_damage_rect_count, previous_damage_rect_capacity;
    unsigned int previous_damage_width, previous_damage_height, previous_damage_generation;
    u64 failed_present_sequence;
    struct delayed_work conversion_work;
    void *workqueue;
    u64 pending_deadline;
    _Atomic int transfer_lock, snapshot_lock, damage_scan_lock;
    int conversion_wait, frame_present_wait, drm, intf;
    bool failed_rect_valid;
    unsigned int failed_generation, failed_width, failed_height;
    struct drm_rect failed_rect;
    struct {
        u64 conversion_ns, conversion_attempts, conversion_successes, conversion_pixels;
        u64 cursor_composite_packets, cursor_composite_pixels, damage_replay_batches_queued;
        u64 damage_replay_batches_sent, usb_requests_requeued_before_send;
        u64 usb_errors, usb_timeouts, usb_short_transfers;
    } stats;
    bool cursor_enabled, cursor_premultiplied;
    int cursor_x, cursor_y;
    unsigned int cursor_width, cursor_height, cursor_alpha_x, cursor_alpha_y, cursor_source_height;
    u32 *cursor_pixels;
};
#ifdef MS912X_TEST_PAYLOAD_COMPARE_HOOK
void mutex_lock(_Atomic int *lock);
void mutex_unlock(_Atomic int *lock);
bool ms912x_test_lock_owned(const _Atomic int *lock);
int ms912x_test_payload_memcmp(const void *old, const void *frame, size_t bytes);
#define lockdep_assert_held(lock) assert(ms912x_test_lock_owned(lock))
#define lockdep_assert_not_held(lock) assert(!ms912x_test_lock_owned(lock))
#define WARN_ON_ONCE(condition) (condition)
#endif
#endif
