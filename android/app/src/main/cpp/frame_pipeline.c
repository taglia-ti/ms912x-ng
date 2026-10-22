#include "frame_pipeline.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

enum slot_state {
	SLOT_FREE,
	SLOT_FILLING,
	SLOT_PENDING,
	SLOT_ACTIVE,
};

struct frame_slot {
	uint8_t *pixels;
	enum slot_state state;
	uint64_t sequence;
	uint64_t generation;
};

struct ms912x_android_frame_pipeline {
	pthread_mutex_t lock;
	struct frame_slot slots[MS912X_ANDROID_FRAME_SLOTS];
	bool reconfiguring;
	uint32_t width;
	uint32_t height;
	uint64_t generation;
	uint64_t next_sequence;
	size_t frame_bytes;
};

static bool frame_size(uint32_t width, uint32_t height, size_t *bytes)
{
	if (!width || !height || width > 8192 || height > 8192 ||
	    (size_t)width > SIZE_MAX / 4U / (size_t)height)
		return false;

	*bytes = (size_t)width * (size_t)height * 4U;
	return *bytes <= MS912X_ANDROID_MAX_FRAME_BYTES /
		MS912X_ANDROID_FRAME_SLOTS;
}

struct ms912x_android_frame_pipeline *
ms912x_android_frame_pipeline_create(uint32_t width, uint32_t height,
				     uint64_t generation)
{
	struct ms912x_android_frame_pipeline *pipeline;
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

	for (i = 0; i < MS912X_ANDROID_FRAME_SLOTS; ++i) {
		pipeline->slots[i].pixels = malloc(bytes);
		if (!pipeline->slots[i].pixels)
			goto fail;
	}
	pipeline->width = width;
	pipeline->height = height;
	pipeline->frame_bytes = bytes;
	pipeline->generation = generation;
	return pipeline;

fail:
	while (i > 0)
		free(pipeline->slots[--i].pixels);
	pthread_mutex_destroy(&pipeline->lock);
	free(pipeline);
	return NULL;
}

void ms912x_android_frame_pipeline_destroy(
	struct ms912x_android_frame_pipeline *pipeline)
{
	unsigned int i;

	if (!pipeline)
		return;
	for (i = 0; i < MS912X_ANDROID_FRAME_SLOTS; ++i)
		free(pipeline->slots[i].pixels);
	pthread_mutex_destroy(&pipeline->lock);
	free(pipeline);
}

int ms912x_android_frame_pipeline_reconfigure(
	struct ms912x_android_frame_pipeline *pipeline,
	uint32_t width, uint32_t height, uint64_t generation)
{
	size_t bytes;
	uint8_t *replacement[MS912X_ANDROID_FRAME_SLOTS] = { 0 };
	unsigned int i;

	if (!pipeline || !frame_size(width, height, &bytes) || !generation)
		return -1;

	pthread_mutex_lock(&pipeline->lock);
	if (pipeline->reconfiguring) {
		pthread_mutex_unlock(&pipeline->lock);
		return -2;
	}
	if (generation <= pipeline->generation) {
		pthread_mutex_unlock(&pipeline->lock);
		return -1;
	}
	for (i = 0; i < MS912X_ANDROID_FRAME_SLOTS; ++i) {
		if (pipeline->slots[i].state == SLOT_FILLING ||
		    pipeline->slots[i].state == SLOT_ACTIVE) {
			pthread_mutex_unlock(&pipeline->lock);
			return -2;
		}
	}
	pipeline->reconfiguring = true;
	pthread_mutex_unlock(&pipeline->lock);

	for (i = 0; i < MS912X_ANDROID_FRAME_SLOTS; ++i) {
		replacement[i] = malloc(bytes);
		if (!replacement[i])
			goto fail;
	}

	pthread_mutex_lock(&pipeline->lock);
	for (i = 0; i < MS912X_ANDROID_FRAME_SLOTS; ++i) {
		free(pipeline->slots[i].pixels);
		pipeline->slots[i].pixels = replacement[i];
		pipeline->slots[i].state = SLOT_FREE;
		pipeline->slots[i].sequence = 0;
		pipeline->slots[i].generation = generation;
	}
	pipeline->width = width;
	pipeline->height = height;
	pipeline->generation = generation;
	pipeline->frame_bytes = bytes;
	pipeline->reconfiguring = false;
	pthread_mutex_unlock(&pipeline->lock);
	return 0;

fail:
	for (i = 0; i < MS912X_ANDROID_FRAME_SLOTS; ++i)
		free(replacement[i]);
	pthread_mutex_lock(&pipeline->lock);
	pipeline->reconfiguring = false;
	pthread_mutex_unlock(&pipeline->lock);
	return -3;
}

static int validate_source(const uint8_t *data, size_t data_size,
			   size_t base_offset, size_t row_stride,
			   size_t pixel_stride, uint32_t crop_x,
			   uint32_t crop_y, uint32_t width,
			   uint32_t height)
{
	size_t row_offset, pixel_offset, last_offset, row_extent;

	if (!data || !width || !height || !row_stride || pixel_stride < 4U ||
	    crop_x > SIZE_MAX / pixel_stride ||
	    crop_y > SIZE_MAX / row_stride)
		return -1;
	if ((size_t)(width - 1U) > SIZE_MAX / pixel_stride ||
	    __builtin_add_overflow((size_t)crop_x, (size_t)(width - 1U),
			   &row_extent))
		return -1;
	if (row_extent > SIZE_MAX / pixel_stride)
		return -1;
	row_extent *= pixel_stride;
	if (row_extent > SIZE_MAX - 4U || row_extent + 4U > row_stride)
		return -1;
	row_offset = (size_t)crop_y * row_stride;
	pixel_offset = (size_t)crop_x * pixel_stride;
	if (base_offset > SIZE_MAX - row_offset ||
	    base_offset + row_offset > SIZE_MAX - pixel_offset)
		return -1;
	last_offset = base_offset + row_offset + pixel_offset;
	if ((size_t)(height - 1U) > SIZE_MAX / row_stride ||
	    last_offset > SIZE_MAX - (size_t)(height - 1U) * row_stride)
		return -1;
	last_offset += (size_t)(height - 1U) * row_stride;
	if (last_offset > SIZE_MAX - (size_t)(width - 1U) * pixel_stride)
		return -1;
	last_offset += (size_t)(width - 1U) * pixel_stride;
	if (last_offset > SIZE_MAX - 4U || last_offset + 4U > data_size)
		return -1;
	return 0;
}

int ms912x_android_frame_pipeline_offer_rgba(
	struct ms912x_android_frame_pipeline *pipeline,
	const uint8_t *data, size_t data_size, size_t base_offset,
	size_t row_stride, size_t pixel_stride,
	uint32_t crop_x, uint32_t crop_y,
	uint32_t width, uint32_t height, uint64_t generation)
{
	unsigned int chosen = MS912X_ANDROID_FRAME_SLOTS;
	uint64_t oldest = UINT64_MAX;
	unsigned int i;
	uint32_t y, x;

	if (!pipeline || validate_source(data, data_size, base_offset, row_stride,
					 pixel_stride, crop_x, crop_y,
					 width, height))
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
	for (i = 0; i < MS912X_ANDROID_FRAME_SLOTS; ++i) {
		struct frame_slot *slot = &pipeline->slots[i];

		if (slot->state == SLOT_PENDING && slot->sequence < oldest) {
			oldest = slot->sequence;
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
	if (chosen == MS912X_ANDROID_FRAME_SLOTS) {
		pthread_mutex_unlock(&pipeline->lock);
		return 1;
	}
	pipeline->slots[chosen].state = SLOT_FILLING;
	pthread_mutex_unlock(&pipeline->lock);

	for (y = 0; y < height; ++y) {
		const uint8_t *src = data + base_offset +
			((size_t)crop_y + y) * row_stride +
			(size_t)crop_x * pixel_stride;
		uint8_t *dst = pipeline->slots[chosen].pixels +
			(size_t)y * (size_t)width * 4U;

		for (x = 0; x < width; ++x, src += pixel_stride, dst += 4) {
			dst[0] = src[2];
			dst[1] = src[1];
			dst[2] = src[0];
			dst[3] = 0xff;
		}
	}

	pthread_mutex_lock(&pipeline->lock);
	if (pipeline->generation != generation ||
	    pipeline->width != width || pipeline->height != height) {
		pipeline->slots[chosen].state = SLOT_FREE;
		pthread_mutex_unlock(&pipeline->lock);
		return -2;
	}
	for (i = 0; i < MS912X_ANDROID_FRAME_SLOTS; ++i) {
		if (i != chosen && pipeline->slots[i].state == SLOT_PENDING)
			pipeline->slots[i].state = SLOT_FREE;
	}
	pipeline->slots[chosen].generation = generation;
	pipeline->slots[chosen].sequence = ++pipeline->next_sequence;
	pipeline->slots[chosen].state = SLOT_PENDING;
	pthread_mutex_unlock(&pipeline->lock);
	return 0;
}

int ms912x_android_frame_pipeline_acquire(
	struct ms912x_android_frame_pipeline *pipeline,
	unsigned int *slot_index, struct ms912x_android_frame_info *info,
	const uint8_t **pixels)
{
	unsigned int newest = MS912X_ANDROID_FRAME_SLOTS;
	unsigned int i;

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
	for (i = 0; i < MS912X_ANDROID_FRAME_SLOTS; ++i) {
		if (i != newest && pipeline->slots[i].state == SLOT_PENDING)
			pipeline->slots[i].state = SLOT_FREE;
	}
	pipeline->slots[newest].state = SLOT_ACTIVE;
	*slot_index = newest;
	info->width = pipeline->width;
	info->height = pipeline->height;
	info->generation = pipeline->slots[newest].generation;
	info->sequence = pipeline->slots[newest].sequence;
	info->stride_bytes = (size_t)pipeline->width * 4U;
	*pixels = pipeline->slots[newest].pixels;
	pthread_mutex_unlock(&pipeline->lock);
	return 0;
}

void ms912x_android_frame_pipeline_release(
	struct ms912x_android_frame_pipeline *pipeline,
	unsigned int slot_index)
{
	if (!pipeline || slot_index >= MS912X_ANDROID_FRAME_SLOTS)
		return;
	pthread_mutex_lock(&pipeline->lock);
	if (pipeline->slots[slot_index].state == SLOT_ACTIVE)
		pipeline->slots[slot_index].state = SLOT_FREE;
	pthread_mutex_unlock(&pipeline->lock);
}
