#include "frame_pipeline_internal.h"
#include "ms912x_pacing_core.h"
#include "../../../../../ms912x_batch.h"
#include "../../../../../ms912x_damage_policy.h"

#include <errno.h>
#include <limits.h>
#include <pthread.h>

/* A paced candidate stays pinned and encoded until it is submitted or replaced. */
void ms912x_frame_pipeline_cancel_coalesced_locked(struct ms912x_android_frame_pipeline *pipeline,
			     bool retain_latest)
{
	unsigned int slot = pipeline->coalesced_slot;
	unsigned int i;
	bool newer = false;

	if (slot >= MS912X_ANDROID_FRAME_SLOTS)
		return;
	ms912x_frame_damage_cancel(pipeline->damage, pipeline->coalesced_sequence);
	pthread_mutex_lock(&pipeline->lock);
	for (i = 0; i < MS912X_ANDROID_FRAME_SLOTS; ++i)
		if (i != slot && pipeline->slots[i].state == SLOT_PENDING)
			newer = true;
	if (pipeline->slots[slot].state == SLOT_ACTIVE)
		pipeline->slots[slot].state = retain_latest && !newer
			? SLOT_PENDING : SLOT_FREE;
	pthread_mutex_unlock(&pipeline->lock);
	pipeline->coalesced_slot = MS912X_ANDROID_FRAME_SLOTS;
	pipeline->coalesced_sequence = 0;
	pipeline->coalesced_length = 0;
	pipeline->coalesced_destination = NULL;
}

static int encode_latest(struct ms912x_android_frame_pipeline *pipeline,
	uint8_t *destination, size_t destination_capacity, struct ms912x_android_frame_info *info,
    uint64_t now_ns, unsigned int refresh, uint64_t *wait_ns)
{
	const uint8_t *pixels;
	unsigned int slot = MS912X_ANDROID_FRAME_SLOTS;
	uint64_t sequence;
	int result;

	if (!pipeline || !destination || !info)
		return -1;
	pthread_mutex_lock(&pipeline->consumer_lock);
	if (pipeline->submission_pending) {
		pthread_mutex_unlock(&pipeline->consumer_lock);
		return -EBUSY;
	}
	if (pipeline->coalesced_slot < MS912X_ANDROID_FRAME_SLOTS) {
		unsigned int i;
		bool newer = false;
		unsigned int slot = pipeline->coalesced_slot;

		pthread_mutex_lock(&pipeline->lock);
		for (i = 0; i < MS912X_ANDROID_FRAME_SLOTS; ++i)
			if (pipeline->slots[i].state == SLOT_PENDING)
				newer = true;
		pthread_mutex_unlock(&pipeline->lock);
		if (!newer && destination == pipeline->coalesced_destination && wait_ns
		    && pipeline->coalesced_length <= destination_capacity) {
			size_t cached_length = pipeline->coalesced_length;
			if (now_ns < pipeline->coalesce_deadline) {
				*wait_ns = pipeline->coalesce_deadline - now_ns;
				pthread_mutex_unlock(&pipeline->consumer_lock);
				return 0;
			}
			info->width = pipeline->width;
			info->height = pipeline->height;
			info->generation = pipeline->slots[slot].generation;
			info->sequence = pipeline->coalesced_sequence;
			info->stride_bytes = (size_t)pipeline->width * 2U;
			info->pixel_format = MS912X_ANDROID_PIXEL_UYVY422;
			pipeline->next_submission_sequence = pipeline->coalesced_sequence;
			pipeline->submission_pending = true;
			pipeline->submitted_slot = slot;
			pipeline->coalesced_slot = MS912X_ANDROID_FRAME_SLOTS;
			pipeline->coalesced_sequence = 0;
			pipeline->coalesced_length = 0;
			pipeline->coalesced_destination = NULL;
			pipeline->coalesce_deadline = 0;
			pthread_mutex_lock(&pipeline->lock);
			pipeline->slots[slot].state = SLOT_SUBMITTED;
			pthread_mutex_unlock(&pipeline->lock);
			result = (int)cached_length;
			pthread_mutex_unlock(&pipeline->consumer_lock);
			return result;
		}
		ms912x_frame_pipeline_cancel_coalesced_locked(pipeline, true);
	}
	sequence = pipeline->next_submission_sequence + 1;
	if (!sequence) {
		pthread_mutex_unlock(&pipeline->consumer_lock);
		return -EOVERFLOW;
	}
	{
		uint64_t low, high;
		unsigned int tolerance;

		ms912x_pacing_watermarks(refresh, &low, &high);
		tolerance = ms912x_damage_scan_auto_tolerance(
			ms912x_pacing_pressure(pipeline->last_packet_bytes, pipeline->usb_rate), low, high,
			pipeline->scan_options.tolerance_min, pipeline->scan_options.tolerance_max);
		ms912x_frame_damage_set_scan_options(pipeline->damage, pipeline->scan_options.stride,
						    tolerance);
	}
	result = ms912x_android_frame_pipeline_acquire(pipeline, &slot, info, &pixels);
	if (result == 1) {
		*info = (struct ms912x_android_frame_info) {
			.width = pipeline->width, .height = pipeline->height,
			.generation = pipeline->generation,
			.stride_bytes = (size_t)pipeline->width * 2U,
			.pixel_format = MS912X_ANDROID_PIXEL_UYVY422,
		};
		result = ms912x_frame_damage_prepare_replay(pipeline->damage, destination,
							  destination_capacity, sequence);
	} else if (!result) {
		result = ms912x_frame_damage_prepare(pipeline->damage, destination,
						   destination_capacity, pixels, sequence);
	} else {
		pthread_mutex_unlock(&pipeline->consumer_lock);
		return result;
	}
	if (result > 0) pipeline->last_packet_bytes = (size_t)result;
    if (wait_ns) {
        *wait_ns = 0;
        if (result > 0 && slot < MS912X_ANDROID_FRAME_SLOTS) {
            uint64_t low, high, pressure, delay;
            ms912x_pacing_watermarks(refresh, &low, &high);
            pressure = ms912x_pacing_pressure((uint64_t)result, pipeline->usb_rate);
            pipeline->backpressured = ms912x_batch_backpressured(pressure, low, high,
                                                               pipeline->backpressured);
            delay = ms912x_batch_coalesce_ns(result, 1000000000ULL / (2ULL * refresh),
                pressure, high, pipeline->backpressured,
                ms912x_frame_damage_pending_full(pipeline->damage));
            if (delay && !pipeline->coalesce_deadline) pipeline->coalesce_deadline = now_ns + delay;
            if (delay && now_ns < pipeline->coalesce_deadline) {
                *wait_ns = pipeline->coalesce_deadline - now_ns;
				pipeline->coalesced_slot = slot;
				pipeline->coalesced_sequence = sequence;
				pipeline->coalesced_length = (size_t)result;
				pipeline->coalesced_destination = destination;
                pthread_mutex_unlock(&pipeline->consumer_lock);
                return 0;
            }
        }
        /* A producer may be filling the only pending slot. Empty polls and replay
         * must not renew that snapshot's original coalescing deadline. */
        if (slot < MS912X_ANDROID_FRAME_SLOTS) pipeline->coalesce_deadline = 0;
    }
	if (result > 0) {
		info->sequence = sequence;
		pipeline->next_submission_sequence = sequence;
		pipeline->submission_pending = true;
		pthread_mutex_lock(&pipeline->lock);
		if (slot < MS912X_ANDROID_FRAME_SLOTS) pipeline->slots[slot].state = SLOT_SUBMITTED;
		pipeline->submitted_slot = slot;
		pthread_mutex_unlock(&pipeline->lock);
	} else {
		ms912x_android_frame_pipeline_release(pipeline, slot);
	}
	pthread_mutex_unlock(&pipeline->consumer_lock);
	return result;
}

int ms912x_android_frame_pipeline_encode_latest(struct ms912x_android_frame_pipeline *pipeline,
    uint8_t *destination, size_t capacity, struct ms912x_android_frame_info *info)
{
    return encode_latest(pipeline, destination, capacity, info, 0, 0, NULL);
}

int ms912x_android_frame_pipeline_encode_paced(struct ms912x_android_frame_pipeline *pipeline,
    uint8_t *destination, size_t capacity, struct ms912x_android_frame_info *info,
    uint64_t now_ns, unsigned int refresh, uint64_t *wait_ns)
{
    if (!wait_ns || !refresh || refresh > 1000 || now_ns > INT64_MAX) return -EINVAL;
    *wait_ns = 0;
    return encode_latest(pipeline, destination, capacity, info, now_ns, refresh, wait_ns);
}

void ms912x_android_frame_pipeline_record_usb(struct ms912x_android_frame_pipeline *pipeline,
    size_t bytes, uint64_t duration_ns)
{
    if (!pipeline || !bytes || bytes > MS912X_ANDROID_MAX_FRAME_BYTES || !duration_ns) return;
    pthread_mutex_lock(&pipeline->consumer_lock);
    ms912x_usb_rate_update(&pipeline->usb_rate, &pipeline->usb_samples,
                          ms912x_pacing_div((u64)bytes * 1000000000ULL, duration_ns));
    pthread_mutex_unlock(&pipeline->consumer_lock);
}

int ms912x_android_frame_pipeline_complete(struct ms912x_android_frame_pipeline *pipeline,
	uint64_t sequence, int transferred)
{
	int result;

	if (!pipeline)
		return -EINVAL;
	pthread_mutex_lock(&pipeline->consumer_lock);
	result = ms912x_frame_damage_finish(pipeline->damage, sequence, transferred != 0);
	if (!result) {
		pthread_mutex_lock(&pipeline->lock);
		if (pipeline->submitted_slot < MS912X_ANDROID_FRAME_SLOTS) {
			struct frame_slot *slot = &pipeline->slots[pipeline->submitted_slot];
			slot->state = pipeline->rescan_after_submission &&
				slot->sequence == pipeline->published_sequence ? SLOT_PENDING : SLOT_FREE;
		}
		pipeline->rescan_after_submission = false;
		pipeline->submitted_slot = MS912X_ANDROID_FRAME_SLOTS;
		pipeline->submission_pending = false;
		pthread_mutex_unlock(&pipeline->lock);
	}
	pthread_mutex_unlock(&pipeline->consumer_lock);
	return result;
}

void ms912x_android_frame_pipeline_invalidate_baseline(
	struct ms912x_android_frame_pipeline *pipeline)
{
	if (!pipeline)
		return;
	pthread_mutex_lock(&pipeline->consumer_lock);
	ms912x_frame_pipeline_cancel_coalesced_locked(pipeline, true);
	ms912x_frame_damage_invalidate(pipeline->damage);
    pipeline->coalesce_deadline = 0;
	pthread_mutex_unlock(&pipeline->consumer_lock);
}

void ms912x_android_frame_pipeline_set_show_draws(
	struct ms912x_android_frame_pipeline *pipeline, int enabled)
{
	if (!pipeline) return;
	pthread_mutex_lock(&pipeline->consumer_lock);
	ms912x_frame_damage_set_show_draws(pipeline->damage, enabled != 0);
	pthread_mutex_unlock(&pipeline->consumer_lock);
}

