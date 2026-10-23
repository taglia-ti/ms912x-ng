/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MS912X_FRAME_DAMAGE_H
#define MS912X_FRAME_DAMAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct ms912x_frame_damage;
size_t ms912x_frame_damage_packet_capacity(uint32_t width, uint32_t height);

/* Caller serializes operations; pixels are tightly packed, uint32_t-aligned UYVY. */
struct ms912x_frame_damage *ms912x_frame_damage_create(uint32_t width, uint32_t height);
void ms912x_frame_damage_destroy(struct ms912x_frame_damage *damage);
/* A positive result pins the caller's pixels until finish(sequence, ...). */
int ms912x_frame_damage_prepare(struct ms912x_frame_damage *damage,
				uint8_t *packet, size_t capacity,
				const uint8_t *pixels, uint64_t sequence);
/* One final history repair from completed pixels when capture is idle. */
int ms912x_frame_damage_prepare_replay(struct ms912x_frame_damage *damage,
				       uint8_t *packet, size_t capacity, uint64_t sequence);
/* Commit only after the complete logical USB batch succeeds; failure forces repair. */
int ms912x_frame_damage_finish(struct ms912x_frame_damage *damage,
			       uint64_t sequence, bool transferred);
/* A reset during an outstanding submission also prevents that batch from committing. */
void ms912x_frame_damage_invalidate(struct ms912x_frame_damage *damage);

#endif
