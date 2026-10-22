// SPDX-License-Identifier: GPL-2.0-only

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#define MS912X_SCAN_FRAME_SLOTS 2
#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))

struct drm_device { unsigned int unused; };
struct ms912x_scan_frame { unsigned int *pixels; };
struct ms912x_device {
	struct drm_device drm;
	struct ms912x_scan_frame scan_frames[MS912X_SCAN_FRAME_SLOTS];
};

static unsigned int freed_frames;
static void vfree(void *ptr)
{
	if (!ptr)
		return;
	freed_frames++;
	free(ptr);
}

#include "probe-frame-cleanup.h"

static void partial_probe_cleanup_keeps_other_adapter_owned(unsigned int count)
{
	struct ms912x_device first = {}, second = {};
	unsigned int i;

	for (i = 0; i < count; i++)
		first.scan_frames[i].pixels = malloc(sizeof(unsigned int));
	for (i = 0; i < MS912X_SCAN_FRAME_SLOTS; i++)
		second.scan_frames[i].pixels = malloc(sizeof(unsigned int));
	for (i = 0; i < count; i++)
		assert(first.scan_frames[i].pixels);
	for (i = 0; i < MS912X_SCAN_FRAME_SLOTS; i++)
		assert(second.scan_frames[i].pixels);

	freed_frames = 0;
	ms912x_free_scan_frames(&first.drm, &first);
	assert(freed_frames == count);
	for (i = 0; i < count; i++)
		assert(!first.scan_frames[i].pixels);
	ms912x_free_scan_frames(&first.drm, &first);
	assert(freed_frames == count);
	for (i = 0; i < MS912X_SCAN_FRAME_SLOTS; i++)
		assert(second.scan_frames[i].pixels);

	ms912x_free_scan_frames(&second.drm, &second);
	assert(freed_frames == count + MS912X_SCAN_FRAME_SLOTS);
}

int main(void)
{
	partial_probe_cleanup_keeps_other_adapter_owned(0);
	partial_probe_cleanup_keeps_other_adapter_owned(1);
	partial_probe_cleanup_keeps_other_adapter_owned(2);
	puts("PASS probe frame cleanup: partial per-adapter allocations unwind without freeing peer buffers");
	return 0;
}
