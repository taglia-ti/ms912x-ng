// SPDX-License-Identifier: GPL-2.0-only
/* Real cursor preparation/publication with mocked framebuffer and filter I/O. */
#include <stdio.h>
#include <time.h>
#include "test_cursor_entry_compat.h"

static int source_memcmp(const void *, const void *, size_t);
#define memcmp source_memcmp
#include "ms912x_cursor.h"
#undef memcmp

enum { PREPARE = 1, TRANSFER = 2, SCAN = 4, SUBMIT = 8, PIXELS = 128 * 128 };
static struct ms912x_device device;
static u32 raw[PIXELS];
static struct drm_framebuffer framebuffer;
static struct drm_plane_state state;
static unsigned int cases, copies, comparisons, preparations, downscales, schedules;
static unsigned int invalidations, hook_action, concurrent_action;
static int copy_error;
static bool allocation_failure, action_failure;
static unsigned int allocations, frees;
static void (*release_action)(struct drm_device *, void *);
static void *release_data;
static _Thread_local unsigned int owned;
static _Thread_local bool stopping_task;
static pthread_t worker;
static pthread_mutex_t probe_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t probe_cond = PTHREAD_COND_INITIALIZER;
static bool worker_waiting, worker_done;
static struct ms912x_usb_request snapshot;
static u32 previous_pixels[PIXELS];
static int previous_x, previous_y;

static void signal_worker_waiting(void)
{
    assert(!pthread_mutex_lock(&probe_mutex));
    worker_waiting = true;
    assert(!pthread_cond_signal(&probe_cond));
    assert(!pthread_mutex_unlock(&probe_mutex));
}

static void mutex_lock(struct test_mutex *lock)
{
    if (lock->bit == PREPARE) assert(!(owned & TRANSFER));
    if (lock->bit == PREPARE && stopping_task) {
        assert(!owned);
        assert(pthread_mutex_trylock(&lock->native) == EBUSY);
        signal_worker_waiting();
    }
    assert(!pthread_mutex_lock(&lock->native));
    assert(!(owned & lock->bit)); owned |= lock->bit;
}
static void mutex_unlock(struct test_mutex *lock)
{
    assert(owned & lock->bit); owned &= ~lock->bit;
    assert(!pthread_mutex_unlock(&lock->native));
}
static void *vzalloc(size_t bytes)
{
    assert(bytes == 4 * PIXELS * sizeof(u32)); allocations++;
    return allocation_failure ? NULL : calloc(1, bytes);
}
static void vfree(void *data)
{
    assert(data == device.cursor_buffer_storage); frees++; free(data);
}
static int drmm_add_action_or_reset(struct drm_device *drm,
    void (*action)(struct drm_device *, void *), void *data)
{
    if (action_failure) { action(drm, data); return -EIO; }
    release_action = action; release_data = data; return 0;
}
#include "cursor-buffers.h"

static void *concurrent_update(void *argument);
static void preparation_hook(void)
{
    unsigned int action = hook_action;
    hook_action = 0;
    if (!action) return;
    assert(owned == PREPARE);
    if (action >= 5) {
        concurrent_action = action;
        worker_waiting = worker_done = false;
        memcpy(previous_pixels, device.cursor_pixels, sizeof(previous_pixels));
        previous_x = device.cursor_x; previous_y = device.cursor_y;
        assert(!pthread_create(&worker, NULL, concurrent_update, NULL));
        assert(!pthread_mutex_lock(&probe_mutex));
        struct timespec deadline;
        assert(!clock_gettime(CLOCK_REALTIME, &deadline)); deadline.tv_sec += 5;
        while (!worker_waiting) assert(!pthread_cond_timedwait(&probe_cond, &probe_mutex, &deadline));
        assert(!pthread_mutex_unlock(&probe_mutex));
        return;
    }
    mutex_lock(&device.transfer_lock);
    if (action == 1) device.transfer_stopping = true;
    if (action == 2) { device.scale_mode = MS912X_SCALE_MODE_DOWNSCALE_2_TO_1; device.frame_generation++; }
    if (action == 3) device.frame_generation++;
    if (action == 4) {
        device.pending_rects[0] = DRM_RECT_INIT(100, 100, 8, 8);
        device.pending_rect_count = 1; device.pending_deadline = 99;
    }
    mutex_unlock(&device.transfer_lock);
}
static int source_memcmp(const void *left, const void *right, size_t bytes)
{
    assert(owned == PREPARE); comparisons++;
    return memcmp(left, right, bytes);
}
static void prepare_memcpy(void *dest, const void *source, size_t bytes)
{
    assert(owned == PREPARE);
    assert(dest != device.cursor_pixels && dest != device.cursor_source_pixels);
    preparations++; preparation_hook(); memcpy(dest, source, bytes);
}
static int ms912x_cursor_copy_framebuffer(struct drm_plane_state *new_state, u32 *pixels)
{
    assert(owned == PREPARE); copies++;
    if (copy_error) return copy_error;
    assert(new_state->fb->width <= 128 && new_state->fb->height <= 128);
    for (unsigned int y = 0; y < new_state->fb->height; y++)
        memcpy(pixels + y * 128, new_state->fb->pixels + y * 128,
               new_state->fb->width * sizeof(u32));
    return 0;
}
static void ms912x_downscale_xrgb8888(enum ms912x_scale_mode mode,
    const u32 *source, u32 *dest, unsigned int sw, unsigned int sh,
    unsigned int dw, unsigned int dh, unsigned int ss, unsigned int ds)
{
    /* Deterministic filter mock; production filter arithmetic is tested separately. */
    assert(owned == PREPARE && mode != MS912X_SCALE_MODE_NATIVE);
    assert(source == dest && ss == 128 && ds == 128); downscales++;
    for (unsigned int y = 0; y < dh; y++)
        for (unsigned int x = 0; x < dw; x++)
            dest[y * ds + x] = source[(y * sh / dh) * ss + x * sw / dw];
}
static void ms912x_payload_invalidate_damage(struct ms912x_device *d,
    const struct drm_rect *rects, unsigned int count)
{ assert(owned == (PREPARE | TRANSFER)); invalidations += count; }
static void ms912x_payload_invalidate_all(struct ms912x_device *d)
{ assert(owned == (PREPARE | TRANSFER)); invalidations++; }
static void ms912x_pending_add_rect_locked(struct ms912x_device *d, const struct drm_rect *rect)
{
    assert(owned & TRANSFER); assert(d->pending_rect_count < ARRAY_SIZE(d->pending_rects));
    d->pending_rects[d->pending_rect_count++] = *rect;
}
static void ms912x_pending_compact_locked(struct ms912x_device *d) { assert(owned & TRANSFER); }
static void ms912x_pending_update_deadline_locked(struct ms912x_device *d, bool had_pending)
{ assert(owned & TRANSFER); if (!had_pending) d->pending_deadline = 1; }
static void ms912x_schedule_conversion_locked(struct ms912x_device *d)
{ assert(owned & TRANSFER); schedules++; }
static void wake_up_all(int *wait) { (void)wait; }
#define memcpy prepare_memcpy
#include "cursor-prepare.h"
#undef memcpy
#include "cursor-entry.h"
#include "cursor-stop.h"

static void *concurrent_update(void *argument)
{
    unsigned int action = concurrent_action;
    if (action == 8) {
        stopping_task = true;
        ms912x_transfer_stop(&device);
        stopping_task = false;
        assert(copies == 2);
        worker_done = true;
        return NULL;
    }
    assert(pthread_mutex_trylock(&device.cursor_prepare_lock.native) == EBUSY);
    mutex_lock(&device.transfer_lock);
    if (action == 5) {
        struct drm_rect full = DRM_RECT_INIT(0, 0, 640, 480);
        ms912x_cursor_snapshot_request_locked(&device, &snapshot, &full, 1);
        assert(snapshot.cursor_x == previous_x && snapshot.cursor_y == previous_y);
        assert(!memcmp(snapshot.cursor_pixels, previous_pixels,
                       snapshot.cursor_source_height * 128 * sizeof(u32)));
    }
    if (action == 7) {
        device.scale_mode = MS912X_SCALE_MODE_DOWNSCALE_5_TO_4;
        device.frame_generation++;
    }
    mutex_unlock(&device.transfer_lock);
    signal_worker_waiting();
    if (action == 6) ms912x_cursor_plane_submit(&device, NULL);
    if (action == 7) ms912x_cursor_reconfigure(&device);
    worker_done = true;
    return NULL;
}

static void assert_buffer_roles(void)
{
    u32 *roles[] = {device.cursor_pixels, device.cursor_source_pixels,
                   device.cursor_staging_pixels, device.cursor_prepare_pixels};
    for (unsigned int i = 0; i < ARRAY_SIZE(roles); i++) {
        assert(roles[i] >= device.cursor_buffer_storage && roles[i] < device.cursor_buffer_storage + 4 * PIXELS);
        assert((roles[i] - device.cursor_buffer_storage) % PIXELS == 0);
        for (unsigned int j = i + 1; j < ARRAY_SIZE(roles); j++) assert(roles[i] != roles[j]);
    }
}
static void reset(void)
{
    static struct { int dev; } interface;
    memset(&device, 0, sizeof(device));
    test_drm_dev_enter_allowed = true;
    test_drm_dev_enter_calls = test_drm_dev_exit_calls = 0;
    device.intf = (void *)&interface;
    device.cursor_plane_enabled = true;
    device.frame_snapshot_width = 640; device.frame_snapshot_height = 480;
    device.frame_generation = 7; device.published_frame_sequence = 10;
    device.cursor_prepare_lock.bit = PREPARE; device.transfer_lock.bit = TRANSFER;
    device.scan_queue_lock.bit = SCAN; device.submit_lock.bit = SUBMIT;
    assert(!pthread_mutex_init(&device.cursor_prepare_lock.native, NULL));
    assert(!pthread_mutex_init(&device.transfer_lock.native, NULL));
    assert(!pthread_mutex_init(&device.scan_queue_lock.native, NULL));
    assert(!pthread_mutex_init(&device.submit_lock.native, NULL));
    allocation_failure = action_failure = false; allocations = frees = 0;
    assert(!ms912x_init_cursor_buffers(&device));
    framebuffer = (struct drm_framebuffer){.base.id = 1, .width = 16, .height = 16, .pixels = raw};
    state = (struct drm_plane_state){.fb = &framebuffer, .crtc = (void *)1,
                                   .visible = true, .crtc_x = 10, .crtc_y = 20};
    memset(raw, 0, sizeof(raw));
    for (unsigned int y = 3; y < 14; y++)
        for (unsigned int x = 2; x < 10; x++)
            raw[y * 128 + x] = ((x + y) & 1 ? 0x80000000U : 0xff000000U) | (y << 12) | (x << 4);
    copies = comparisons = preparations = downscales = schedules = invalidations = 0;
    hook_action = concurrent_action = 0; copy_error = 0;
    memset(&snapshot, 0, sizeof(snapshot));
    assert(!owned); assert_buffer_roles(); cases++;
}
static void cleanup(void)
{
    assert(!owned); assert_buffer_roles();
    release_action(&device.drm, release_data);
    assert(allocations == 1 && frees == 1);
    assert(!pthread_mutex_destroy(&device.cursor_prepare_lock.native));
    assert(!pthread_mutex_destroy(&device.transfer_lock.native));
    assert(!pthread_mutex_destroy(&device.scan_queue_lock.native));
    assert(!pthread_mutex_destroy(&device.submit_lock.native));
}
static int reference_coordinate(int value, enum ms912x_scale_mode mode)
{
    unsigned int n = 1, d = 1;
    if (mode == MS912X_SCALE_MODE_DOWNSCALE_2_TO_1) { n = 1; d = 2; }
    if (mode == MS912X_SCALE_MODE_DOWNSCALE_3_TO_2) { n = 2; d = 3; }
    if (mode == MS912X_SCALE_MODE_DOWNSCALE_5_TO_4) { n = 4; d = 5; }
    int product = value * (int)n;
    return product < 0 ? -((-product + (int)d - 1) / (int)d) : product / (int)d;
}
static void verify_current(void)
{
    u32 expected[PIXELS] = {};
    unsigned int w = framebuffer.width, h = framebuffer.height;
    if (device.scale_mode != MS912X_SCALE_MODE_NATIVE) {
        w = ms912x_scale_dimension(w, device.scale_mode);
        h = ms912x_scale_dimension(h, device.scale_mode);
    }
    unsigned int min_x = w, min_y = h, max_x = 0, max_y = 0;
    for (unsigned int y = 0; y < h; y++)
        for (unsigned int x = 0; x < w; x++) {
            u32 pixel = raw[(y * framebuffer.height / h) * 128 + x * framebuffer.width / w];
            expected[y * 128 + x] = pixel;
            assert(device.cursor_pixels[y * 128 + x] == pixel);
            if (pixel >> 24) { min_x = min(min_x, x); min_y = min(min_y, y); max_x = max(max_x, x); max_y = max(max_y, y); }
        }
    bool visible = min_x < w;
    unsigned int cw = visible ? max_x - min_x + 1 : 0, ch = visible ? max_y - min_y + 1 : 0;
    if (!visible) min_x = min_y = 0;
    int px = state.crtc_x - (int)(state.src_x >> 16), py = state.crtc_y - (int)(state.src_y >> 16);
    int x = reference_coordinate(px, device.scale_mode) + min_x;
    int y = reference_coordinate(py, device.scale_mode) + min_y;
    assert(device.cursor_enabled == visible && device.cursor_plane_active);
    assert(device.cursor_width == cw && device.cursor_height == ch && device.cursor_source_height == h);
    assert(device.cursor_alpha_x == min_x && device.cursor_alpha_y == min_y);
    assert(device.cursor_x == x && device.cursor_y == y && device.cursor_scale_mode == device.scale_mode);
    for (unsigned int row = 0; row < framebuffer.height; row++)
        assert(!memcmp(device.cursor_source_pixels + row * 128, raw + row * 128, framebuffer.width * sizeof(u32)));
    unsigned char actual_packet[8 + 64 * 64 * 2], expected_packet[sizeof(actual_packet)];
    memset(actual_packet, 128, sizeof(actual_packet)); memcpy(expected_packet, actual_packet, sizeof(actual_packet));
    struct drm_rect rect = DRM_RECT_INIT(0, 0, 64, 64);
    ms912x_cursor_composite_uyvy(actual_packet, &rect, device.cursor_pixels,
        device.cursor_x, device.cursor_y, device.cursor_width, device.cursor_height,
        device.cursor_alpha_x, device.cursor_alpha_y, device.cursor_premultiplied);
    ms912x_cursor_composite_uyvy(expected_packet, &rect, expected, x, y, cw, ch,
        min_x, min_y, state.pixel_blend_mode != DRM_MODE_BLEND_COVERAGE);
    assert(!memcmp(actual_packet, expected_packet, sizeof(actual_packet)));
    assert_buffer_roles();
}

static void normal_cases(void)
{
    for (unsigned int mode = 0; mode <= MS912X_SCALE_MODE_DOWNSCALE_5_TO_4; mode++) {
        reset(); device.scale_mode = mode;
        ms912x_cursor_plane_submit(&device, &state); verify_current();
        assert(preparations == 1 && copies == 1);
        u32 *rendered = device.cursor_pixels, *cached = device.cursor_source_pixels;
        state.crtc_x = -3; state.src_x = 1U << 16;
        device.pending_rect_count = 0;
        ms912x_cursor_plane_submit(&device, &state); verify_current();
        assert(device.cursor_pixels == rendered && device.cursor_source_pixels == cached);
        assert(preparations == 1 && comparisons && copies == 2);
        raw[3 * 128 + 2] ^= 0x00000100;
        state.pixel_blend_mode = DRM_MODE_BLEND_COVERAGE;
        device.pending_rect_count = 0;
        ms912x_cursor_plane_submit(&device, &state); verify_current();
        assert(device.cursor_pixels != rendered && device.cursor_source_pixels != cached && preparations == 2);
        for (unsigned int update = 0; update < 20; update++) {
            device.pending_rect_count = 0; raw[3 * 128 + 2]++;
            ms912x_cursor_plane_submit(&device, &state); verify_current();
        }
        cleanup();
    }
    reset(); memset(raw, 0, sizeof(raw));
    ms912x_cursor_plane_submit(&device, &state); verify_current(); assert(!device.cursor_enabled);
    device.pending_rect_count = 0; memset(raw, 0xff, sizeof(raw));
    framebuffer.width = framebuffer.height = 128;
    ms912x_cursor_plane_submit(&device, &state); verify_current();
    device.pending_rect_count = 0; state.visible = false;
    ms912x_cursor_plane_submit(&device, &state);
    assert(!device.cursor_enabled && !device.cursor_plane_active && !device.cursor_width && !device.cursor_source_height);
    cleanup();

    reset(); state.crtc_x = 700; ms912x_cursor_plane_submit(&device, &state);
    assert(device.cursor_enabled && !device.pending_rect_count); verify_current(); cleanup();
}

static void interruption_cases(void)
{
    for (unsigned int action = 1; action <= 4; action++) {
        reset(); ms912x_cursor_plane_submit(&device, &state);
        u32 *rendered = device.cursor_pixels, *cached = device.cursor_source_pixels;
        u32 source_before[PIXELS]; memcpy(source_before, cached, sizeof(source_before));
        raw[4 * 128 + 3] ^= 0x00010000; device.pending_rect_count = 0;
        device.cursor_commit_pending = true; device.cursor_commit_sequence = 11;
        hook_action = action;
        ms912x_cursor_plane_submit(&device, &state);
        if (action == 1) {
            assert(device.cursor_pixels == rendered && device.cursor_source_pixels == cached);
            assert(!memcmp(cached, source_before, sizeof(source_before)) && !device.pending_rect_count);
            assert(!device.cursor_commit_pending && !device.cursor_commit_sequence);
        } else {
            verify_current();
            if (action == 2 || action == 3) assert(preparations == 3);
            if (action == 4) assert(device.pending_rect_count == 3 && device.pending_deadline == 99);
        }
        cleanup();
    }
    reset(); ms912x_cursor_plane_submit(&device, &state);
    u32 *rendered = device.cursor_pixels, *cached = device.cursor_source_pixels;
    copy_error = -EIO; device.cursor_commit_pending = true;
    ms912x_cursor_plane_submit(&device, &state);
    assert(device.cursor_pixels == rendered && device.cursor_source_pixels == cached && !device.cursor_commit_pending);
    cleanup();

    reset(); device.frame_snapshot_width = device.frame_snapshot_height = 0;
    device.cursor_commit_pending = true; device.cursor_commit_sequence = 11;
    ms912x_cursor_plane_submit(&device, &state); verify_current();
    assert(device.cursor_commit_pending && !device.pending_rect_count);
    device.published_frame_sequence = 11;
    ms912x_cursor_plane_submit(&device, &state); assert(!device.cursor_commit_pending); cleanup();
}

static void reconfigure_and_races(void)
{
    for (unsigned int action = 0; action <= 3; action++) {
        reset(); ms912x_cursor_plane_submit(&device, &state);
        device.scale_mode = MS912X_SCALE_MODE_DOWNSCALE_3_TO_2;
        device.transfer_stopping = true; /* Modeset reconfigures while transfers are stopped. */
        hook_action = action == 1 ? 2 : action == 2 ? 3 : 0;
        u32 *cached = device.cursor_source_pixels;
        ms912x_cursor_reconfigure(&device); verify_current();
        assert(device.cursor_source_pixels == cached && invalidations >= 2);
        if (action == 3) {
            device.cursor_plane_active = false;
            unsigned int before = preparations;
            ms912x_cursor_reconfigure(&device); assert(preparations == before);
        }
        cleanup();
    }
    for (unsigned int action = 5; action <= 8; action++)
        for (unsigned int repeat = 0; repeat < 10; repeat++) {
            reset(); ms912x_cursor_plane_submit(&device, &state);
            u32 *rendered = device.cursor_pixels, *cached = device.cursor_source_pixels;
            raw[4 * 128 + 3] ^= 0x00010000; device.pending_rect_count = 0;
            hook_action = action;
            ms912x_cursor_plane_submit(&device, &state);
            assert(!pthread_join(worker, NULL) && worker_done);
            if (action == 6) assert(!device.cursor_plane_active && !device.cursor_enabled);
            else if (action == 8) assert(device.cursor_pixels == rendered && device.cursor_source_pixels == cached);
            else verify_current();
            cleanup();
        }
}

static void unplugged_cursor_paths_do_not_touch_buffers(void)
{
    reset();
    test_drm_dev_enter_allowed = false;
    ms912x_cursor_plane_submit(&device, &state);
    ms912x_cursor_reconfigure(&device);
    assert(test_drm_dev_enter_calls == 2 && !test_drm_dev_exit_calls);
    assert(!copies && !preparations && !device.cursor_plane_active);
    cleanup();
}

int main(void)
{
    memset(&device, 0, sizeof(device)); allocation_failure = true;
    assert(ms912x_init_cursor_buffers(&device) == -ENOMEM && !device.cursor_buffer_storage);
    allocation_failure = false; action_failure = true;
    assert(ms912x_init_cursor_buffers(&device) == -EIO && frees == 1);
    normal_cases(); interruption_cases(); reconfigure_and_races();
    unplugged_cursor_paths_do_not_touch_buffers();
    printf("PASS cursor entry: %u scenarios; private preparation, byte-exact composition, "
           "buffer rotation, epochs, stop, disable/reconfigure races and allocation cleanup\n", cases);
    return 0;
}
