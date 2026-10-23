#include "frame_pipeline.h"
#include "ms912x_frame_damage.h"
#include "ms912x_rgbx_convert.h"

#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>

enum slot_state {
	SLOT_FREE,
	SLOT_FILLING,
	SLOT_PENDING,
	SLOT_ACTIVE,
	SLOT_SUBMITTED,
};

struct frame_slot {
	uint8_t *pixels;
	enum slot_state state;
	uint64_t sequence;
	uint64_t generation;
};

/* lock protects the mailbox; consumer_lock serializes damage/submission state.
 * When both are needed, acquire consumer_lock first. Conversion holds neither.
 */
struct ms912x_android_frame_pipeline {
	pthread_mutex_t lock;
	pthread_mutex_t consumer_lock;
	struct frame_slot slots[MS912X_ANDROID_FRAME_SLOTS];
	struct ms912x_frame_damage *damage;
	unsigned int submitted_slot;
	bool submission_pending;
	uint64_t next_submission_sequence;
	bool reconfiguring;
	uint32_t width, height;
	uint64_t generation;
	uint64_t next_sequence;
	uint64_t published_sequence;
	size_t frame_bytes;
	enum ms912x_color_backend backend;
};

static bool frame_size(uint32_t width, uint32_t height, size_t *bytes)
{
	if (!width || !height || width > 1920 || height > 1200 || (width & 1U))
		return false;
	*bytes = (size_t)width * height * 2U;
	return *bytes <= MS912X_ANDROID_MAX_FRAME_BYTES / MS912X_ANDROID_FRAME_SLOTS;
}

static void free_buffers(uint8_t *buffers[MS912X_ANDROID_FRAME_SLOTS])
{
	unsigned int i;

	for (i = 0; i < MS912X_ANDROID_FRAME_SLOTS; ++i)
		free(buffers[i]);
}

static bool allocate_buffers(uint8_t *buffers[MS912X_ANDROID_FRAME_SLOTS], size_t bytes)
{
	unsigned int i;

	for (i = 0; i < MS912X_ANDROID_FRAME_SLOTS; ++i) {
		buffers[i] = malloc(bytes);
		if (!buffers[i])
			return false;
	}
	return true;
}

struct ms912x_android_frame_pipeline *
ms912x_android_frame_pipeline_create(uint32_t width, uint32_t height, uint64_t generation)
{
	struct ms912x_android_frame_pipeline *pipeline;
	uint8_t *buffers[MS912X_ANDROID_FRAME_SLOTS] = { 0 };
	size_t bytes;
	unsigned int i;

	if (!frame_size(width, height, &bytes) || !generation)
		return NULL;
	pipeline = calloc(1, sizeof(*pipeline));
	if (!pipeline)
		return NULL;
	if (pthread_mutex_init(&pipeline->lock, NULL)) {
		free(pipeline);
		return NULL;
	}
	if (pthread_mutex_init(&pipeline->consumer_lock, NULL)) {
		pthread_mutex_destroy(&pipeline->lock);
		free(pipeline);
		return NULL;
	}
	pipeline->damage = ms912x_frame_damage_create(width, height);
	if (!pipeline->damage || !allocate_buffers(buffers, bytes)) {
		free_buffers(buffers);
		ms912x_frame_damage_destroy(pipeline->damage);
		pthread_mutex_destroy(&pipeline->consumer_lock);
		pthread_mutex_destroy(&pipeline->lock);
		free(pipeline);
		return NULL;
	}
	for (i = 0; i < MS912X_ANDROID_FRAME_SLOTS; ++i)
		pipeline->slots[i].pixels = buffers[i];
	pipeline->width = width;
	pipeline->height = height;
	pipeline->generation = generation;
	pipeline->frame_bytes = bytes;
	pipeline->submitted_slot = MS912X_ANDROID_FRAME_SLOTS;
	pipeline->backend = ms912x_rgbx_default_backend();
	return pipeline;
}

void ms912x_android_frame_pipeline_destroy(struct ms912x_android_frame_pipeline *pipeline)
{
	unsigned int i;

	if (!pipeline)
		return;
	for (i = 0; i < MS912X_ANDROID_FRAME_SLOTS; ++i)
		free(pipeline->slots[i].pixels);
	ms912x_frame_damage_destroy(pipeline->damage);
	pthread_mutex_destroy(&pipeline->consumer_lock);
	pthread_mutex_destroy(&pipeline->lock);
	free(pipeline);
}

int ms912x_android_frame_pipeline_reconfigure(struct ms912x_android_frame_pipeline *pipeline,
	uint32_t width, uint32_t height, uint64_t generation)
{
	uint8_t *buffers[MS912X_ANDROID_FRAME_SLOTS] = { 0 };
	struct ms912x_frame_damage *replacement;
	size_t bytes;
	unsigned int i;
	int result = 0;

	if (!pipeline || !frame_size(width, height, &bytes) || !generation)
		return -1;
	pthread_mutex_lock(&pipeline->consumer_lock);
	pthread_mutex_lock(&pipeline->lock);
	if (pipeline->submission_pending) {
		result = -2;
	} else if (generation <= pipeline->generation) {
		result = -1;
	} else {
		for (i = 0; i < MS912X_ANDROID_FRAME_SLOTS; ++i) {
			enum slot_state state = pipeline->slots[i].state;

			if (state == SLOT_FILLING || state == SLOT_ACTIVE || state == SLOT_SUBMITTED)
				result = -2;
		}
	}
	if (result) {
		pthread_mutex_unlock(&pipeline->lock);
		pthread_mutex_unlock(&pipeline->consumer_lock);
		return result;
	}
	pipeline->reconfiguring = true;
	pthread_mutex_unlock(&pipeline->lock);
	replacement = ms912x_frame_damage_create(width, height);
	if (!replacement || !allocate_buffers(buffers, bytes)) {
		free_buffers(buffers);
		ms912x_frame_damage_destroy(replacement);
		result = -3;
	}
	pthread_mutex_lock(&pipeline->lock);
	if (!result) {
		for (i = 0; i < MS912X_ANDROID_FRAME_SLOTS; ++i) {
			free(pipeline->slots[i].pixels);
			pipeline->slots[i] = (struct frame_slot){ .pixels = buffers[i] };
		}
		ms912x_frame_damage_destroy(pipeline->damage);
		pipeline->damage = replacement;
		pipeline->width = width;
		pipeline->height = height;
		pipeline->generation = generation;
		pipeline->frame_bytes = bytes;
	}
	pipeline->reconfiguring = false;
	pthread_mutex_unlock(&pipeline->lock);
	pthread_mutex_unlock(&pipeline->consumer_lock);
	return result;
}

/* Reserve under the mailbox lock; the producer owns the chosen buffer until publish. */
static unsigned int reserve_slot(struct ms912x_android_frame_pipeline *pipeline)
{
	unsigned int chosen = MS912X_ANDROID_FRAME_SLOTS, i;
	uint64_t oldest = UINT64_MAX;

	for (i = 0; i < MS912X_ANDROID_FRAME_SLOTS; ++i) {
		if (pipeline->slots[i].state == SLOT_PENDING &&
		    pipeline->slots[i].sequence < oldest) {
			oldest = pipeline->slots[i].sequence;
			chosen = i;
		}
	}
	if (chosen == MS912X_ANDROID_FRAME_SLOTS) {
		for (i = 0; i < MS912X_ANDROID_FRAME_SLOTS; ++i) {
			if (pipeline->slots[i].state == SLOT_FREE) {
				chosen = i;
				break;
			}
		}
	}
	if (chosen < MS912X_ANDROID_FRAME_SLOTS) {
		pipeline->slots[chosen].state = SLOT_FILLING;
		pipeline->slots[chosen].sequence = ++pipeline->next_sequence;
	}
	return chosen;
}

int ms912x_android_frame_pipeline_offer_rgba(struct ms912x_android_frame_pipeline *pipeline,
	const uint8_t *data, size_t data_size, size_t base_offset,
	size_t row_stride, size_t pixel_stride, uint32_t crop_x, uint32_t crop_y,
	uint32_t width, uint32_t height, uint64_t generation)
{
	struct ms912x_rgbx_view source = {
		.data = data, .bytes = data_size, .offset = base_offset,
		.row_stride = row_stride, .pixel_stride = pixel_stride,
		.crop_x = crop_x, .crop_y = crop_y, .width = width, .height = height,
		.order = MS912X_RGBX,
	};
	struct ms912x_rgbx_plan conversion;
	unsigned int chosen, i;
	size_t bytes;
	int result;

	if (!pipeline || ms912x_rgbx_prepare(&conversion, &source, MS912X_COLOR_AUTO))
		return -1;
	pthread_mutex_lock(&pipeline->lock);
	if (pipeline->reconfiguring) {
		pthread_mutex_unlock(&pipeline->lock);
		return -3;
	}
	if (width != pipeline->width || height != pipeline->height ||
	    generation != pipeline->generation) {
		pthread_mutex_unlock(&pipeline->lock);
		return -2;
	}
	if (pipeline->next_sequence == UINT64_MAX) {
		pthread_mutex_unlock(&pipeline->lock);
		return -EOVERFLOW;
	}
	chosen = reserve_slot(pipeline);
	bytes = pipeline->frame_bytes;
	pthread_mutex_unlock(&pipeline->lock);
	if (chosen == MS912X_ANDROID_FRAME_SLOTS)
		return 1;
	result = ms912x_rgbx_execute(&conversion, pipeline->slots[chosen].pixels,
				    bytes, (size_t)width * 2U);
	pthread_mutex_lock(&pipeline->lock);
	if (result) {
		pipeline->slots[chosen].state = SLOT_FREE;
		result = -1;
	} else if (pipeline->slots[chosen].sequence < pipeline->published_sequence) {
		pipeline->slots[chosen].state = SLOT_FREE;
		result = 1; /* A newer producer finished first; never publish the older frame. */
	} else {
		for (i = 0; i < MS912X_ANDROID_FRAME_SLOTS; ++i) {
			if (i != chosen && pipeline->slots[i].state == SLOT_PENDING)
				pipeline->slots[i].state = SLOT_FREE;
		}
		pipeline->slots[chosen].generation = generation;
		pipeline->published_sequence = pipeline->slots[chosen].sequence;
		pipeline->slots[chosen].state = SLOT_PENDING;
		pipeline->backend = conversion.backend;
	}
	pthread_mutex_unlock(&pipeline->lock);
	return result;
}

int ms912x_android_frame_pipeline_acquire(struct ms912x_android_frame_pipeline *pipeline,
	unsigned int *slot_index, struct ms912x_android_frame_info *info, const uint8_t **pixels)
{
	unsigned int newest = MS912X_ANDROID_FRAME_SLOTS, i;

	if (!pipeline || !slot_index || !info || !pixels)
		return -1;
	pthread_mutex_lock(&pipeline->lock);
	if (pipeline->reconfiguring) {
		pthread_mutex_unlock(&pipeline->lock);
		return 1;
	}
	for (i = 0; i < MS912X_ANDROID_FRAME_SLOTS; ++i) {
		if (pipeline->slots[i].state == SLOT_PENDING &&
		    (newest == MS912X_ANDROID_FRAME_SLOTS ||
		     pipeline->slots[i].sequence > pipeline->slots[newest].sequence))
			newest = i;
	}
	if (newest == MS912X_ANDROID_FRAME_SLOTS) {
		pthread_mutex_unlock(&pipeline->lock);
		return 1;
	}
	pipeline->slots[newest].state = SLOT_ACTIVE;
	*slot_index = newest;
	*info = (struct ms912x_android_frame_info) {
		.width = pipeline->width, .height = pipeline->height,
		.generation = pipeline->slots[newest].generation,
		.sequence = pipeline->slots[newest].sequence,
		.stride_bytes = (size_t)pipeline->width * 2U,
		.pixel_format = MS912X_ANDROID_PIXEL_UYVY422,
	};
	*pixels = pipeline->slots[newest].pixels;
	pthread_mutex_unlock(&pipeline->lock);
	return 0;
}

void ms912x_android_frame_pipeline_release(struct ms912x_android_frame_pipeline *pipeline,
	unsigned int slot_index)
{
	if (!pipeline || slot_index >= MS912X_ANDROID_FRAME_SLOTS)
		return;
	pthread_mutex_lock(&pipeline->lock);
	if (pipeline->slots[slot_index].state == SLOT_ACTIVE)
		pipeline->slots[slot_index].state = SLOT_FREE;
	pthread_mutex_unlock(&pipeline->lock);
}

int ms912x_android_frame_pipeline_encode_latest(struct ms912x_android_frame_pipeline *pipeline,
	uint8_t *destination, size_t destination_capacity, struct ms912x_android_frame_info *info)
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
	sequence = pipeline->next_submission_sequence + 1;
	if (!sequence) {
		pthread_mutex_unlock(&pipeline->consumer_lock);
		return -EOVERFLOW;
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
		if (pipeline->submitted_slot < MS912X_ANDROID_FRAME_SLOTS)
			pipeline->slots[pipeline->submitted_slot].state = SLOT_FREE;
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
	ms912x_frame_damage_invalidate(pipeline->damage);
	pthread_mutex_unlock(&pipeline->consumer_lock);
}

const char *ms912x_android_frame_pipeline_backend(struct ms912x_android_frame_pipeline *pipeline)
{
	enum ms912x_color_backend backend;

	pthread_mutex_lock(&pipeline->lock);
	backend = pipeline->backend;
	pthread_mutex_unlock(&pipeline->lock);
	return ms912x_rgbx_backend_name(backend);
}

size_t ms912x_android_frame_pipeline_packet_capacity(struct ms912x_android_frame_pipeline *pipeline)
{
	size_t capacity;

	pthread_mutex_lock(&pipeline->lock);
	capacity = ms912x_frame_damage_packet_capacity(pipeline->width, pipeline->height);
	pthread_mutex_unlock(&pipeline->lock);
	return capacity;
}
