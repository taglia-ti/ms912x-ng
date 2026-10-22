// SPDX-License-Identifier: GPL-2.0-only

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_rect_compat.h"

#define GFP_KERNEL 0
#define PAGE_SIZE 4096
#define MS912X_INITIAL_DAMAGE_RECTS 8
#define DIV_ROUND_UP(value, divisor) (((value) + (divisor) - 1) / (divisor))

struct ms912x_device {};
struct work_struct { int unused; };
struct timer_list { int unused; };
struct page { int unused; };
struct sg_table { bool allocated; };

enum ms912x_request_state { MS912X_REQUEST_FREE };

struct ms912x_usb_request {
	struct drm_rect *rects;
	struct drm_rect *current_rects;
	unsigned int rect_capacity, current_rect_capacity;
	void *transfer_buffer;
	struct sg_table transfer_sgt;
	struct ms912x_device *ms912x;
	struct timer_list timer;
	struct work_struct work;
	enum ms912x_request_state state;
};

static unsigned int allocation_calls, fail_at, live_allocations;
static struct page fake_page;

static bool allocation_fails(void)
{
	return allocation_calls++ == fail_at;
}

static void *tracked_alloc(size_t size)
{
	void *ptr;

	if (allocation_fails())
		return NULL;
	ptr = calloc(1, size);
	if (ptr)
		live_allocations++;
	return ptr;
}

static void tracked_free(void *ptr)
{
	if (!ptr)
		return;
	assert(live_allocations);
	live_allocations--;
	free(ptr);
}

static void *kvcalloc(size_t count, size_t size, int flags)
{
	(void)flags;
	return tracked_alloc(count * size);
}

static void *vmalloc_32(size_t size) { return tracked_alloc(size); }
static void *kmalloc_array(size_t count, size_t size, int flags)
{
	(void)flags;
	return tracked_alloc(count * size);
}
static void kvfree(void *ptr) { tracked_free(ptr); }
static void vfree(void *ptr) { tracked_free(ptr); }
static void kfree(void *ptr) { tracked_free(ptr); }
static struct page *vmalloc_to_page(void *ptr)
{
	(void)ptr;
	return &fake_page;
}
static int sg_alloc_table_from_pages(struct sg_table *table,
				     struct page **pages, unsigned int count,
				     unsigned int offset, size_t size, int flags)
{
	(void)pages;
	(void)count;
	(void)offset;
	(void)size;
	(void)flags;
	if (allocation_fails())
		return -ENOMEM;
	table->allocated = true;
	live_allocations++;
	return 0;
}
static void sg_free_table(struct sg_table *table)
{
	assert(table->allocated && live_allocations);
	table->allocated = false;
	live_allocations--;
}
static void cancel_work_sync(struct work_struct *work) { (void)work; }
static void timer_shutdown_sync(struct timer_list *timer) { (void)timer; }
static void timer_setup(struct timer_list *timer, void (*callback)(struct timer_list *),
		unsigned int flags)
{
	(void)timer;
	(void)callback;
	(void)flags;
}
static void ms912x_usb_request_timeout(struct timer_list *timer) { (void)timer; }
static void ms912x_usb_request_worker(struct work_struct *work) { (void)work; }
#define INIT_WORK(work, callback) do { (void)(work); (void)(callback); } while (0)

#include "usb-request-init.h"

static void second_request_allocation_failure_releases_cleanly(
					unsigned int allocation_to_fail)
{
	struct ms912x_device device;
	struct ms912x_usb_request first = {}, second = {};
	unsigned int first_request_live;

	allocation_calls = 0;
	fail_at = UINT_MAX;
	assert(!ms912x_usb_request_init(&device, &first, 8192));
	first_request_live = live_allocations;
	assert(first.transfer_buffer && first.rects && first.current_rects);
	assert(first.transfer_sgt.allocated);

	allocation_calls = 0;
	fail_at = allocation_to_fail;
	assert(ms912x_usb_request_init(&device, &second, 8192) == -ENOMEM);
	assert(!second.transfer_buffer && !second.rects && !second.current_rects);
	assert(!second.rect_capacity && !second.current_rect_capacity);
	assert(!second.transfer_sgt.allocated);
	assert(live_allocations == first_request_live);

	fail_at = UINT_MAX;
	ms912x_usb_request_release(&first);
	assert(!first.transfer_buffer && !first.rects && !first.current_rects);
	assert(!first.rect_capacity && !first.current_rect_capacity);
	assert(live_allocations == 0);
}

int main(void)
{
	unsigned int failure;

	for (failure = 0; failure < 5; failure++)
		second_request_allocation_failure_releases_cleanly(failure);
	puts("PASS USB request init: partial second-request allocation failures release only owned resources");
	return 0;
}
