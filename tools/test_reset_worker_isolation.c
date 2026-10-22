// SPDX-License-Identifier: GPL-2.0-only

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

struct work_struct { unsigned int unused; };
struct device { unsigned int adapter_id; };
struct usb_interface { struct device dev; };
struct drm_device { unsigned int unused; };
struct ms912x_device {
	struct work_struct detect_reset_work;
	struct drm_device drm;
	struct usb_interface *intf;
	int detect_lock;
	int detect_reset_pending;
	bool detect_reset_stopping;
	bool detect_reset_running;
};

#define container_of(ptr, type, member) \
	((type *)((char *)(ptr) - offsetof(type, member)))
#define READ_ONCE(value) (value)
#define WRITE_ONCE(value, next) ((value) = (next))
#define mutex_lock(lock) do { assert(!*(lock)); *(lock) = 1; } while (0)
#define mutex_unlock(lock) do { assert(*(lock)); *(lock) = 0; } while (0)

static struct usb_interface *reset_target;
static unsigned int reset_calls;

static void atomic_set(int *value, int next) { *value = next; }
static void usb_queue_reset_device(struct usb_interface *interface)
{
	reset_calls++;
	reset_target = interface;
}
static void test_dev_info(struct device *device, const char *format, ...)
{
	(void)device;
	(void)format;
}
#define dev_info(device, ...) test_dev_info(device, __VA_ARGS__)

#include "detect-reset-worker.h"

int main(void)
{
	struct usb_interface first_interface = { .dev.adapter_id = 1 };
	struct usb_interface second_interface = { .dev.adapter_id = 2 };
	struct ms912x_device first = {
		.intf = &first_interface,
		.detect_reset_pending = 1,
	};
	struct ms912x_device second = {
		.intf = &second_interface,
		.detect_reset_pending = 1,
	};

	ms912x_detect_reset_worker(&first.detect_reset_work);
	assert(reset_calls == 1 && reset_target == &first_interface);
	assert(first.detect_reset_running && first.detect_reset_pending == 1);
	assert(!second.detect_reset_running && second.detect_reset_pending == 1);

	second.detect_reset_stopping = true;
	ms912x_detect_reset_worker(&second.detect_reset_work);
	assert(reset_calls == 1 && reset_target == &first_interface);
	assert(second.detect_reset_pending == 0 && !second.detect_reset_running);
	assert(first.detect_reset_pending == 1 && first.detect_reset_running);

	puts("PASS reset worker: USB reset and stop cleanup stay on the selected adapter");
	return 0;
}
