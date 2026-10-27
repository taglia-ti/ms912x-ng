// SPDX-License-Identifier: GPL-2.0-only

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>

#define strscpy(buf, value, size) snprintf(buf, size, "%s", value)
#define MS912X_SCALE_AUTO 0
#define MS912X_SCALE_50 50
#define MS912X_SCALE_67 67
#define MS912X_SCALE_80 80
#define MS912X_SCALE_100 100
#define sysfs_emit(buf, ...) snprintf(buf, 4096, __VA_ARGS__)
#define READ_ONCE(value) (value)
#define WRITE_ONCE(value, next) ((value) = (next))
#define container_of(ptr, type, member) \
	((type *)((char *)(ptr) - offsetof(type, member)))

struct device { unsigned int adapter_id; };
struct device_attribute { unsigned int unused; };
struct drm_device { unsigned int adapter_id, hotplug_events; };
struct ms912x_device {
	struct drm_device drm;
	unsigned int scale_setting;
	unsigned int scale_device_config_generation;
};
struct usb_interface {
	struct device dev;
	struct ms912x_device *driver_data;
};

static int ms912x_scale_lock;
static unsigned int settings_logs;

#define to_usb_interface(device) \
	container_of(device, struct usb_interface, dev)
#define mutex_lock(lock) do { assert(!*(lock)); *(lock) = 1; } while (0)
#define mutex_unlock(lock) do { assert(*(lock)); *(lock) = 0; } while (0)

static int scnprintf(char *buf, size_t size, const char *format, ...)
{
	va_list args;
	va_start(args, format);
	int count = vsnprintf(buf, size, format, args);
	va_end(args);
	return count < (int)size ? count : (int)size - 1;
}

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
static void ms912x_log_settings(struct device *dev,
					struct ms912x_device *ms912x)
{
	(void)dev;
	(void)ms912x;
	settings_logs++;
}

#include "scale-sysfs.h"

int main(void)
{
	char readback[4096];
	struct ms912x_device first = { .drm.adapter_id = 1, .scale_setting = MS912X_SCALE_100 };
	struct ms912x_device second = { .drm.adapter_id = 2, .scale_setting = MS912X_SCALE_100 };
	struct usb_interface first_interface = {
		.dev.adapter_id = 1, .driver_data = &first,
	};
	struct usb_interface second_interface = {
		.dev.adapter_id = 2, .driver_data = &second,
	};
	const char scale_67[] = "67\n";
	const char full[] = "100\n";
	const char removed[] = "inherit\n";
	const char invalid[] = "79\n";

	assert(ms912x_scale_store(&first_interface.dev, NULL, scale_67,
				  sizeof(scale_67) - 1) == sizeof(scale_67) - 1);
	assert(first.scale_setting == 67);
	assert(ms912x_scale_show(&first_interface.dev, NULL, readback) == 3);
	assert(!strcmp(readback, "67\n"));
	assert(first.scale_device_config_generation == 1);
	assert(first.drm.hotplug_events == 1 && settings_logs == 1);
	assert(second.scale_setting == MS912X_SCALE_100 &&
	       second.scale_device_config_generation == 0 &&
	       second.drm.hotplug_events == 0);

	/* Repeating the same value does not trigger a redundant reconfiguration. */
	assert(ms912x_scale_store(&first_interface.dev, NULL, scale_67,
				  sizeof(scale_67) - 1) == sizeof(scale_67) - 1);
	assert(first.scale_device_config_generation == 1 &&
	       first.drm.hotplug_events == 1);

	assert(ms912x_scale_store(&second_interface.dev, NULL, full,
				  sizeof(full) - 1) == sizeof(full) - 1);
	assert(second.scale_setting == MS912X_SCALE_100 &&
	       second.scale_device_config_generation == 0 &&
	       second.drm.hotplug_events == 0);
	assert(ms912x_scale_store(&first_interface.dev, NULL, full,
				  sizeof(full) - 1) == sizeof(full) - 1);
	assert(first.scale_setting == MS912X_SCALE_100 &&
	       first.scale_device_config_generation == 2 &&
	       first.drm.hotplug_events == 2 && settings_logs == 2);

	assert(ms912x_scale_store(&second_interface.dev, NULL, invalid,
				  sizeof(invalid) - 1) == -EINVAL);
	assert(second.scale_device_config_generation == 0 &&
	       second.drm.hotplug_events == 0 && settings_logs == 2);
	assert(ms912x_scale_show(&first_interface.dev, NULL, readback) == 4);
	assert(!strcmp(readback, "100\n"));
	assert(ms912x_scale_store(&first_interface.dev, NULL, removed,
				  sizeof(removed) - 1) == -EINVAL);
	assert(first.scale_setting == MS912X_SCALE_100 &&
	       first.scale_device_config_generation == 2 &&
	       first.drm.hotplug_events == 2);
	first_interface.driver_data = NULL;
	assert(ms912x_scale_show(&first_interface.dev, NULL, readback) == -ENODEV);
	assert(!ms912x_scale_lock);

	puts("PASS scale sysfs: setting generations and hotplug notifications stay per-adapter");
	return 0;
}
