#ifndef MS912X_ANDROID_FRAME_PIPELINE_INTERNAL_H
#define MS912X_ANDROID_FRAME_PIPELINE_INTERNAL_H

#include "frame_pipeline.h"
#include "ms912x_frame_damage.h"
#include "ms912x_rgbx_convert.h"

#include <pthread.h>
#include <stdbool.h>

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
	unsigned int coalesced_slot;
	bool submission_pending;
	uint64_t next_submission_sequence;
	uint64_t coalesced_sequence;
	size_t coalesced_length;
	uint8_t *coalesced_destination;
	uint64_t usb_rate, usb_samples, coalesce_deadline;
	bool backpressured;
	bool reconfiguring;
	struct ms912x_android_scan_options scan_options;
	size_t last_packet_bytes;
	bool rescan_after_submission;
	bool scan_enabled;
	uint32_t width, height;
	uint64_t generation;
	uint64_t next_sequence;
	uint64_t published_sequence;
	size_t frame_bytes;
	enum ms912x_color_backend backend;
};


/* consumer_lock must be held by callers. */
void ms912x_frame_pipeline_cancel_coalesced_locked(
	struct ms912x_android_frame_pipeline *pipeline, bool retain_latest);

#endif
