/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MS912X_CURSOR_ENTRY_TEST_COMPAT_H
#define MS912X_CURSOR_ENTRY_TEST_COMPAT_H

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

typedef int64_t s64;
#define DRM_RECT_INIT(x, y, w, h) ((struct drm_rect){(x), (y), (x) + (w), (y) + (h)})
#define MS912X_MAX_WIDTH 1920
#define MS912X_MAX_HEIGHT 1200
#define DRM_MODE_BLEND_COVERAGE 2
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define ALIGN(v, a) (((v) + (a) - 1) & ~((a) - 1))
#define ALIGN_DOWN(v, a) ((v) & ~((a) - 1))
#define DIV_ROUND_UP(v, d) (((v) + (d) - 1) / (d))
#define div_s64(v, d) ((v) / (d))
#define swap(a, b) do { __typeof__(a) tmp = (a); (a) = (b); (b) = tmp; } while (0)
#define dev_warn_ratelimited(d, ...) ((void)(d))

#include "ms912x_scale.h"

struct drm_device { int unused; };
struct drm_framebuffer {
    struct { u32 id; } base;
    unsigned int width, height;
    const u32 *pixels;
};
struct drm_plane_state {
    struct drm_framebuffer *fb;
    void *crtc;
    bool visible;
    int crtc_x, crtc_y;
    unsigned int src_x, src_y;
    unsigned int pixel_blend_mode;
};
struct test_mutex { pthread_mutex_t native; unsigned int bit; };
struct ms912x_usb_request {
    bool cursor_enabled, cursor_included, cursor_premultiplied;
    int cursor_x, cursor_y;
    unsigned int cursor_width, cursor_height, cursor_alpha_x, cursor_alpha_y;
    unsigned int cursor_source_height;
    u32 cursor_pixels[128 * 128];
};
struct ms912x_device {
    struct drm_device drm;
    struct { int dev; } *intf;
    struct test_mutex transfer_lock, cursor_prepare_lock, submit_lock, scan_queue_lock;
    bool cursor_plane_enabled, transfer_stopping;
    u32 *cursor_buffer_storage, *cursor_source_pixels, *cursor_staging_pixels;
    u32 *cursor_prepare_pixels, *cursor_pixels;
    enum ms912x_scale_mode scale_mode, cursor_scale_mode;
    u32 frame_generation;
    unsigned int frame_snapshot_width, frame_snapshot_height;
    bool cursor_enabled, cursor_plane_active, cursor_premultiplied;
    int cursor_x, cursor_y, cursor_plane_x, cursor_plane_y;
    unsigned int cursor_width, cursor_height, cursor_source_height;
    unsigned int cursor_plane_width, cursor_plane_height, cursor_fb_id;
    unsigned int cursor_alpha_x, cursor_alpha_y;
    bool cursor_commit_pending;
    u64 cursor_commit_sequence, published_frame_sequence;
    struct drm_rect pending_rects[32];
    unsigned int pending_rect_count, pending_width, pending_height;
    unsigned int pending_deadline;
    bool pending_valid, pending_force_full, present_backpressured, failed_rect_valid;
    bool scan_stopping, scan_work_scheduled;
    u32 scan_epoch;
    unsigned int previous_damage_rect_count;
    u64 present_queue_pressure_ns, present_queue_low_watermark_ns, present_queue_high_watermark_ns;
    u64 drm_released_sequence;
    int conversion_wait, frame_present_wait, scan_frame_wait;
};

static inline bool drm_rect_intersect(struct drm_rect *r, const struct drm_rect *b)
{
    r->x1 = max(r->x1, b->x1); r->y1 = max(r->y1, b->y1);
    r->x2 = min(r->x2, b->x2); r->y2 = min(r->y2, b->y2);
    return r->x1 < r->x2 && r->y1 < r->y2;
}
static inline bool drm_rect_equals(const struct drm_rect *a, const struct drm_rect *b)
{ return !memcmp(a, b, sizeof(*a)); }

u64 ms912x_cursor_composite_uyvy(void *, const struct drm_rect *, const u32 *,
    int, int, unsigned int, unsigned int, unsigned int, unsigned int, bool);

#endif
