#ifndef MS912X_ANDROID_FRAME_PIPELINE_H
#define MS912X_ANDROID_FRAME_PIPELINE_H

#include <stddef.h>
#include <stdint.h>

#define MS912X_ANDROID_MAX_FRAME_BYTES (64U * 1024U * 1024U)
#define MS912X_ANDROID_FRAME_SLOTS 3U

struct ms912x_android_frame_pipeline;

struct ms912x_android_frame_info {
	uint32_t width;
	uint32_t height;
	uint64_t generation;
	uint64_t sequence;
	size_t stride_bytes;
};

struct ms912x_android_frame_pipeline *
ms912x_android_frame_pipeline_create(uint32_t width, uint32_t height,
				    uint64_t generation);
void ms912x_android_frame_pipeline_destroy(
	struct ms912x_android_frame_pipeline *pipeline);

int ms912x_android_frame_pipeline_reconfigure(
	struct ms912x_android_frame_pipeline *pipeline,
	uint32_t width, uint32_t height, uint64_t generation);

int ms912x_android_frame_pipeline_offer_rgba(
	struct ms912x_android_frame_pipeline *pipeline,
	const uint8_t *data, size_t data_size, size_t base_offset,
	size_t row_stride, size_t pixel_stride,
	uint32_t crop_x, uint32_t crop_y,
	uint32_t width, uint32_t height, uint64_t generation);

int ms912x_android_frame_pipeline_acquire(
	struct ms912x_android_frame_pipeline *pipeline,
	unsigned int *slot_index, struct ms912x_android_frame_info *info,
	const uint8_t **pixels);
void ms912x_android_frame_pipeline_release(
	struct ms912x_android_frame_pipeline *pipeline,
	unsigned int slot_index);

#endif
