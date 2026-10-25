#ifndef MS912X_ANDROID_FRAME_PIPELINE_H
#define MS912X_ANDROID_FRAME_PIPELINE_H

#include <stddef.h>
#include <stdint.h>
#include "ms912x_mode3_core.h"

#define MS912X_ANDROID_MAX_FRAME_BYTES (64U * 1024U * 1024U)
#define MS912X_ANDROID_FRAME_SLOTS 3U

enum ms912x_android_pixel_format {
	MS912X_ANDROID_PIXEL_UYVY422 = 1,
};

struct ms912x_android_frame_pipeline;
struct ms912x_android_scan_options {
    unsigned int profile, stride, tolerance_min, tolerance_max;
};
int ms912x_android_profile_defaults(unsigned int profile,
                                  struct ms912x_android_scan_options *options);
/* Live update: preserve submitted buffers, invalidate comparison and rescan latest pixels. */
int ms912x_android_frame_pipeline_set_options(struct ms912x_android_frame_pipeline *pipeline,
                                             unsigned int profile, unsigned int stride,
                                             int tolerance);

struct ms912x_android_frame_info {
	uint32_t width;
	uint32_t height;
	uint64_t generation;
	uint64_t sequence;
	size_t stride_bytes;
	enum ms912x_android_pixel_format pixel_format;
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
int ms912x_android_frame_pipeline_encode_latest(
	struct ms912x_android_frame_pipeline *pipeline,
	uint8_t *destination, size_t destination_capacity,
	struct ms912x_android_frame_info *info);
/* Caller supplies monotonic time; zero with wait_ns > 0 defers a replaceable snapshot. */
int ms912x_android_frame_pipeline_encode_paced(
    struct ms912x_android_frame_pipeline *pipeline, uint8_t *destination, size_t capacity,
    struct ms912x_android_frame_info *info, uint64_t now_ns, unsigned int refresh,
    uint64_t *wait_ns);
void ms912x_android_frame_pipeline_record_usb(
    struct ms912x_android_frame_pipeline *pipeline, size_t bytes, uint64_t duration_ns);
/* Complete exactly one positive encode result, using info.sequence as its token. */
int ms912x_android_frame_pipeline_complete(
	struct ms912x_android_frame_pipeline *pipeline, uint64_t sequence, int transferred);
const char *ms912x_android_frame_pipeline_backend(
	struct ms912x_android_frame_pipeline *pipeline);
size_t ms912x_android_frame_pipeline_packet_capacity(
	struct ms912x_android_frame_pipeline *pipeline);
void ms912x_android_frame_pipeline_invalidate_baseline(
	struct ms912x_android_frame_pipeline *pipeline);
void ms912x_android_frame_pipeline_set_show_draws(
	struct ms912x_android_frame_pipeline *pipeline, int enabled);

#endif
