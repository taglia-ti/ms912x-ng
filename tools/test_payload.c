// SPDX-License-Identifier: GPL-2.0-only
/* Production invalidation, refresh and worker preflight with mocked I/O. */
#include <assert.h>
#include <stdio.h>
#include "ms912x_batch.h"
#include "ms912x_payload.h"

#define WIDTH 64
#define HEIGHT 32
#define BYTES (MS912X_MAX_WIDTH * HEIGHT * sizeof(u32))
#define container_of(p, t, field) ((t *)((char *)(p) - offsetof(t, field)))
#define interface_to_usbdev(p) ((struct usb_device *)NULL)
#define ALIGN(x, a) (((x) + (a) - 1) & ~((a) - 1))
#define ALIGN_DOWN(x, a) ((x) & ~((a) - 1))
#define to_delayed_work(p) container_of(p, struct delayed_work, work)
#define MS912X_STAT_INC(d, n) do { if (statistics_enabled) (d)->stats.n++; } while (0)
#define MS912X_STAT_ADD(d, n, v) do { if (statistics_enabled) (d)->stats.n += (v); } while (0)
#define WARN_ON_ONCE(c) (c)
#define drm_err_ratelimited(d, ...) ((void)(d))
#define wait_event(w, c) do { assert(!device.transfer_lock); device.conversion_busy = false; } while (0)
static struct ms912x_device device;
static struct drm_rect rects[32];
static struct drm_rect current_rects[32], pending_rects[32], previous_rects[32];
static unsigned int conversions, usb_submissions, action, invalidations, requeues;
static unsigned int composite_calls;
static size_t composite_offsets[32];
static struct { bool dbg_show_draws; } ms912x_parameters;
static bool stage_failure;
static bool queue_failure, growth_failure, statistics_enabled;
static unsigned int queued_requests;
static u64 encoded_generation;
static unsigned int cases;
static void mutex_lock(int *m) { assert(!*m); *m = 1; }
static void mutex_unlock(int *m) { assert(*m); *m = 0; }
static void wake_up_all(int *w) { (void)w; }
static bool ms912x_stats_enabled(void) { return statistics_enabled; }
static void atomic64_add(u64 n, u64 *p) { *p += n; }
static u64 ktime_get_ns(void) { return 1; }
static bool drm_dev_enter(int *d, int *idx) { *idx = 0; return true; }
static void drm_dev_exit(int idx) { (void)idx; }
static void ms912x_schedule_conversion_locked(struct ms912x_device *d) { assert(d->transfer_lock); }
static void ms912x_transfer_update_pacing_locked(struct ms912x_device *d) { assert(d->transfer_lock); }
static void ms912x_damage_scan_invalidate(struct ms912x_device *d) { invalidations++; }
static void ms912x_transfer_requeue_rect_locked(struct ms912x_device *d,
    const struct drm_rect *r, unsigned int w, unsigned int h) { requeues++; d->pending_valid = true; }
static void ms912x_usb_requeue_request_rects_locked(struct ms912x_device *d,
    struct ms912x_usb_request *r) { requeues++; d->pending_valid = true; }
static bool ms912x_usb_request_covers_rect(struct ms912x_usb_request *r,
    struct drm_rect *rect) { return false; }
struct drm_rect *ms912x_rect_vector_grow(struct drm_rect *p, unsigned int *capacity,
    unsigned int required, unsigned int limit)
{
    if (required <= *capacity) return p;
    assert(growth_failure); /* Normal cases have persistent preallocated vectors. */
    return NULL;
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
static bool ms912x_rect_list_transfer_len(const struct drm_rect *rs, unsigned int n,
    unsigned int w, unsigned int h, size_t *len)
{
    *len = 0;
    if (!n) return false;
    for (unsigned int i = 0; i < n; i++) {
        if (rs[i].x1 < 0 || rs[i].y1 < 0 || rs[i].x2 > (int)w || rs[i].y2 > (int)h ||
            rs[i].x1 >= rs[i].x2 || rs[i].y1 >= rs[i].y2) return false;
        *len += ms912x_rect_encoded_bytes(&rs[i]);
    }
    return *len <= w * h * 2 + 16;
}
static bool ms912x_snapshot_stage_conversion(struct ms912x_device *d,
    const struct drm_rect *rs, unsigned int n, unsigned int w, unsigned int h)
{
    assert(d->transfer_lock && d->conversion_busy);
    if (stage_failure) return false;
    memcpy(d->conversion_xrgb, d->latest_xrgb, BYTES);
    encoded_generation = d->requests[0].content_generation;
    return true;
}
static int ms912x_convert_xrgb8888_to_uyvy_batch(void *out, const __le32 *in,
    struct ms912x_device *d, unsigned int w, unsigned int h,
    const struct drm_rect *rs, unsigned int n)
{
    assert(!d->transfer_lock && d->conversion_busy);
    conversions++;
    assert(conversions < 10); /* Detect refresh starvation immediately. */
    memcpy(out, in, BYTES);
    mutex_lock(&d->transfer_lock);
    if (action == 1) {
        ms912x_payload_invalidate_damage(d, &rs[0], 1);
        d->latest_xrgb[rs[0].y1 * MS912X_MAX_WIDTH + rs[0].x1]++;
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
    return 1;
}
static void ms912x_debug_draw_uyvy_border(void *out, const struct drm_rect *a,
                                        const struct drm_rect *b) {}

#include "refresh.h"

static struct ms912x_usb_request *setup(void)
{
    static u32 latest[MS912X_MAX_WIDTH * HEIGHT], scratch[MS912X_MAX_WIDTH * HEIGHT];
    static u32 output[MS912X_MAX_WIDTH * HEIGHT];
    memset(&device, 0, sizeof(device)); memset(latest, 0, sizeof(latest));
    device.latest_xrgb = latest; device.conversion_xrgb = scratch;
    device.frame_generation = 7;
    device.published_frame_sequence = 10;
    device.pending_rects = pending_rects;
    device.previous_damage_rects = previous_rects;
    device.previous_damage_rect_capacity = ARRAY_SIZE(previous_rects);
    struct ms912x_usb_request *r = &device.requests[0];
    *r = (struct ms912x_usb_request){.ms912x = &device, .rects = rects, .rect_count = 1,
        .rect_capacity = ARRAY_SIZE(rects), .width = WIDTH, .height = HEIGHT,
        .current_rects = current_rects, .current_rect_capacity = ARRAY_SIZE(current_rects),
        .generation = 7, .state = MS912X_REQUEST_USB_QUEUED, .transfer_buffer = output};
    rects[0] = DRM_RECT_INIT(0, 0, 16, 8);
    conversions = usb_submissions = action = invalidations = requeues = 0;
    stage_failure = false; composite_calls = 0;
    queue_failure = growth_failure = statistics_enabled = false;
    queued_requests = 0;
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
    for (unsigned int state = 0; state < 4; state++) {
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
    frame[1] = 0xff000000; /* X is ignored by XRGB image comparisons. */
    ms912x_payload_invalidate_frame(&device, frame, WIDTH, HEIGHT);
    assert(!r->content_generation);
    frame[20 * MS912X_MAX_WIDTH + 40] = 1;
    ms912x_payload_invalidate_frame(&device, frame, WIDTH, HEIGHT);
    assert(!r->content_generation);
    frame[7 * MS912X_MAX_WIDTH + 15] = 1;
    ms912x_payload_invalidate_frame(&device, frame, WIDTH, HEIGHT);
    assert(r->content_generation == 1);
    ms912x_payload_invalidate_frame(&device, frame, WIDTH, HEIGHT);
    assert(r->content_generation == 1); /* Dirty payloads need no repeated comparison. */
    memcpy(device.latest_xrgb, frame, BYTES);
    ms912x_payload_invalidate_frame(&device, frame, WIDTH, HEIGHT);
    assert(r->content_generation == 1);
    r->source_generation = r->content_generation;
    ms912x_payload_invalidate_frame(&device, frame, WIDTH - 1, HEIGHT);
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
    assert(((u32 *)r->transfer_buffer)[0] == 0 && device.latest_xrgb[0] == 1);
    /* The production USB preflight permits only one refresh, despite publishers. */
    r = setup(); r->content_generation = 1; action = 1;
    ms912x_usb_request_worker(&r->work);
    assert(conversions == 1 && usb_submissions == 1 && device.pending_valid);
    assert(r->state == MS912X_REQUEST_USB_ACTIVE && r->source_generation == 1);
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
    ms912x_usb_request_worker(&r->work);
    assert(!conversions && !usb_submissions && requeues && r->state == MS912X_REQUEST_FREE);
    r = setup(); r->content_generation = 1;
    device.cursor_enabled = true; device.cursor_x = 12; device.cursor_y = 4;
    device.cursor_width = 16; device.cursor_height = 8;
    assert(!ms912x_usb_request_refresh(r) && conversions == 1 && r->cursor_included);
    struct drm_rect *last = &r->rects[r->rect_count - 1];
    assert(last->x1 <= 12 && last->x2 >= 28 && last->y1 <= 4 && last->y2 >= 12);
    size_t bytes;
    assert(ms912x_rect_list_transfer_len(r->rects, r->rect_count, WIDTH, HEIGHT, &bytes));
    assert(bytes == r->transfer_len);
    r = setup(); r->content_generation = 1;
    rects[0] = DRM_RECT_INIT(WIDTH, 0, 4, 4); /* Force full-frame fallback. */
    device.cursor_enabled = true; device.cursor_x = 4; device.cursor_y = 4;
    device.cursor_width = 8; device.cursor_height = 8;
    assert(!ms912x_usb_request_refresh(r) && r->rect_count == 1 && r->cursor_included);
    assert(r->rects[0].x1 == 0 && r->rects[0].x2 == WIDTH);

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
    assert(r->rect_count == 3 && r->current_rect_count == 2);
    assert(!device.pending_valid && !device.pending_rect_count && !device.pending_deadline);
    assert(!device.conversion_busy && !device.transfer_lock);
    assert(device.previous_damage_rect_count == 2);
    assert(!memcmp(previous_rects, current_rects, 2 * sizeof(*previous_rects)));
    assert(device.stats.conversion_attempts == 1 && device.stats.conversion_successes == 1);
    assert(device.stats.conversion_pixels == 320 && device.stats.damage_replay_batches_queued == 1);

    r = setup_conversion(); action = 1;
    ms912x_conversion_worker(&device.conversion_work.work);
    assert(conversions == 1 && queued_requests == 1 && device.pending_valid);
    assert(r->source_generation == 1 && r->content_generation == 2);
    assert(((u32 *)r->transfer_buffer)[0] == 0 && device.latest_xrgb[0] == 1);

    for (unsigned int a = 2; a <= 4; a++) {
        r = setup_conversion(); action = a;
        statistics_enabled = true;
        ms912x_conversion_worker(&device.conversion_work.work);
        assert(conversions == 1 && !queued_requests && r->state == MS912X_REQUEST_FREE);
        assert(!device.conversion_busy && !device.transfer_lock);
        assert(device.previous_damage_rect_count == 1 && previous_rects[0].x1 == 52);
        if (a == 4) {
            assert(device.pending_valid && device.pending_force_full && device.pending_rect_count == 1);
            assert(pending_rects[0].x2 == WIDTH && pending_rects[0].y2 == HEIGHT);
            assert(device.failed_present_sequence == 10 && invalidations == 1);
            assert(device.stats.conversion_attempts == 1 && !device.stats.conversion_successes);
        } else {
            assert(!device.pending_valid && !invalidations && !device.failed_present_sequence);
        }
    }

    r = setup_conversion(); queue_failure = true;
    ms912x_conversion_worker(&device.conversion_work.work);
    assert(conversions == 1 && !queued_requests && r->state == MS912X_REQUEST_FREE);
    assert(device.pending_valid && device.pending_force_full && device.pending_rect_count == 1);
    assert(pending_rects[0].x2 == WIDTH && pending_rects[0].y2 == HEIGHT);
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
    assert(device.previous_damage_rect_count == 1);
    assert(previous_rects[0].x1 == 0 && previous_rects[0].y1 == 0 &&
           previous_rects[0].x2 == 48 && previous_rects[0].y2 == 24);

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
    assert(queued_requests == 1 && r->current_rect_count == 1);
    assert(current_rects[0].x1 == 0 && current_rects[0].y1 == 0 &&
           current_rects[0].x2 == 48 && current_rects[0].y2 == 24);

    r = setup_conversion(); growth_failure = true; r->rect_capacity = 1;
    ms912x_conversion_worker(&device.conversion_work.work);
    assert(queued_requests == 1 && r->rect_count == 1 && r->replay_included);
    assert(rects[0].x1 == 0 && rects[0].y1 == 0 && rects[0].x2 == 60 && rects[0].y2 == HEIGHT);
}

int main(void)
{
    invalidate_cases(); refresh_cases(); conversion_cases();
    struct ms912x_usb_request *r = setup();
    r->rect_count = 3;
    r->cursor_included = true; r->cursor_x = r->cursor_y = 8;
    r->cursor_width = r->cursor_height = 8;
    rects[0] = DRM_RECT_INIT(0, 0, 20, 20);
    rects[1] = DRM_RECT_INIT(32, 16, 8, 8);
    rects[2] = DRM_RECT_INIT(8, 8, 8, 8);
    struct drm_rect saved[3]; memcpy(saved, rects, sizeof(saved));
    assert(ms912x_request_finish_batch(r) == 1);
    assert(composite_calls == 1 && composite_offsets[0] ==
           ms912x_rect_encoded_bytes(&rects[0]) + ms912x_rect_encoded_bytes(&rects[1]));
    assert(!memcmp(saved, rects, sizeof(saved)) && r->rect_count == 3);
    printf("PASS payload/conversion: %u scenarios; publications during conversion, worker guards, "
           "replay, allocation/queue failures and bounded refresh\n", cases);
    return 0;
}
