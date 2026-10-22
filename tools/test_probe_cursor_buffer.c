// SPDX-License-Identifier: GPL-2.0-only

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#define MS912X_CURSOR_MAX_WIDTH 128
#define MS912X_CURSOR_MAX_HEIGHT 128
typedef unsigned int u32;

struct drm_device { unsigned int unused; };
struct ms912x_device {
	struct drm_device drm;
	unsigned int *cursor_buffer_storage;
	unsigned int *cursor_source_pixels;
	unsigned int *cursor_staging_pixels;
	unsigned int *cursor_prepare_pixels;
	unsigned int *cursor_pixels;
};

typedef void (*drmm_action_fn)(struct drm_device *drm, void *data);

static bool allocation_fails;
static int action_result;
static unsigned int freed;

static void *vzalloc(size_t bytes)
{
	return allocation_fails ? NULL : calloc(1, bytes);
}

static void vfree(void *ptr)
{
	if (!ptr)
		return;
	freed++;
	free(ptr);
}

static int drmm_add_action_or_reset(struct drm_device *drm,
				    drmm_action_fn action, void *data)
{
	if (action_result)
		action(drm, data);
	return action_result;
}

#include "probe-cursor-buffer.h"

static void allocation_failure_leaves_no_partial_state(void)
{
	struct ms912x_device device = {};

	allocation_fails = true;
	assert(ms912x_init_cursor_buffers(&device) == -ENOMEM);
	assert(!device.cursor_buffer_storage && !device.cursor_source_pixels &&
	       !device.cursor_staging_pixels && !device.cursor_prepare_pixels &&
	       !device.cursor_pixels);
	assert(freed == 0);
	allocation_fails = false;
}

static void action_registration_failure_releases_storage(void)
{
	struct ms912x_device device = {};

	action_result = -ENOMEM;
	assert(ms912x_init_cursor_buffers(&device) == -ENOMEM);
	assert(freed == 1);
	assert(!device.cursor_buffer_storage && !device.cursor_source_pixels &&
	       !device.cursor_staging_pixels && !device.cursor_prepare_pixels &&
	       !device.cursor_pixels);
	action_result = 0;
}

static void successful_buffers_are_owned_per_adapter(void)
{
	const unsigned int pixels = MS912X_CURSOR_MAX_WIDTH *
				    MS912X_CURSOR_MAX_HEIGHT;
	struct ms912x_device first = {}, second = {};

	assert(ms912x_init_cursor_buffers(&first) == 0);
	assert(ms912x_init_cursor_buffers(&second) == 0);
	assert(first.cursor_source_pixels == first.cursor_buffer_storage);
	assert(first.cursor_staging_pixels == first.cursor_source_pixels + pixels);
	assert(first.cursor_prepare_pixels == first.cursor_staging_pixels + pixels);
	assert(first.cursor_pixels == first.cursor_prepare_pixels + pixels);
	assert(first.cursor_buffer_storage != second.cursor_buffer_storage);

	freed = 0;
	ms912x_free_cursor_buffers(&first.drm, &first);
	assert(freed == 1 && !first.cursor_buffer_storage &&
	       !first.cursor_source_pixels && !first.cursor_staging_pixels &&
	       !first.cursor_prepare_pixels && !first.cursor_pixels &&
	       second.cursor_buffer_storage);
	ms912x_free_cursor_buffers(&first.drm, &first);
	assert(freed == 1);
	ms912x_free_cursor_buffers(&second.drm, &second);
	assert(freed == 2);
}

int main(void)
{
	allocation_failure_leaves_no_partial_state();
	action_registration_failure_releases_storage();
	successful_buffers_are_owned_per_adapter();
	puts("PASS probe cursor buffers: allocation unwind and per-adapter ownership");
	return 0;
}
