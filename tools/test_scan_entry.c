// SPDX-License-Identifier: GPL-2.0-only
#include <stdio.h>
#include <pthread.h>

#define MS912X_TEST_SUBMIT_LOCK_HOOK
#define MS912X_TEST_REAL_SCAN_QUEUE
#define MS912X_TEST_ALLOC_HOOK
#include "test_entrypoint_compat.h"
#include "scan-invalidate.h"

struct ms912x_parameters_test ms912x_parameters;
unsigned int test_drm_exit_count, test_cpu_end_count;
bool test_kvrealloc_fail;

struct ms912x_scan_rect_index { unsigned int reset_height; };
struct ms912x_payload_check { bool changed; };
enum ms912x_damage_scan_mode {
	MS912X_DAMAGE_SCAN_OFF, MS912X_DAMAGE_SCAN_ON, MS912X_DAMAGE_SCAN_AUTO,
};
#define container_of(pointer, type, member) \
	((type *)((char *)(pointer) - offsetof(type, member)))
#define wait_event(wait, condition) assert(condition)
#define drm_err_ratelimited(...) ((void)0)

static struct ms912x_device device;
static struct ms912x_scan_rect_index index_state;
static u32 buffers[5][MS912X_MAX_WIDTH * 8];
static unsigned int scenarios, row_calls, full_calls, incremental_calls;
static unsigned int compare_calls, apply_calls, pending_calls, schedule_calls;
static unsigned int threaded_cases, present_wakes;
static unsigned int convert_calls;
static u32 converted_format;
static unsigned int effective_tolerance;
static bool changed, invalidate_after_commit;
static pthread_mutex_t native_locks[5] = {
	PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER,
	PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER,
	PTHREAD_MUTEX_INITIALIZER,
};
static _Thread_local unsigned int owned_locks;
static pthread_mutex_t rendezvous = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t condition = PTHREAD_COND_INITIALIZER;
static bool paused, resume_worker;
enum stage { NONE, CONVERT, FULL_COPY, SAMPLED_ROWS, ROWS, COMPARE };
static enum stage pause_stage;

static unsigned int lock_index(int *lock)
{
	if (lock == &device.submit_lock) return 0;
	if (lock == &device.snapshot_lock) return 1;
	if (lock == &device.transfer_lock) return 2;
	if (lock == &device.damage_scan_lock) return 3;
	assert(lock == &device.scan_queue_lock);
	return 4;
}

bool test_submit_lock_owned(int *lock)
{
	return (owned_locks & (1U << lock_index(lock))) != 0;
}

void test_mutex_lock(int *lock)
{
	unsigned int i = lock_index(lock);

	assert(!(owned_locks & (1U << i)));
	if (i == 1) assert(!(owned_locks & ((1U << 2) | (1U << 3))));
	if (i == 2) assert(!(owned_locks & (1U << 3)));
	assert(!pthread_mutex_lock(&native_locks[i]));
	assert(!*lock); *lock = 1;
	owned_locks |= 1U << i;
}

void test_mutex_unlock(int *lock)
{
	unsigned int i = lock_index(lock);

	assert(*lock && (owned_locks & (1U << i)));
	*lock = 0;
	owned_locks &= ~(1U << i);
	assert(!pthread_mutex_unlock(&native_locks[i]));
}

void test_submit_lock_hook(int *lock) { (void)lock; }

static void assert_compute_unlocked(void)
{
	assert(!(owned_locks & ((1U << 1) | (1U << 2) | (1U << 3))));
}

static void pause_at(enum stage stage)
{
	if (stage != pause_stage) return;
	assert(!pthread_mutex_lock(&rendezvous));
	if (!paused) {
		paused = true;
		assert(!pthread_cond_broadcast(&condition));
		while (!resume_worker)
			assert(!pthread_cond_wait(&condition, &rendezvous));
	}
	assert(!pthread_mutex_unlock(&rendezvous));
}

void test_scan_alloc(void) { assert_compute_unlocked(); }

static void *scan_memcpy(void *destination, const void *source, size_t length)
{
	assert_compute_unlocked();
	assert(((uintptr_t)destination >= (uintptr_t)device.damage_scan_snapshot &&
	        (uintptr_t)destination + length <= (uintptr_t)(device.damage_scan_snapshot + ARRAY_SIZE(buffers[0]))) ||
	       ((uintptr_t)destination >= (uintptr_t)device.damage_scan_snapshot_back &&
	        (uintptr_t)destination + length <= (uintptr_t)(device.damage_scan_snapshot_back + ARRAY_SIZE(buffers[0]))));
	if ((uintptr_t)source >= (uintptr_t)device.damage_scan_snapshot &&
	    (uintptr_t)source < (uintptr_t)(device.damage_scan_snapshot +
						 ARRAY_SIZE(buffers[0])))
		pause_at(SAMPLED_ROWS);
	else
		pause_at(FULL_COPY);
	return memcpy(destination, source, length);
}

void ms912x_scan_rect_index_reset(struct ms912x_scan_rect_index *index,
					 unsigned int height)
{
	assert_compute_unlocked();
	index->reset_height = height;
}

unsigned int ms912x_damage_scan_effective_tolerance(struct ms912x_device *ms912x)
{
	return effective_tolerance;
}

/* Backend arithmetic is covered separately; this fixture tests ownership. */
static void ms912x_damage_scan_select_full_backend_locked(
	struct ms912x_device *ms912x, const u32 *frame, unsigned int width,
	unsigned int height, struct ms912x_scan_config *config)
{
	lockdep_assert_held(&ms912x->damage_scan_lock);
	++full_calls;
}

static void ms912x_damage_scan_select_incremental_backend_locked(
	struct ms912x_device *ms912x, struct ms912x_scan_config *config)
{
	lockdep_assert_held(&ms912x->damage_scan_lock);
	config->use_relaxed = config->tolerance != 0;
	++incremental_calls;
}

struct drm_rect *ms912x_rect_vector_grow(struct drm_rect *rects,
	unsigned int *capacity, unsigned int required, unsigned int limit);

static void ms912x_damage_scan_run_rows(struct ms912x_scan_work *work,
	const struct ms912x_scan_config *config, u64 sequence, unsigned int *scanned)
{
	assert_compute_unlocked();
	assert(work->baseline == device.damage_scan_snapshot);
	assert(work->next_snapshot == device.damage_scan_snapshot_back ||
	       work->next_snapshot == device.damage_scan_snapshot);
	if (config->stride == 2) pause_at(SAMPLED_ROWS);
	pause_at(ROWS);
	++row_calls;
	for (unsigned int y = 0; y < work->height; y++)
		memcpy(work->next_snapshot + y * work->row_words,
		       work->frame + y * work->row_words, work->word_width * sizeof(u32));
	*scanned = DIV_ROUND_UP(work->height, config->stride);
	work->changed_pixels = changed ? 2 : 0;
	if (changed) {
		*work->rects = ms912x_rect_vector_grow(*work->rects,
			work->rect_capacity, 1, MS912X_MAX_DAMAGE_RECTS);
		assert(*work->rects);
		(*work->rects)[0] = DRM_RECT_INIT(1, 1, 2, 1);
		*work->rect_count = 1;
	}
}

static void ms912x_damage_scan_extend_bottom(struct ms912x_scan_work *work)
{
	assert_compute_unlocked();
}

#define memcpy scan_memcpy
#include "ms912x_pixels.h"
#include "scan-entry.h"
#undef memcpy

static void assert_pending_unlocked_scan(void)
{
	lockdep_assert_held(&device.transfer_lock);
	lockdep_assert_not_held(&device.damage_scan_lock);
}

static void ms912x_payload_compare_frame(struct ms912x_device *ms912x,
	const u32 *pixels, unsigned int width, unsigned int height,
	struct ms912x_payload_check *checks)
{
	lockdep_assert_held(&ms912x->snapshot_lock);
	lockdep_assert_not_held(&ms912x->transfer_lock);
	lockdep_assert_not_held(&ms912x->damage_scan_lock);
	pause_at(COMPARE);
	++compare_calls;
	checks[0].changed = changed;
}

static void ms912x_payload_invalidate_compared(struct ms912x_device *ms912x,
	const struct ms912x_payload_check *checks)
{
	assert_pending_unlocked_scan();
	++apply_calls;
	if (invalidate_after_commit) ms912x_damage_scan_invalidate(ms912x);
}

static void ms912x_pending_add_rect_locked(struct ms912x_device *ms912x,
	const struct drm_rect *rect)
{
	assert_pending_unlocked_scan();
	++ms912x->pending_rect_count;
	ms912x->pending_valid = true;
	++pending_calls;
}

static void ms912x_pending_compact_locked(struct ms912x_device *ms912x)
{
	assert_pending_unlocked_scan();
}

static void ms912x_pending_update_deadline_locked(struct ms912x_device *ms912x,
	bool had_pending)
{
	assert_pending_unlocked_scan();
}

static void ms912x_schedule_conversion_locked(struct ms912x_device *ms912x)
{
	assert_pending_unlocked_scan();
	++schedule_calls;
}

static void ms912x_transfer_update_pacing_locked(struct ms912x_device *ms912x)
{
	assert_pending_unlocked_scan();
}

static u64 ms912x_rects_payload_bytes(const struct drm_rect *rects,
	unsigned int count)
{
	return count * 32;
}

static void wake_up_all(int *wait)
{
	if (wait == &device.frame_present_wait) ++present_wakes;
}

/* Color arithmetic has independent fixtures; pair packing identifies buffer ownership. */
static int ms912x_convert_snapshot(struct ms912x_device *ms912x, u32 *dst,
                                   const u32 *src, const struct drm_rect *rect, u32 format)
{
 assert(format == DRM_FORMAT_XRGB8888 || format == DRM_FORMAT_XBGR8888);
 assert_compute_unlocked();
 converted_format = format;
 ++convert_calls;
 pause_at(CONVERT);
 for (int y = rect->y1; y < rect->y2; y++)
  for (int x = rect->x1; x < rect->x2; x += 2)
   dst[y * (MS912X_MAX_WIDTH / 2) + x / 2] = src[y * MS912X_MAX_WIDTH + x];
 return 0;
}

#include "scan-worker.h"
#include "scan-mode.h"

static void reset_fixture(void)
{
	assert(!owned_locks);
	free(device.scan_damage_rects);
	memset(&device, 0, sizeof(device));
	memset(&index_state, 0, sizeof(index_state));
	memset(buffers, 0xa5, sizeof(buffers));
	device.drm.alive = true;
	device.scan_rect_index = &index_state;
	device.damage_scan_snapshot = buffers[0];
	device.damage_scan_snapshot_back = buffers[1];
	device.scan_frames[0].pixels = buffers[2];
	device.primary_plane_format = DRM_FORMAT_XRGB8888;
	device.latest_pixels = buffers[3];
	device.scan_pixels = buffers[4];
	device.damage_scan_valid = true;
	device.damage_scan_width = device.damage_scan_height = 4;
	device.damage_scan_stride_default = 1;
	device.frame_generation = 7;
	device.frame_sequence = 41;
	device.framebuffer_width = device.video_in_width = 4;
	device.framebuffer_height = device.video_in_height = 4;
	device.frame_snapshot_width = device.frame_snapshot_height = 4;
	device.published_frame_sequence = 40;
	device.scan_frames[0].width = device.scan_frames[0].height = 4;
	device.scan_frames[0].generation = 7;
	device.scan_frames[0].sequence = 41;
	device.scan_frames[0].queued = true;
	device.scan_work_scheduled = true;
	memset(&ms912x_parameters, 0, sizeof(ms912x_parameters));
	ms912x_parameters.damage_scan_method = MS912X_DAMAGE_SCAN_SCALAR;
	ms912x_parameters.damage_scan_stride = 1;
	effective_tolerance = 0;
	changed = invalidate_after_commit = test_kvrealloc_fail = false;
	row_calls = full_calls = incremental_calls = 0;
	compare_calls = apply_calls = pending_calls = schedule_calls = 0;
	convert_calls = converted_format = 0;
	test_drm_exit_count = present_wakes = 0;
	test_stats_enabled = false;
	pause_stage = NONE;
	paused = resume_worker = false;
	for (unsigned int y = 0; y < 4; y++)
		for (unsigned int x = 0; x < 4; x++) {
			if (!(x & 1)) buffers[0][y * (MS912X_MAX_WIDTH / 2) + x / 2] = 0x112200 + y * 4 + x;
			buffers[2][y * MS912X_MAX_WIDTH + x] = 0x123400 + y * 4 + x;
		}
	++scenarios;
}

static int compute(struct ms912x_damage_scan_result *result, bool force_full)
{
	unsigned int count;

	ms912x_convert_snapshot(&device, device.scan_pixels, device.scan_frames[0].pixels,
		&(struct drm_rect){0, 0, 4, 4}, device.primary_plane_format);
	return ms912x_damage_scan_find_snapshot_changes(&device,
		device.scan_pixels, 41, 7,
		device.damage_scan_snapshot, device.damage_scan_snapshot_back,
		4, 4, force_full, result, &device.scan_damage_rects,
		&device.scan_damage_rect_capacity, &count);
}

static void check_publication(bool expected)
{
	assert(!owned_locks && !device.scan_frames[0].running);
	assert(!device.scan_frames[0].queued && !device.scan_work_scheduled);
	assert(device.published_frame_sequence == (expected ? 41 : 40));
	assert(device.latest_pixels == buffers[expected ? 4 : 3]);
	assert(device.damage_scan_snapshot == buffers[0]);
	assert(device.damage_scan_snapshot_back == buffers[1]);
	assert(device.scan_frames[0].pixels == buffers[2]);
	assert(apply_calls == (unsigned int)expected);
	if (expected) for (unsigned int y = 0; y < 4; y++)
		for (unsigned int x = 0; x < 4; x += 2)
			assert(device.damage_scan_snapshot[y * (MS912X_MAX_WIDTH / 2) + x / 2] ==
			       (row_calls || full_calls ? 0x123400U : 0x112200U) + y * 4 + x);
}

static void entry_cases(void)
{
	struct ms912x_damage_scan_result result;
	unsigned int count = 77;

	reset_fixture();
	ms912x_parameters.damage_scan = MS912X_DAMAGE_SCAN_AUTO;
	ms912x_parameters.damage_scan_override = true;
	assert(!ms912x_software_scan_enabled(&device, 1, false));
	ms912x_parameters.damage_scan_generation++;
	assert(ms912x_software_scan_enabled(&device, 1, false));
	assert(device.damage_scan_invalidation_epoch == 1 && device.damage_scan_invalid);
	ms912x_parameters.damage_scan_config_generation++;
	assert(ms912x_software_scan_enabled(&device, 1, false));
	assert(device.damage_scan_invalidation_epoch == 2 && device.damage_scan_invalid);
	assert(ms912x_software_scan_enabled(&device, 1, false));
	assert(device.damage_scan_invalidation_epoch == 2);

	reset_fixture();
	assert(ms912x_damage_scan_find_snapshot_changes(&device, buffers[2], 41,
		7, buffers[0], buffers[1], MS912X_MAX_WIDTH + 1, 4, false,
		&result, &device.scan_damage_rects, &device.scan_damage_rect_capacity,
		&count) == -EINVAL);
	assert(count == 77 && !index_state.reset_height);

	reset_fixture();
	ms912x_damage_scan_invalidate(&device);
	test_stats_enabled = true;
	assert(compute(&result, false) == 0);
	assert(full_calls == 1 && !row_calls);
	assert(device.damage_scan_invalid && !device.damage_scan_valid);
	assert(device.damage_scan_snapshot == buffers[0]);
	assert(device.damage_scan_snapshot_back == buffers[1]);
	assert(buffers[1][4] == 0xa5a5a5a5U);
	assert(device.stats.software_scan_frames == 1 &&
	       device.stats.software_scan_frame_pixels == 16 &&
	       device.stats.software_scan_bbox_pixels == 16 &&
	       device.stats.software_scan_rectangles == 1);
	assert(result.invalidation_epoch == 1);
	assert(index_state.reset_height == 4 && device.scan_damage_rect_capacity);

	reset_fixture();
	test_kvrealloc_fail = true;
	assert(compute(&result, true) == -ENOMEM);
	assert(!device.scan_damage_rects && !device.scan_damage_rect_capacity);
	assert(!owned_locks);

	reset_fixture();
	ms912x_parameters.damage_scan_config_generation = 4;
	assert(compute(&result, false) == 0 && full_calls == 1);
	assert(device.damage_scan_config_generation == 4);
	assert(device.damage_scan_invalid && result.config_generation == 4);

	for (unsigned int stride = 1; stride <= 2; stride++)
		for (unsigned int tolerance = 0; tolerance <= 1; tolerance++)
			for (unsigned int dirty = 0; dirty <= 1; dirty++) {
				reset_fixture();
				device.damage_scan_stride_default = stride;
				effective_tolerance = tolerance;
				changed = dirty;
				test_stats_enabled = true;
				ms912x_scan_frame_worker(&device.scan_work);
				check_publication(true);
				assert(!device.damage_scan_invalid && device.damage_scan_valid);
				assert(row_calls == 1 && incremental_calls == 1 && !full_calls);
				assert(device.stats.software_scan_compared_pixels == 16 / stride);
				assert(pending_calls == dirty && schedule_calls == dirty);
			}

	reset_fixture();
	device.scan_frames[0].force_full = true;
	ms912x_scan_frame_worker(&device.scan_work);
	check_publication(true);
	assert(full_calls == 1 && pending_calls == 1 && device.pending_force_full);

	reset_fixture();
	device.primary_plane_format = DRM_FORMAT_XBGR8888;
	ms912x_scan_frame_worker(&device.scan_work);
	check_publication(true);
	assert(convert_calls == 1 && converted_format == DRM_FORMAT_XBGR8888);

	reset_fixture();
	device.primary_plane_format = DRM_FORMAT_XBGR8888;
	device.frame_generation++;
	ms912x_scan_frame_worker(&device.scan_work);
	check_publication(false);
	assert(!convert_calls); /* The old queued frame must never use the new format. */

	reset_fixture();
	device.scan_frames[0].force_full = true;
	test_kvrealloc_fail = true;
	ms912x_scan_frame_worker(&device.scan_work);
	check_publication(false);
	assert(device.failed_present_sequence == 41 && device.damage_scan_invalid);

	reset_fixture();
	changed = invalidate_after_commit = true;
	ms912x_scan_frame_worker(&device.scan_work);
	check_publication(true);
	assert(device.damage_scan_invalid && !device.damage_scan_valid);
	assert(pending_calls == 1 && device.pending_valid);
}

enum action {
	INVALIDATE, REPEAT_INVALIDATE, CONFIG, STOP, GENERATION, FORMAT, DIRECT, DEBUGFS,
	CURSOR, LATER_CURSOR,
};

static void *run_worker(void *unused)
{
	ms912x_scan_frame_worker(&device.scan_work);
	assert(!owned_locks);
	return NULL;
}

static void concurrent_cases(void)
{
	for (unsigned int dirty = 0; dirty <= 1; dirty++)
	for (enum stage stage = FULL_COPY; stage <= COMPARE; stage++)
		for (enum action action = INVALIDATE; action <= LATER_CURSOR; action++) {
			pthread_t worker;
			bool accepted = action == DIRECT || action == DEBUGFS;

			reset_fixture();
			changed = dirty;
			++threaded_cases;
			pause_stage = stage;
			if (stage == FULL_COPY) {
				device.scan_frames[0].force_full = true;
				ms912x_damage_scan_invalidate(&device);
			}
			if (stage == SAMPLED_ROWS) device.damage_scan_stride_default = 2;
			assert(!pthread_create(&worker, NULL, run_worker, NULL));
			assert(!pthread_mutex_lock(&rendezvous));
			while (!paused) assert(!pthread_cond_wait(&condition, &rendezvous));
			assert(!pthread_mutex_unlock(&rendezvous));

			if (action == INVALIDATE || action == REPEAT_INVALIDATE) {
				ms912x_damage_scan_invalidate(&device);
				if (action == REPEAT_INVALIDATE) ms912x_damage_scan_invalidate(&device);
			} else if (action == CONFIG) {
				/* Parameters can change without a subsequent KMS commit. */
				atomic_set(&ms912x_parameters.damage_scan_config_generation, 1);
			} else if (action == STOP || action == GENERATION) {
				mutex_lock(&device.transfer_lock);
				if (action == STOP) device.transfer_stopping = true;
				else device.frame_generation++;
				mutex_unlock(&device.transfer_lock);
			} else if (action == FORMAT || action == DIRECT) {
				const struct drm_format_info format = { .format = DRM_FORMAT_XRGB8888 };
				const struct drm_format_info rgbx = { .format = DRM_FORMAT_XBGR8888 };
				struct drm_framebuffer fb = {
					.width = 4, .height = 4,
					.format = action == FORMAT ? &rgbx : &format,
				};
				struct ms912x_framebuffer_submit submit = { .ms912x = &device, .fb = &fb };

				mutex_lock(&device.submit_lock);
				mutex_lock(&device.transfer_lock);
				ms912x_submit_begin_frame_locked(&submit);
				assert(submit.software_scan && device.damage_scan_snapshot == buffers[0]);
				assert(submit.new_epoch == (action == FORMAT));
				mutex_unlock(&device.transfer_lock);
				mutex_unlock(&device.submit_lock);
			} else if (action == DEBUGFS) {
				mutex_lock(&device.damage_scan_lock);
				/* Capacity stays at the last published value during growth. */
				assert(device.scan_damage_rect_capacity == (stage == COMPARE && dirty ? 512U : 0));
				mutex_unlock(&device.damage_scan_lock);
			} else {
				ms912x_damage_scan_invalidate(&device);
				mutex_lock(&device.transfer_lock);
				device.cursor_commit_pending = true;
				device.cursor_commit_sequence = action == CURSOR ? 41 : 42;
				mutex_unlock(&device.transfer_lock);
			}
			assert(!pthread_mutex_lock(&rendezvous));
			resume_worker = true;
			assert(!pthread_cond_broadcast(&condition));
			assert(!pthread_mutex_unlock(&rendezvous));
			assert(!pthread_join(worker, NULL));
			check_publication(accepted);
			if (!accepted) {
				assert(device.damage_scan_invalid && !device.damage_scan_valid);
				assert(!pending_calls && schedule_calls == (action == CURSOR ? 1U : 0));
				assert(device.failed_present_sequence == 41 && present_wakes);
				assert(device.cursor_commit_pending == (action == LATER_CURSOR));
				/* Restart/retry must initialize a full baseline after rejection. */
				device.transfer_stopping = false;
				device.scan_frames[0].generation = device.frame_generation;
				device.scan_frames[0].queued = true;
				device.scan_work_scheduled = true;
				pause_stage = NONE;
				ms912x_scan_frame_worker(&device.scan_work);
				check_publication(true);
				assert(full_calls && !device.damage_scan_invalid && device.damage_scan_valid);
				assert(converted_format == device.primary_plane_format);
			}
		}
}

static void format_during_conversion_case(void)
{
	pthread_t worker;
	const struct drm_format_info format = { .format = DRM_FORMAT_XBGR8888 };
	struct drm_framebuffer fb = { .width = 4, .height = 4, .format = &format };
	struct ms912x_framebuffer_submit submit = { .ms912x = &device, .fb = &fb };

	reset_fixture();
	pause_stage = CONVERT;
	assert(!pthread_create(&worker, NULL, run_worker, NULL));
	assert(!pthread_mutex_lock(&rendezvous));
	while (!paused) assert(!pthread_cond_wait(&condition, &rendezvous));
	assert(!pthread_mutex_unlock(&rendezvous));
	mutex_lock(&device.submit_lock);
	mutex_lock(&device.transfer_lock);
	ms912x_submit_begin_frame_locked(&submit);
	mutex_unlock(&device.transfer_lock);
	mutex_unlock(&device.submit_lock);
	assert(!pthread_mutex_lock(&rendezvous));
	resume_worker = true;
	assert(!pthread_cond_broadcast(&condition));
	assert(!pthread_mutex_unlock(&rendezvous));
	assert(!pthread_join(worker, NULL));
	check_publication(false);
	assert(convert_calls == 1 && converted_format == DRM_FORMAT_XRGB8888);
	assert(device.damage_scan_invalid && device.primary_plane_format == DRM_FORMAT_XBGR8888);
}

int main(void)
{
	entry_cases();
	concurrent_cases();
	format_during_conversion_case();
	free(device.scan_damage_rects);
	printf("PASS damage_scan: %u entry/worker scenarios, including %u threaded ownership, invalidation, config and publication cases\n", scenarios, threaded_cases);
	return 0;
}
