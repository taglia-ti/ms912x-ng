/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MS912X_FRAME_DAMAGE_H
#define MS912X_FRAME_DAMAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "ms912x_scan_backend_types.h"

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
/* Defer an unsent batch while retaining its baseline and replay history. */
int ms912x_frame_damage_cancel(struct ms912x_frame_damage *damage, uint64_t sequence);
bool ms912x_frame_damage_pending_full(const struct ms912x_frame_damage *damage);
/* Commit only after the complete logical USB batch succeeds; failure forces repair. */
int ms912x_frame_damage_finish(struct ms912x_frame_damage *damage,
			       uint64_t sequence, bool transferred);
/* A reset during an outstanding submission also prevents that batch from committing. */
void ms912x_frame_damage_invalidate(struct ms912x_frame_damage *damage);
/* Marks encoded payload only; toggling schedules a clean full-frame refresh. */
void ms912x_frame_damage_set_show_draws(struct ms912x_frame_damage *damage, bool enabled);
/* Same byte buckets as the Linux UYVY scanner; sent pixels alone advance the baseline. */
int ms912x_frame_damage_set_scan_options(struct ms912x_frame_damage *damage,
                                       unsigned int stride, unsigned int tolerance);
/* Disabled scanning sends each offered snapshot in full; idle replay remains bounded. */
void ms912x_frame_damage_set_scan_enabled(struct ms912x_frame_damage *damage, bool enabled);

#endif
