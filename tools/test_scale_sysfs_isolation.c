// SPDX-License-Identifier: GPL-2.0-only

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#define MS912X_SCALE_AUTO 0
#define MS912X_SCALE_50 50
#define MS912X_SCALE_67 67
#define MS912X_SCALE_80 80
#define MS912X_SCALE_100 100
#define READ_ONCE(value) (value)
#define WRITE_ONCE(value, next) ((value) = (next))
#define container_of(ptr, type, member) \
	((type *)((char *)(ptr) - offsetof(type, member)))

struct device { unsigned int adapter_id; };
struct device_attribute { unsigned int unused; };
struct drm_device { unsigned int adapter_id, hotplug_events; };
struct ms912x_device {
	struct drm_device drm;
	bool scale_is_overridden;
	unsigned int scale_override_setting;
	unsigned int scale_device_config_generation;
};
struct usb_interface {
	struct device dev;
	struct ms912x_device *driver_data;
};

static int ms912x_scale_lock;
static unsigned int profile_logs;

#define to_usb_interface(device) \
	container_of(device, struct usb_interface, dev)
#define mutex_lock(lock) do { assert(!*(lock)); *(lock) = 1; } while (0)
#define mutex_unlock(lock) do { assert(*(lock)); *(lock) = 0; } while (0)

static bool sysfs_streq(const char *left, const char *right)
{
	size_t length = strlen(right);
	return !strncmp(left, right, length) &&
		(!left[length] || (left[length] == '\n' && !left[length + 1]));
}
static struct ms912x_device *usb_get_intfdata(struct usb_interface *interface)
{
	return interface->driver_data;
}
static void drm_kms_helper_hotplug_event(struct drm_device *drm)
{
	drm->hotplug_events++;
}
static void ms912x_profile_log_settings(struct device *dev,
					struct ms912x_device *ms912x)
{
	(void)dev;
	(void)ms912x;
	profile_logs++;
}

#include "scale-sysfs.h"

int main(void)
{
	struct ms912x_device first = { .drm.adapter_id = 1 };
	struct ms912x_device second = { .drm.adapter_id = 2 };
	struct usb_interface first_interface = {
		.dev.adapter_id = 1, .driver_data = &first,
	};
	struct usb_interface second_interface = {
		.dev.adapter_id = 2, .driver_data = &second,
	};
	const char scale_67[] = "67\n";
	const char inherit[] = "inherit\n";
	const char invalid[] = "79\n";

	assert(ms912x_scale_store(&first_interface.dev, NULL, scale_67,
				  sizeof(scale_67) - 1) == sizeof(scale_67) - 1);
	assert(first.scale_is_overridden && first.scale_override_setting == 67);
	assert(first.scale_device_config_generation == 1);
	assert(first.drm.hotplug_events == 1 && profile_logs == 1);
	assert(!second.scale_is_overridden &&
	       second.scale_device_config_generation == 0 &&
	       second.drm.hotplug_events == 0);

	/* Repeating the same value does not trigger a redundant reconfiguration. */
	assert(ms912x_scale_store(&first_interface.dev, NULL, scale_67,
				  sizeof(scale_67) - 1) == sizeof(scale_67) - 1);
	assert(first.scale_device_config_generation == 1 &&
	       first.drm.hotplug_events == 1);

	assert(ms912x_scale_store(&second_interface.dev, NULL, inherit,
				  sizeof(inherit) - 1) == sizeof(inherit) - 1);
	assert(!second.scale_is_overridden &&
	       second.scale_device_config_generation == 0 &&
	       second.drm.hotplug_events == 0);
	assert(ms912x_scale_store(&first_interface.dev, NULL, inherit,
				  sizeof(inherit) - 1) == sizeof(inherit) - 1);
	assert(!first.scale_is_overridden &&
	       first.scale_device_config_generation == 2 &&
	       first.drm.hotplug_events == 2 && profile_logs == 2);

	assert(ms912x_scale_store(&second_interface.dev, NULL, invalid,
				  sizeof(invalid) - 1) == -EINVAL);
	assert(second.scale_device_config_generation == 0 &&
	       second.drm.hotplug_events == 0 && profile_logs == 2);
	assert(!ms912x_scale_lock);

	puts("PASS scale sysfs: override generations and hotplug notifications stay per-adapter");
	return 0;
}
