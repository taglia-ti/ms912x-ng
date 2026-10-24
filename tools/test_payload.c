// SPDX-License-Identifier: GPL-2.0-only
/* Production invalidation, serialization and complete worker with simulated USB I/O. */
#include <assert.h>
#include <stdio.h>
#include <pthread.h>
#include "ms912x_batch.h"
#include "ms912x_payload.h"
#include "ms912x_mode3.h"
#include "ms912x_cursor.h"
#include "shared/ms912x_draw_core.h"
#include "shared/ms912x_pacing_core.h"

#define WIDTH 64
#define HEIGHT 32
#define UYVY_WORD_STRIDE (MS912X_MAX_WIDTH / 2)
#define BYTES (UYVY_WORD_STRIDE * HEIGHT * sizeof(u32))
#define container_of(p, t, field) ((t *)((char *)(p) - offsetof(t, field)))
#define interface_to_usbdev(p) ((struct usb_device *)NULL)
#define ALIGN(x, a) (((x) + (a) - 1) & ~((a) - 1))
#define ALIGN_DOWN(x, a) ((x) & ~((a) - 1))
#define to_delayed_work(p) container_of(p, struct delayed_work, work)
#define MS912X_STAT_INC(d, n) do { if (statistics_enabled) (d)->stats.n++; } while (0)
#define MS912X_STAT_ADD(d, n, v) do { if (statistics_enabled) (d)->stats.n += (v); } while (0)
#define drm_err_ratelimited(d, ...) ((void)(d))
#define wait_event(w, c) do { assert(!device.transfer_lock && !device.snapshot_lock); device.conversion_busy = false; } while (0)
static struct ms912x_device device;
static struct ms912x_device unplugged_device;
static struct drm_rect rects[32];
static struct drm_rect current_rects[32], pending_rects[32], previous_rects[32];
static struct drm_rect grown_rects[64];
static struct drm_rect compare_rects[MS912X_MAX_BATCH_RECTS];
static unsigned int conversions, usb_submissions, action, invalidations, requeues;
static int usb_result;
static bool usb_short;
static u64 usb_duration;
static unsigned int planning_action, planning_calls;
static unsigned int vector_grows;
static unsigned int composite_calls;
static size_t composite_offsets[32];
static struct { bool dbg_show_draws; } ms912x_parameters;
static bool stage_failure;
static bool queue_failure, growth_failure, statistics_enabled;
static unsigned int queued_requests;
static u64 encoded_generation;
static unsigned int cases;
static pthread_mutex_t transfer_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t snapshot_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t unplugged_transfer_mutex = PTHREAD_MUTEX_INITIALIZER;
static _Thread_local bool transfer_owned;
static _Thread_local bool snapshot_owned;
static unsigned int staging_action, staging_copies;
static pthread_t publisher;
static pthread_mutex_t probe_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t probe_cond = PTHREAD_COND_INITIALIZER;
static bool publisher_waiting;
static u32 published_pixels[MS912X_MAX_WIDTH * HEIGHT];
static unsigned int comparison_action, comparison_calls;
static size_t comparison_bytes;
static pthread_mutex_t *native_mutex(_Atomic int *m)
{
    if (m == &device.transfer_lock) return &transfer_mutex;
    if (m == &device.snapshot_lock) return &snapshot_mutex;
    assert(m == &unplugged_device.transfer_lock);
    return &unplugged_transfer_mutex;
}
bool ms912x_test_lock_owned(const _Atomic int *m)
{
    if (m == &device.transfer_lock) return transfer_owned;
    if (m == &device.snapshot_lock) return snapshot_owned;
    assert(m == &device.damage_scan_lock);
    return false;
}
void mutex_lock(_Atomic int *m)
{
    if (m == &device.snapshot_lock || m == &unplugged_device.transfer_lock)
        assert(!transfer_owned);
    assert(!pthread_mutex_lock(native_mutex(m)));
    assert(!*m); *m = 1;
    if (m == &device.transfer_lock || m == &unplugged_device.transfer_lock)
        transfer_owned = true;
    else
        snapshot_owned = true;
}
void mutex_unlock(_Atomic int *m)
{
    assert(*m); *m = 0;
    if (m == &device.transfer_lock || m == &unplugged_device.transfer_lock)
        transfer_owned = false;
    else
        snapshot_owned = false;
    assert(!pthread_mutex_unlock(native_mutex(m)));
}
static void wake_up_all(int *w) { (void)w; }
static bool ms912x_stats_enabled(void) { return statistics_enabled; }
static u64 ktime_get_ns(void) { return 1; }
static bool drm_dev_enter(int *d, int *idx) { *idx = 0; return !*d; }
static void drm_dev_exit(int idx) { (void)idx; }
static void ms912x_schedule_conversion_locked(struct ms912x_device *d) { assert(d->transfer_lock); }
static void ms912x_transfer_update_pacing_locked(struct ms912x_device *d) { assert(d->transfer_lock); }
static void ms912x_damage_scan_invalidate(struct ms912x_device *d) { invalidations++; }
static void ms912x_transfer_requeue_rect_locked(struct ms912x_device *d,
    const struct drm_rect *r, unsigned int w, unsigned int h) { requeues++; d->pending_valid = true; }
static void ms912x_usb_requeue_request_rects_locked(struct ms912x_device *, const struct ms912x_usb_request *);
static bool ms912x_usb_request_covers_rect(const struct ms912x_usb_request *, const struct drm_rect *);
struct drm_rect *ms912x_rect_vector_grow(struct drm_rect *p, unsigned int *capacity,
    unsigned int required, unsigned int limit)
{
    assert(!device.transfer_lock);
    if (required <= *capacity) return p;
    vector_grows++;
    if (growth_failure) return NULL;
    assert(required <= ARRAY_SIZE(grown_rects));
    memcpy(grown_rects, p, *capacity * sizeof(*p));
    *capacity = ARRAY_SIZE(grown_rects);
    return grown_rects;
}
void ms912x_test_sort_hook(void)
{
    struct ms912x_usb_request *r = NULL;
    unsigned int inject = planning_action;

    assert(!device.transfer_lock && device.conversion_busy);
    for (unsigned int i = 0; i < ARRAY_SIZE(device.requests); i++)
        if (device.requests[i].state == MS912X_REQUEST_PREPARING)
            r = &device.requests[i];
    assert(r);
    planning_calls++;
    planning_action = 0;
    if (!inject) return;
    mutex_lock(&device.transfer_lock);
    if (inject == 1 || inject == 3) {
        struct drm_rect update = DRM_RECT_INIT(0, 0, 4, 4);
        u64 generation = r->content_generation;

        ms912x_payload_invalidate_damage(&device, &update, 1);
        assert(r->content_generation == generation); /* Private coverage is not watched. */
        device.latest_pixels[0] = 0x123456;
        device.published_frame_sequence++;
        device.pending_rects[0] = update;
        device.pending_rect_count = 1;
        device.pending_valid = true;
        device.pending_deadline = 99;
    }
    if (inject == 2) device.transfer_stopping = true;
    if (inject == 3) {
        device.frame_generation++;
        device.previous_damage_rect_count = 0;
    }
    if (inject == 4) {
        device.failed_rect_valid = true;
        device.failed_generation = device.frame_generation;
        device.failed_width = WIDTH; device.failed_height = HEIGHT;
    }
    if (inject == 5) {
        device.cursor_enabled = true; device.cursor_x = 4; device.cursor_y = 4;
        device.cursor_width = device.cursor_height = device.cursor_source_height = 8;
        device.cursor_pixels[0] = 0x24681357;
    }
    mutex_unlock(&device.transfer_lock);
}
static bool queue_work(void *queue, struct work_struct *work)
{
    struct ms912x_usb_request *r = container_of(work, struct ms912x_usb_request, work);
    assert(device.transfer_lock && !device.conversion_busy);
    assert(r->state == MS912X_REQUEST_USB_QUEUED);
    if (queue_failure) return false;
    queued_requests++;
    return true;
}
static void ms912x_pending_add_rect_locked(struct ms912x_device *d, const struct drm_rect *r)
{
    assert(d->transfer_lock);
    /* Worker recovery always adds one complete frame. */
    assert(r->x1 == 0 && r->y1 == 0 && r->x2 == (int)d->pending_width &&
           r->y2 == (int)d->pending_height);
    d->pending_rects[0] = *r;
    d->pending_rect_count = 1;
    d->pending_valid = d->pending_force_full = true;
    d->pending_deadline = 0;
}
static bool ms912x_cursor_bounds(int x, int y, unsigned int cw, unsigned int ch,
    unsigned int w, unsigned int h, struct drm_rect *r)
{
    *r = (struct drm_rect){max(0, x) & ~3, max(0, y),
                         min((x + (int)cw + 3) & ~3, (int)w), min(y + (int)ch, (int)h)};
    return r->x1 < r->x2 && r->y1 < r->y2;
}
static size_t ms912x_rect_encoded_bytes(const struct drm_rect *r)
{ return drm_rect_width(r) * drm_rect_height(r) * 2 + 16; }
static void *publish_during_staging(void *argument)
{
    unsigned int kind = *(unsigned int *)argument;
    struct drm_rect full = DRM_RECT_INIT(0, 0, WIDTH, HEIGHT);
    struct ms912x_payload_check checks[ARRAY_SIZE(device.requests)];

    /* USB bookkeeping must progress while the pixel copy retains its source. */
    mutex_lock(&device.transfer_lock);
    assert(device.conversion_busy);
    mutex_unlock(&device.transfer_lock);
    assert(pthread_mutex_trylock(&snapshot_mutex) == EBUSY);
    assert(!pthread_mutex_lock(&probe_mutex));
    publisher_waiting = true;
    assert(!pthread_cond_signal(&probe_cond));
    assert(!pthread_mutex_unlock(&probe_mutex));

    mutex_lock(&device.snapshot_lock);
    if (kind == 5) {
        for (unsigned int y = 0; y < HEIGHT; y++)
            for (unsigned int x = 0; x < WIDTH / 2; x++)
                published_pixels[y * UYVY_WORD_STRIDE + x] = 0x654321;
        ms912x_payload_compare_frame(&device, published_pixels, WIDTH, HEIGHT, checks);
    }
    mutex_lock(&device.transfer_lock);
    if (kind == 4) {
        ms912x_payload_invalidate_damage(&device, &full, 1);
        for (unsigned int y = 0; y < HEIGHT; y++)
            for (unsigned int x = 0; x < WIDTH / 2; x++)
                device.latest_pixels[y * UYVY_WORD_STRIDE + x] = 0x654321;
    } else {
        u32 *retired = device.latest_pixels;

        ms912x_payload_invalidate_compared(&device, checks);
        device.latest_pixels = published_pixels;
        /* The old image can now be reused as a scan capture slot. */
        memset(retired, 0xa5, BYTES);
    }
    device.published_frame_sequence++;
    device.pending_rects[0] = full;
    device.pending_rect_count = 1;
    device.pending_valid = true;
    mutex_unlock(&device.transfer_lock);
    mutex_unlock(&device.snapshot_lock);
    return NULL;
}
static void staging_memcpy(void *destination, const void *source, size_t bytes)
{
    assert(!device.transfer_lock && device.snapshot_lock && device.conversion_busy);
    if (!staging_copies++) {
        if (staging_action >= 4) {
            assert(!pthread_create(&publisher, NULL, publish_during_staging, &staging_action));
            assert(!pthread_mutex_lock(&probe_mutex));
            while (!publisher_waiting)
                assert(!pthread_cond_wait(&probe_cond, &probe_mutex));
            assert(!pthread_mutex_unlock(&probe_mutex));
        } else if (staging_action) {
            mutex_lock(&device.transfer_lock);
            if (staging_action == 1) device.transfer_stopping = true;
            if (staging_action == 2) {
                device.frame_generation++;
                device.pending_rects[0] = DRM_RECT_INIT(0, 0, 4, 4);
                device.pending_rect_count = 1;
                device.pending_valid = true;
            }
            if (staging_action == 3) {
                /* Cursor changes can invalidate coverage during the copy. */
                ms912x_payload_invalidate_all(&device);
                device.pending_valid = true;
            }
            mutex_unlock(&device.transfer_lock);
        }
    }
    memcpy(destination, source, bytes);
}
#define memcpy staging_memcpy
#include "ms912x_pixels.h"
#define ms912x_snapshot_stage_conversion ms912x_snapshot_stage_conversion_real
#include "stage.h"
#undef ms912x_snapshot_stage_conversion
#undef memcpy

static bool ms912x_snapshot_stage_conversion(struct ms912x_device *d,
    const struct drm_rect *rs, unsigned int n, unsigned int w, unsigned int h)
{
    assert(!d->transfer_lock && d->snapshot_lock && d->conversion_busy);
    if (stage_failure) return false;
    encoded_generation = d->requests[0].content_generation;
    return ms912x_snapshot_stage_conversion_real(d, rs, n, w, h);
}
/* Observe calls and inject races around unchanged production pixel/packet code. */
#define ms912x_pack_uyvy_batch ms912x_pack_uyvy_batch_real
#define ms912x_cursor_composite_uyvy ms912x_cursor_composite_uyvy_real
#include "packet-production.h"
#undef ms912x_cursor_composite_uyvy
#undef ms912x_pack_uyvy_batch

static int ms912x_pack_uyvy_batch(void *out, const u32 *in,
    unsigned int w, unsigned int h,
    const struct drm_rect *rs, unsigned int n)
{
    struct ms912x_device *d = &device;
    assert(!transfer_owned && !snapshot_owned && d->conversion_busy);
    conversions++;
    assert(conversions < 10); /* Detect refresh starvation immediately. */
    int ret = ms912x_pack_uyvy_batch_real(out, in, w, h, rs, n);
    if (ret) return ret;
    mutex_lock(&d->transfer_lock);
    if (action == 1) {
        ms912x_payload_invalidate_damage(d, &rs[0], 1);
        d->latest_pixels[rs[0].y1 * UYVY_WORD_STRIDE + rs[0].x1 / 2]++;
        d->published_frame_sequence++;
        d->pending_valid = true;
    }
    if (action == 2) d->transfer_stopping = true;
    if (action == 3) d->frame_generation++;
    mutex_unlock(&d->transfer_lock);
    return action == 4 ? -EIO : 0;
}
static u64 ms912x_cursor_composite_uyvy(void *out, const struct drm_rect *rect,
    const u32 *pixels, int x, int y, unsigned int w, unsigned int h,
    unsigned int ax, unsigned int ay, bool premultiplied)
{
    assert(composite_calls < ARRAY_SIZE(composite_offsets));
    composite_offsets[composite_calls++] = (u8 *)out - (u8 *)device.requests[0].transfer_buffer;
    return ms912x_cursor_composite_uyvy_real(out, rect, pixels, x, y, w, h, ax, ay, premultiplied);
}
/* The USB boundary alone is simulated; the complete worker and completion run. */
static int ms912x_usb_request_execute(struct ms912x_usb_request *request,
    struct usb_device *usbdev, bool stats, u64 start, u64 *duration, bool *short_transfer)
{
    assert(!transfer_owned && !snapshot_owned);
    assert(request->state == MS912X_REQUEST_USB_ACTIVE);
    usb_submissions++;
    *duration = usb_duration;
    *short_transfer = usb_short;
    return usb_result;
}
#include "refresh.h"

static u32 packet_pair(const struct ms912x_usb_request *request, unsigned int x, unsigned int y)
{
    size_t offset = 0;
    for (unsigned int i = 0; i < request->rect_count; ++i) {
        const struct drm_rect *r = &request->rects[i];
        unsigned int width = drm_rect_width(r);
        if ((int)x >= r->x1 && (int)x < r->x2 && (int)y >= r->y1 && (int)y < r->y2) {
            u32 pair;
            memcpy(&pair, (const u8 *)request->transfer_buffer + offset + 8 +
                ((y - r->y1) * width + x - r->x1) * 2, sizeof(pair));
            return pair;
        }
        offset += ms912x_rect_encoded_bytes(r);
    }
    assert(!"Pixel is outside the encoded batch");
    return 0;
}

static void *comparison_bookkeeping(void *argument)
{
    unsigned int inject = *(unsigned int *)argument;
    struct ms912x_usb_request *r = &device.requests[0];

    assert(pthread_mutex_trylock(&snapshot_mutex) == EBUSY);
    mutex_lock(&device.transfer_lock);
    if (inject == 1) ms912x_payload_invalidate_all(&device);
    if (inject == 2 || inject == 3) ms912x_usb_request_release_locked(r);
    if (inject == 3) {
        /* Model private geometry reallocation after this slot is reused. */
        free(r->rects);
        r->rects = NULL; r->rect_count = ~0U;
        r->state = MS912X_REQUEST_PREPARING;
    }
    if (inject == 4) r->state = MS912X_REQUEST_USB_QUEUED;
    if (inject == 5) r->state = MS912X_REQUEST_USB_ACTIVE;
    if (inject == 6) device.frame_generation++;
    if (inject == 7) {
        r->source_generation++;
        r->content_generation = r->source_generation;
    }
    if (inject == 8) r->generation = ++device.frame_generation;
    if (inject == 9) device.transfer_stopping = true;
    mutex_unlock(&device.transfer_lock);
    return NULL;
}

int ms912x_test_payload_memcmp(const void *old, const void *frame, size_t bytes)
{
    assert(snapshot_owned && !transfer_owned && !device.damage_scan_lock);
    comparison_calls++;
    comparison_bytes += bytes;
    if (comparison_action) {
        unsigned int inject = comparison_action;
        pthread_t bookkeeping;

        comparison_action = 0;
        assert(!pthread_create(&bookkeeping, NULL, comparison_bookkeeping, &inject));
        assert(!pthread_join(bookkeeping, NULL));
    }
    return memcmp(old, frame, bytes);
}

static void invalidate_frame(struct ms912x_device *d, const u32 *frame,
                             unsigned int width, unsigned int height)
{
    struct ms912x_payload_check checks[ARRAY_SIZE(d->requests)];

    mutex_lock(&d->snapshot_lock);
    ms912x_payload_compare_frame(d, frame, width, height, checks);
    mutex_lock(&d->transfer_lock);
    ms912x_payload_invalidate_compared(d, checks);
    mutex_unlock(&d->transfer_lock);
    mutex_unlock(&d->snapshot_lock);
}

static struct ms912x_usb_request *setup(void)
{
    static u32 latest[MS912X_MAX_WIDTH * HEIGHT], scratch[MS912X_MAX_WIDTH * HEIGHT];
    static u32 output[(MS912X_MAX_TRANSFER_LEN + 3) / sizeof(u32)];
    static u32 cursor_pixels[MS912X_CURSOR_MAX_WIDTH * MS912X_CURSOR_MAX_HEIGHT];
    memset(&device, 0, sizeof(device)); memset(latest, 0, sizeof(latest));
    device.latest_pixels = latest; device.conversion_pixels = scratch;
    device.cursor_pixels = cursor_pixels;
    memset(cursor_pixels, 0, sizeof(cursor_pixels));
    device.frame_generation = 7;
    device.published_frame_sequence = 10;
    device.pending_rects = pending_rects;
    device.pending_rect_capacity = ARRAY_SIZE(pending_rects);
    device.previous_damage_rects = previous_rects;
    device.previous_damage_rect_capacity = ARRAY_SIZE(previous_rects);
    device.payload_compare_rects = compare_rects;
    struct ms912x_usb_request *r = &device.requests[0];
    *r = (struct ms912x_usb_request){.ms912x = &device, .rects = rects, .rect_count = 1,
        .rect_capacity = ARRAY_SIZE(rects), .width = WIDTH, .height = HEIGHT,
        .current_rects = current_rects, .current_rect_capacity = ARRAY_SIZE(current_rects),
        .generation = 7, .state = MS912X_REQUEST_USB_QUEUED, .transfer_buffer = output};
    rects[0] = DRM_RECT_INIT(0, 0, 16, 8);
    conversions = usb_submissions = action = invalidations = requeues = 0;
    usb_result = 0; usb_short = false; usb_duration = 0;
    stage_failure = false; composite_calls = 0;
    queue_failure = growth_failure = statistics_enabled = false;
    queued_requests = 0;
    planning_action = planning_calls = 0;
    vector_grows = 0;
    staging_action = staging_copies = 0;
    comparison_action = comparison_calls = 0;
    comparison_bytes = 0;
    publisher_waiting = false;
    assert(!transfer_owned && !snapshot_owned);
    cases++;
    return r;
}
static void invalidate_cases(void)
{
    struct ms912x_usb_request *r = setup();
    struct drm_rect outside = DRM_RECT_INIT(32, 16, 16, 8);
    for (unsigned int i = 0; i < 1000; i++) {
        ms912x_payload_invalidate_damage(&device, &outside, 1);
        assert(!r->content_generation);
    }
    struct drm_rect edge = DRM_RECT_INIT(16, 0, 8, 8);
    ms912x_payload_invalidate_damage(&device, &edge, 1);
    assert(!r->content_generation); /* Touching edges do not overlap. */
    edge.x1--;
    ms912x_payload_invalidate_damage(&device, &edge, 1);
    assert(r->content_generation == 1);
    r->source_generation = r->content_generation;
    /* A replay/expanded rectangle is covered even outside current damage. */
    rects[r->rect_count++] = outside;
    ms912x_payload_invalidate_damage(&device, &outside, 1);
    assert(r->content_generation == 2);
    for (unsigned int state = 0; state <= MS912X_REQUEST_USB_ACTIVE; state++) {
        r->state = state; r->content_generation = r->source_generation = 0;
        ms912x_payload_invalidate_all(&device);
        assert(r->content_generation == (state == MS912X_REQUEST_CONVERTING ||
                                         state == MS912X_REQUEST_USB_QUEUED));
    }
    r->state = MS912X_REQUEST_USB_QUEUED;
    r->generation--;
    u64 generation = r->content_generation;
    ms912x_payload_invalidate_all(&device);
    ms912x_payload_invalidate_damage(&device, &rects[0], 1);
    assert(r->content_generation == generation);

    r = setup();
    u32 *frame = calloc(1, BYTES); assert(frame);
    frame[1] = 0xff000000; /* Y1 participates in UYVY comparisons. */
    invalidate_frame(&device, frame, WIDTH, HEIGHT);
    assert(r->content_generation == 1);
    r = setup();
    frame[1] = 0;
    frame[20 * UYVY_WORD_STRIDE + 20] = 1;
    invalidate_frame(&device, frame, WIDTH, HEIGHT);
    assert(!r->content_generation);
    frame[7 * UYVY_WORD_STRIDE + 7] = 1;
    invalidate_frame(&device, frame, WIDTH, HEIGHT);
    assert(r->content_generation == 1);
    invalidate_frame(&device, frame, WIDTH, HEIGHT);
    assert(r->content_generation == 1); /* Dirty payloads need no repeated comparison. */
    memcpy(device.latest_pixels, frame, BYTES);
    invalidate_frame(&device, frame, WIDTH, HEIGHT);
    assert(r->content_generation == 1);
    r->source_generation = r->content_generation;
    invalidate_frame(&device, frame, WIDTH - 1, HEIGHT);
    assert(r->content_generation == 2);
    free(frame);
}
static void refresh_cases(void)
{
    struct ms912x_usb_request *r = setup();
    assert(!ms912x_usb_request_refresh(r) && !conversions);
    r->content_generation = 1;
    device.conversion_busy = true;
    assert(!ms912x_usb_request_refresh(r) && conversions == 1);
    assert(r->source_generation == r->content_generation);
    assert(!device.conversion_busy && !device.transfer_lock);
    r = setup(); r->content_generation = 1; action = 1;
    assert(!ms912x_usb_request_refresh(r) && conversions == 1);
    assert(r->source_generation == encoded_generation && r->source_generation == 1);
    assert(r->content_generation == 2 && device.pending_valid);
    assert(packet_pair(r, 0, 0) == 0 && device.latest_pixels[0] == 1);
    /* The production USB preflight permits only one refresh, despite publishers. */
    r = setup(); r->content_generation = 1; action = 1;
    ms912x_usb_request_worker(&r->work);
    assert(conversions == 1 && usb_submissions == 1 && device.pending_valid);
    assert(r->state == MS912X_REQUEST_FREE && r->source_generation == 1);
    for (unsigned int a = 2; a <= 4; a++) {
        r = setup(); r->content_generation = 1; action = a;
        assert(ms912x_usb_request_refresh(r) == (a == 4 ? -EIO : -ECANCELED));
        assert(conversions == 1 && !device.conversion_busy && !device.transfer_lock);
    }
    r = setup(); r->content_generation = 1; stage_failure = true;
    assert(ms912x_usb_request_refresh(r) == -EINVAL && !conversions && !device.conversion_busy);
    r = setup(); r->content_generation = 1; r->generation--;
    assert(ms912x_usb_request_refresh(r) == -ECANCELED && !conversions);
    r = setup(); r->content_generation = 1; device.transfer_stopping = true;
    assert(ms912x_usb_request_refresh(r) == -ECANCELED && !conversions);
    r = setup(); r->content_generation = 1; device.cursor_commit_pending = true;
    ms912x_usb_request_worker(&r->work);
    assert(!conversions && !usb_submissions && requeues && r->state == MS912X_REQUEST_FREE);
    r = setup(); r->content_generation = 1; device.failed_rect_valid = true;
    device.failed_generation = 7; device.failed_width = WIDTH; device.failed_height = HEIGHT;
    device.failed_rect = DRM_RECT_INIT(32, 16, 8, 8);
    ms912x_usb_request_worker(&r->work);
    assert(!conversions && !usb_submissions && requeues && r->state == MS912X_REQUEST_FREE);
    r = setup(); r->content_generation = 1;
    device.cursor_enabled = true; device.cursor_x = 12; device.cursor_y = 4;
    device.cursor_width = 16; device.cursor_height = 8;
    device.cursor_source_height = 8; device.cursor_pixels[0] = 0x12345678;
    assert(!ms912x_usb_request_refresh(r) && conversions == 1 && r->cursor_included);
    assert(r->cursor_pixels[0] == 0x12345678);
    struct drm_rect *last = &r->rects[r->rect_count - 1];
    assert(last->x1 <= 12 && last->x2 >= 28 && last->y1 <= 4 && last->y2 >= 12);
    size_t bytes;
    assert(ms912x_rect_list_transfer_len(r->rects, r->rect_count, WIDTH, HEIGHT, &bytes));
    assert(bytes == r->transfer_len);
    r = setup(); r->content_generation = 1;
    rects[0] = DRM_RECT_INIT(WIDTH, 0, 4, 4); /* Force full-frame fallback. */
    device.cursor_enabled = true; device.cursor_x = 4; device.cursor_y = 4;
    device.cursor_width = 8; device.cursor_height = 8;
    device.cursor_source_height = 8; device.cursor_pixels[0] = 0x87654321;
    assert(!ms912x_usb_request_refresh(r) && r->rect_count == 1 && r->cursor_included);
    assert(r->cursor_pixels[0] == 0x87654321);
    assert(r->rects[0].x1 == 0 && r->rects[0].x2 == WIDTH);

    r = setup(); r->content_generation = 1;
    device.cursor_enabled = true; device.cursor_x = 40; device.cursor_y = 20;
    device.cursor_width = 8; device.cursor_height = 8;
    device.cursor_source_height = 8; device.cursor_pixels[0] = 0xabcdef01;
    assert(!ms912x_usb_request_refresh(r) && !r->cursor_included);
    assert(!r->cursor_pixels[0]);

    r = setup(); r->content_generation = 1;
    device.cursor_enabled = true; device.cursor_x = WIDTH + 1; device.cursor_y = 4;
    device.cursor_width = 8; device.cursor_height = 8;
    device.cursor_source_height = 8; device.cursor_pixels[0] = 0xabcdef01;
    assert(!ms912x_usb_request_refresh(r) && !r->cursor_included);
    assert(!r->cursor_pixels[0]);

    r = setup(); r->content_generation = 1;
    device.cursor_enabled = false; device.cursor_x = 4; device.cursor_y = 4;
    device.cursor_width = 8; device.cursor_height = 8;
    device.cursor_source_height = 8; device.cursor_pixels[0] = 0xabcdef01;
    assert(!ms912x_usb_request_refresh(r) && !r->cursor_included);
    assert(!r->cursor_pixels[0]);

}

static struct ms912x_usb_request *setup_conversion(void)
{
    struct ms912x_usb_request *r = setup();

    r->state = MS912X_REQUEST_FREE;
    r->content_generation = 1;
    device.pending_valid = true;
    device.pending_width = WIDTH;
    device.pending_height = HEIGHT;
    device.pending_rect_count = 2;
    device.pending_deadline = 5;
    pending_rects[0] = DRM_RECT_INIT(0, 0, 16, 8);
    pending_rects[1] = DRM_RECT_INIT(32, 16, 16, 8);
    device.previous_damage_width = WIDTH;
    device.previous_damage_height = HEIGHT;
    device.previous_damage_generation = device.frame_generation;
    device.previous_damage_rect_count = 1;
    previous_rects[0] = DRM_RECT_INIT(52, 24, 8, 8);
    return r;
}

static void conversion_cases(void)
{
    struct ms912x_usb_request *r = setup_conversion();

    statistics_enabled = true;
    ms912x_conversion_worker(&device.conversion_work.work);
    assert(conversions == 1 && queued_requests == 1);
    assert(r->state == MS912X_REQUEST_USB_QUEUED && r->replay_included);
    assert(r->source_generation == 1 && r->content_generation == 1);
    assert(r->rect_count == 3 && !r->current_rect_count);
    assert(!device.pending_valid && !device.pending_rect_count && !device.pending_deadline);
    assert(!device.conversion_busy && !device.transfer_lock);
    assert(device.previous_damage_rect_count == 2);
    assert(device.previous_damage_rects == pending_rects);
    assert(device.pending_rects == current_rects && r->current_rects == previous_rects);
    assert(device.previous_damage_rects[0].x1 == 0 &&
           device.previous_damage_rects[1].x1 == 32);
    assert(planning_calls >= 2);
    assert(device.stats.conversion_attempts == 0 && device.stats.conversion_successes == 0);
    assert(device.stats.conversion_pixels == 0 && device.stats.damage_replay_batches_queued == 1);

    r = setup_conversion();
    device.cursor_enabled = true; device.cursor_x = 4; device.cursor_y = 4;
    device.cursor_width = 8; device.cursor_height = 8;
    device.cursor_source_height = 8; device.cursor_pixels[0] = 0x13572468;
    ms912x_conversion_worker(&device.conversion_work.work);
    assert(queued_requests == 1 && r->cursor_included);
    assert(r->cursor_pixels[0] == 0x13572468);

    r = setup_conversion(); action = 1;
    ms912x_conversion_worker(&device.conversion_work.work);
    assert(conversions == 1 && queued_requests == 1 && device.pending_valid);
    assert(r->source_generation == 1 && r->content_generation == 2);
    assert(packet_pair(r, 0, 0) == 0 && device.latest_pixels[0] == 1);

    for (unsigned int a = 2; a <= 4; a++) {
        r = setup_conversion(); action = a;
        statistics_enabled = true;
        ms912x_conversion_worker(&device.conversion_work.work);
        assert(conversions == 1 && !queued_requests && r->state == MS912X_REQUEST_FREE);
        assert(!device.conversion_busy && !device.transfer_lock);
        assert(device.previous_damage_rect_count == 1 && previous_rects[0].x1 == 52);
        if (a == 4) {
            assert(device.pending_valid && device.pending_force_full && device.pending_rect_count == 1);
            assert(device.pending_rects[0].x2 == WIDTH && device.pending_rects[0].y2 == HEIGHT);
            assert(device.failed_present_sequence == 10 && invalidations == 1);
            assert(device.stats.conversion_attempts == 0 && !device.stats.conversion_successes);
        } else {
            assert(!device.pending_valid && !invalidations && !device.failed_present_sequence);
        }
    }

    r = setup_conversion(); queue_failure = true;
    ms912x_conversion_worker(&device.conversion_work.work);
    assert(conversions == 1 && !queued_requests && r->state == MS912X_REQUEST_FREE);
    assert(device.pending_valid && device.pending_force_full && device.pending_rect_count == 1);
    assert(device.pending_rects[0].x2 == WIDTH && device.pending_rects[0].y2 == HEIGHT);
    assert(device.previous_damage_rect_count == 1 && !device.failed_present_sequence);
    assert(!device.conversion_busy && !device.transfer_lock);

    r = setup_conversion(); stage_failure = true;
    ms912x_conversion_worker(&device.conversion_work.work);
    assert(!conversions && !queued_requests && r->state == MS912X_REQUEST_FREE);
    assert(device.pending_valid && device.pending_force_full && device.pending_rect_count == 1);
    assert(!device.conversion_busy && !device.transfer_lock);

    r = setup_conversion(); growth_failure = true;
    device.previous_damage_rect_capacity = 1;
    ms912x_conversion_worker(&device.conversion_work.work);
    assert(queued_requests == 1 && r->replay_included);
    assert(device.previous_damage_rect_count == 2);
    assert(device.previous_damage_rects == pending_rects && r->current_rects == previous_rects);
    assert(r->current_rect_capacity == 1); /* No replay allocation or bounding fallback. */
    assert(!vector_grows);

    for (unsigned int guard = 0; guard < 6; guard++) {
        r = setup_conversion();
        if (guard == 0) device.transfer_stopping = true;
        if (guard == 1) device.conversion_busy = true;
        if (guard == 2) device.cursor_commit_pending = true;
        if (guard == 3) device.pending_valid = false;
        if (guard == 4) device.pending_rect_count = 0;
        if (guard == 5) r->state = device.requests[1].state = MS912X_REQUEST_USB_ACTIVE;
        unsigned int pending_count = device.pending_rect_count;
        ms912x_conversion_worker(&device.conversion_work.work);
        assert(!conversions && !queued_requests && !device.transfer_lock);
        assert(device.pending_rect_count == pending_count);
        assert(device.conversion_busy == (guard == 1));
    }

    r = setup_conversion(); device.failed_rect_valid = true;
    device.failed_generation = device.frame_generation;
    device.failed_width = WIDTH; device.failed_height = HEIGHT;
    ms912x_conversion_worker(&device.conversion_work.work);
    assert(queued_requests == 1 && r->rect_count == 1 && !r->replay_included);
    assert(rects[0].x1 == 0 && rects[0].y1 == 0 && rects[0].x2 == WIDTH && rects[0].y2 == HEIGHT);

    r = setup_conversion(); growth_failure = true; r->current_rect_capacity = 1;
    ms912x_conversion_worker(&device.conversion_work.work);
    assert(queued_requests == 1 && !r->current_rect_count);
    assert(device.previous_damage_rect_count == 2 && device.pending_rect_capacity == 1);
    assert(device.previous_damage_rects[0].x1 == 0 &&
           device.previous_damage_rects[1].x1 == 32);

    r = setup_conversion(); growth_failure = true; r->rect_capacity = 1;
    ms912x_conversion_worker(&device.conversion_work.work);
    assert(queued_requests == 1 && r->rect_count == 1 && r->replay_included);
    assert(rects[0].x1 == 0 && rects[0].y1 == 0 && rects[0].x2 == 60 && rects[0].y2 == HEIGHT);
}

static void preparation_cases(void)
{
    struct ms912x_usb_request *r = setup_conversion();
    struct ms912x_conversion_batch batch = {};
    struct drm_rect update = DRM_RECT_INIT(0, 0, 4, 4);
    struct drm_rect *coverage = r->rects;
    u64 source_sequence;

    mutex_lock(&device.transfer_lock);
    assert(ms912x_conversion_reserve_locked(&device, &batch) == r);
    assert(r->state == MS912X_REQUEST_PREPARING && device.conversion_busy);
    assert(r->current_rects == pending_rects && device.pending_rects == current_rects);
    assert(batch.previous_rects == previous_rects && batch.previous_rect_count == 1);
    size_t reserved_bytes = r->transfer_len;
    /* Unpublished geometry may be incomplete or under reallocation. */
    r->source_generation = r->content_generation;
    r->rects = NULL; r->rect_count = ~0U;
    ms912x_payload_invalidate_all(&device);
    ms912x_payload_invalidate_damage(&device, &update, 1);
    assert(r->content_generation == 1);
    assert(reserved_bytes && ms912x_present_queue_bytes_locked(&device) == reserved_bytes);
    mutex_unlock(&device.transfer_lock);
    invalidate_frame(&device, device.latest_pixels, WIDTH, HEIGHT);
    assert(r->content_generation == 1);
    mutex_lock(&device.transfer_lock);
    r->rects = coverage;
    mutex_unlock(&device.transfer_lock);
    ms912x_conversion_prepare_batch(r, &batch);
    mutex_lock(&device.snapshot_lock);
    mutex_lock(&device.transfer_lock);
    assert(ms912x_conversion_stage_locked(r, &source_sequence));
    assert(r->state == MS912X_REQUEST_CONVERTING && source_sequence == 10);
    mutex_unlock(&device.transfer_lock);
    mutex_unlock(&device.snapshot_lock);

    r = setup_conversion(); planning_action = 1;
    ms912x_conversion_worker(&device.conversion_work.work);
    assert(conversions == 1 && queued_requests == 1 && device.pending_valid);
    assert(device.pending_rect_count == 1 && device.pending_deadline == 99);
    assert(device.pending_rects[0].x2 == 4 && device.previous_damage_rects[0].x2 == 16);
    assert(packet_pair(r, 0, 0) == 0x123456);
    assert(r->source_generation == r->content_generation && r->content_generation == 1);

    for (unsigned int inject = 2; inject <= 3; inject++) {
        r = setup_conversion(); planning_action = inject;
        ms912x_conversion_worker(&device.conversion_work.work);
        assert(!conversions && !queued_requests && r->state == MS912X_REQUEST_FREE);
        assert(!device.conversion_busy && !device.transfer_lock);
        if (inject == 3)
            assert(device.pending_valid && device.pending_rect_count == 1 &&
                   device.pending_deadline == 99 && !device.previous_damage_rect_count);
    }

    r = setup_conversion(); planning_action = 4;
    ms912x_conversion_worker(&device.conversion_work.work);
    assert(conversions == 1 && queued_requests == 1 && r->rect_count == 1);
    assert(rects[0].x2 == WIDTH && rects[0].y2 == HEIGHT && !r->replay_included);
    assert(device.previous_damage_rect_count == 1);

    r = setup_conversion(); planning_action = 5;
    ms912x_conversion_worker(&device.conversion_work.work);
    assert(queued_requests == 1 && r->cursor_included && r->cursor_pixels[0] == 0x24681357);
    struct drm_rect *last = &r->rects[r->rect_count - 1];
    assert(last->x1 <= 4 && last->x2 >= 12 && last->y1 <= 4 && last->y2 >= 12);

    r = setup_conversion(); planning_action = 1; action = 4;
    ms912x_conversion_worker(&device.conversion_work.work);
    assert(!queued_requests && device.failed_present_sequence == 11);
    assert(device.pending_force_full && device.pending_rect_count == 1);

    r = setup_conversion(); r->rect_capacity = 1;
    ms912x_conversion_worker(&device.conversion_work.work);
    assert(queued_requests == 1 && r->rects == grown_rects && vector_grows == 1);
    assert(r->rect_count == 3 && device.previous_damage_rect_count == 2);
}

static void vector_ownership_cases(void)
{
    static struct drm_rect second_rects[32], second_current[32];
    static u32 second_output[MS912X_MAX_WIDTH * HEIGHT];
    setup();
    device.requests[1] = (struct ms912x_usb_request){
        .ms912x = &device, .rects = second_rects, .rect_capacity = ARRAY_SIZE(second_rects),
        .current_rects = second_current, .current_rect_capacity = ARRAY_SIZE(second_current),
        .transfer_buffer = second_output,
    };
    for (unsigned int frame = 0; frame < 12; frame++) {
        struct ms912x_usb_request *r = &device.requests[frame & 1];
        struct drm_rect damage = DRM_RECT_INIT((frame * 8) % 48, (frame % 4) * 4, 8, 4);

        r->state = MS912X_REQUEST_FREE;
        device.requests[(frame & 1) ^ 1].state = MS912X_REQUEST_USB_ACTIVE;
        device.pending_width = WIDTH; device.pending_height = HEIGHT;
        device.pending_rects[0] = damage;
        device.pending_rect_count = 1; device.pending_valid = true;
        device.published_frame_sequence++;
        conversions = queued_requests = 0;
        ms912x_conversion_worker(&device.conversion_work.work);
        assert(conversions == 1 && queued_requests == 1);
        assert(r->state == MS912X_REQUEST_USB_QUEUED && !r->current_rect_count);
        assert(device.previous_damage_rect_count == 1);
        assert(drm_rect_equals(&device.previous_damage_rects[0], &damage));
        struct drm_rect *owners[] = {
            device.pending_rects, device.previous_damage_rects,
            device.requests[0].current_rects, device.requests[1].current_rects,
        };
        for (unsigned int i = 0; i < ARRAY_SIZE(owners); i++)
            for (unsigned int j = i + 1; j < ARRAY_SIZE(owners); j++)
                assert(owners[i] != owners[j]);
    }
}

static void staging_cases(void)
{
    /* Copy only covered pixels at native and each supported reduced geometry. */
    const unsigned int widths[] = {60, 48, 40, 30};
    const unsigned int heights[] = {30, 24, 20, 15};
    for (unsigned int scale = 0; scale < ARRAY_SIZE(widths); scale++) {
        setup();
        unsigned int width = widths[scale], height = heights[scale];
        struct drm_rect regions[] = {
            DRM_RECT_INIT(4, 1, 8, 2),
            DRM_RECT_INIT(width - 8, height - 3, 8, 3),
        };
        for (unsigned int i = 0; i < BYTES / sizeof(u32); i++) {
            device.latest_pixels[i] = 0x12345678U + i;
            device.conversion_pixels[i] = 0xdeadbeef;
        }
        device.conversion_busy = true;
        mutex_lock(&device.snapshot_lock);
        assert(ms912x_snapshot_stage_conversion_real(&device, regions, 2, width, height));
        assert(!ms912x_snapshot_stage_conversion_real(&device, regions, 0, width, height));
        struct drm_rect invalid = DRM_RECT_INIT(width, 0, 1, 1);
        assert(!ms912x_snapshot_stage_conversion_real(&device, &invalid, 1, width, height));
        mutex_unlock(&device.snapshot_lock);
        for (unsigned int y = 0; y < HEIGHT; y++)
            for (unsigned int x = 0; x < UYVY_WORD_STRIDE; x++) {
                unsigned int offset = y * UYVY_WORD_STRIDE + x;
                bool covered = false;
                for (unsigned int i = 0; i < ARRAY_SIZE(regions); i++)
                    covered |= x * 2 >= (unsigned int)regions[i].x1 &&
                               x * 2 < (unsigned int)regions[i].x2 &&
                               y >= (unsigned int)regions[i].y1 &&
                               y < (unsigned int)regions[i].y2;
                assert(device.conversion_pixels[offset] ==
                       (covered ? device.latest_pixels[offset] : 0xdeadbeef));
            }
    }

    for (unsigned int refresh = 0; refresh < 2; refresh++) {
        for (unsigned int inject = 1; inject <= 3; inject++) {
            struct ms912x_usb_request *r = refresh ? setup() : setup_conversion();
            r->content_generation = 1;
            staging_action = inject;
            if (refresh) {
                assert(ms912x_usb_request_refresh(r) == (inject < 3 ? -ECANCELED : 0));
            } else {
                ms912x_conversion_worker(&device.conversion_work.work);
                assert(queued_requests == (inject == 3));
            }
            assert(staging_copies && conversions == (inject == 3));
            assert(!device.conversion_busy && !device.transfer_lock && !device.snapshot_lock);
            if (inject == 2) assert(device.pending_valid && device.pending_rect_count == 1);
            if (inject == 3) assert(r->source_generation == 1 && r->content_generation == 2);
        }
        /* Both a direct writer and a scan publication must wait for the reader. */
        for (unsigned int kind = 4; kind <= 5; kind++)
            for (unsigned int iteration = 0; iteration < 20; iteration++) {
                struct ms912x_usb_request *r = refresh ? setup() : setup_conversion();
                struct drm_rect full = DRM_RECT_INIT(0, 0, WIDTH, HEIGHT);
                r->content_generation = 1;
                if (refresh) r->rects[0] = full;
                else {
                    device.pending_rects[0] = full;
                    device.pending_rect_count = 1;
                    device.previous_damage_rect_count = 0;
                }
                for (unsigned int i = 0; i < BYTES / sizeof(u32); i++)
                    device.latest_pixels[i] = 0x112233;
                staging_action = kind;
                if (refresh) assert(!ms912x_usb_request_refresh(r));
                else ms912x_conversion_worker(&device.conversion_work.work);
                assert(!pthread_join(publisher, NULL));
                assert(conversions == 1 && device.pending_valid);
                assert(r->source_generation == 1 && r->content_generation == 2);
                for (unsigned int y = 0; y < HEIGHT; y++)
                    for (unsigned int x = 0; x < WIDTH / 2; x++) {
                        unsigned int offset = y * UYVY_WORD_STRIDE + x;
                        assert(packet_pair(r, x * 2, y) == 0x112233);
                        assert(device.latest_pixels[offset] == 0x654321);
                    }
                assert(!device.snapshot_lock && !device.transfer_lock && !device.conversion_busy);
            }
    }
}

static void comparison_cases(void)
{
    struct ms912x_usb_request *r;
    static u32 frame[MS912X_MAX_WIDTH * HEIGHT];
    struct ms912x_payload_check checks[ARRAY_SIZE(device.requests)];

    for (unsigned int inject = 1; inject <= 9; inject++) {
        r = setup();
        memset(frame, 0, BYTES);
        frame[7 * UYVY_WORD_STRIDE + 7] = 1; /* Unsampled row, last covered column. */
        r->state = MS912X_REQUEST_CONVERTING;
        if (inject == 3) {
            r->rects = malloc(sizeof(*r->rects));
            assert(r->rects);
            *r->rects = rects[0];
        }
        comparison_action = inject;
        mutex_lock(&device.snapshot_lock);
        ms912x_payload_compare_frame(&device, frame, WIDTH, HEIGHT, checks);
        assert(comparison_calls && checks[0].changed);
        assert(checks[0].source_generation == 0 && checks[0].generation == 7);
        mutex_lock(&device.transfer_lock);
        ms912x_payload_invalidate_compared(&device, checks);
        mutex_unlock(&device.transfer_lock);
        mutex_unlock(&device.snapshot_lock);
        if (inject == 1 || inject == 4 || inject == 7)
            assert(r->content_generation == 1);
        else
            assert(!r->content_generation);
        if (inject == 3) assert(!r->rects && r->state == MS912X_REQUEST_PREPARING);
        assert(!device.transfer_lock && !device.snapshot_lock);
    }

    /* Overlapping requests retain independent coverage and early exits. */
    r = setup();
    memset(frame, 0, BYTES);
    struct drm_rect second[] = {DRM_RECT_INIT(8, 4, 16, 8)};
    device.requests[1] = *r;
    device.requests[1].rects = second;
    frame[7 * UYVY_WORD_STRIDE + 7] = 0;
    invalidate_frame(&device, frame, WIDTH, HEIGHT);
    assert(!r->content_generation && !device.requests[1].content_generation);
    assert(comparison_calls == 16);
    frame[7 * UYVY_WORD_STRIDE + 7] = 0xff000000;
    comparison_calls = 0;
    invalidate_frame(&device, frame, WIDTH, HEIGHT);
    assert(r->content_generation == 1 && device.requests[1].content_generation == 1);
    assert(comparison_calls == 12);
    comparison_calls = 0;
    invalidate_frame(&device, frame, WIDTH, HEIGHT);
    assert(!comparison_calls); /* Already dirty: neither coverage copy nor pixel walk. */

    /* The shared coverage vector must hold the largest valid request. */
    r = setup();
    memset(frame, 0, BYTES);
    struct drm_rect *maximum = calloc(MS912X_MAX_BATCH_RECTS + 1, sizeof(*maximum));
    assert(maximum);
    for (unsigned int i = 0; i < MS912X_MAX_BATCH_RECTS; i++)
        maximum[i] = DRM_RECT_INIT(0, 0, 4, 1);
    r->rects = maximum; r->rect_count = MS912X_MAX_BATCH_RECTS;
    invalidate_frame(&device, frame, WIDTH, HEIGHT);
    assert(!r->content_generation && comparison_calls == MS912X_MAX_BATCH_RECTS);
    assert(comparison_bytes == MS912X_MAX_BATCH_RECTS * 4 * 2);
    comparison_calls = 0;
    r->rect_count++;
    invalidate_frame(&device, frame, WIDTH, HEIGHT);
    assert(r->content_generation == 1 && !comparison_calls);
    free(maximum);
}

static void unplugged_usb_worker_isolated(void)
{
    struct ms912x_usb_request request = {
        .ms912x = &unplugged_device,
        .state = MS912X_REQUEST_USB_ACTIVE,
    };

    setup();
    memset(&unplugged_device, 0, sizeof(unplugged_device));
    unplugged_device.drm = 1; /* drm_dev_enter() rejects the removed adapter. */
    device.requests[0].state = MS912X_REQUEST_USB_ACTIVE;
    device.pending_valid = true;
    device.pending_rect_count = 1;
    device.pending_width = WIDTH;
    device.pending_height = HEIGHT;
    device.previous_damage_rect_count = 1;
    device.frame_generation = 17;
    unsigned int submissions_before = usb_submissions;
    unsigned int conversions_before = conversions;

    ms912x_usb_request_worker(&request.work);

    assert(request.state == MS912X_REQUEST_FREE);
    assert(!unplugged_device.transfer_lock);
    assert(device.requests[0].state == MS912X_REQUEST_USB_ACTIVE);
    assert(device.pending_valid && device.pending_rect_count == 1);
    assert(device.pending_width == WIDTH && device.pending_height == HEIGHT);
    assert(device.previous_damage_rect_count == 1 && device.frame_generation == 17);
    assert(usb_submissions == submissions_before && conversions == conversions_before);
}

static void complete_usb_worker_cases(void)
{
    const int results[] = {0, -EIO, -ETIMEDOUT, -ENODEV};
    for (unsigned int i = 0; i < ARRAY_SIZE(results); i++) {
        struct ms912x_usb_request *r = setup();
        statistics_enabled = true;
        r->content_generation = 1; /* Refresh serializes the real staged pixels. */
        usb_result = results[i];
        usb_short = usb_result == -EIO;
        usb_duration = 1000000;
        ms912x_usb_request_worker(&r->work);
        assert(conversions == 1 && usb_submissions == 1);
        assert(r->state == MS912X_REQUEST_FREE && !device.transfer_lock);
        assert(!device.snapshot_lock && !device.conversion_busy);
        bool failure = usb_result && usb_result != -ENODEV;
        assert(device.failed_rect_valid == failure);
        assert(device.stats.usb_errors == (unsigned)failure);
        assert(device.stats.usb_timeouts == (unsigned)(usb_result == -ETIMEDOUT));
        assert(device.stats.usb_short_transfers == (unsigned)usb_short);
        if (failure) {
            struct drm_rect full = DRM_RECT_INIT(0, 0, WIDTH, HEIGHT);
            assert(drm_rect_equals(&device.failed_rect, &full));
        }
        assert(device.usb_successful_transfers == (unsigned)(usb_result == 0));
        if (!usb_result) {
            assert(device.usb_speed_samples == 1);
            assert(device.usb_speed_mean_bytes_per_sec == r->transfer_len * 1000);
        }
    }
}

int main(void)
{
    invalidate_cases(); refresh_cases(); conversion_cases(); preparation_cases(); vector_ownership_cases();
    staging_cases();
    comparison_cases();
    struct ms912x_usb_request *r = setup();
    r->rect_count = 3;
    r->cursor_included = true; r->cursor_x = r->cursor_y = 8;
    r->cursor_width = r->cursor_height = 8;
    rects[0] = DRM_RECT_INIT(0, 0, 20, 20);
    rects[1] = DRM_RECT_INIT(32, 16, 8, 8);
    rects[2] = DRM_RECT_INIT(8, 8, 8, 8);
    struct drm_rect saved[3]; memcpy(saved, rects, sizeof(saved));
    r->cursor_pixels[0] = 0xffffffff;
    assert(ms912x_request_finish_batch(r) == 1);
    assert(composite_calls == 1 && composite_offsets[0] ==
           ms912x_rect_encoded_bytes(&rects[0]) + ms912x_rect_encoded_bytes(&rects[1]));
    assert(!memcmp(saved, rects, sizeof(saved)) && r->rect_count == 3);
    unplugged_usb_worker_isolated();
    complete_usb_worker_cases();
    printf("PASS payload/conversion: %u scenarios; publications during conversion, worker guards, "
           "unlocked staging, source reuse, private batch preparation, replay, "
           "unlocked exact comparisons, request reuse, allocation/queue failures, "
           "bounded refresh, and per-adapter USB unplug slot release\n", cases);
    return 0;
}
