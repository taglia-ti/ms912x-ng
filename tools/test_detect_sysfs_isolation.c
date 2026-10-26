// SPDX-License-Identifier: GPL-2.0-only

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#define sysfs_emit(buf, ...) snprintf(buf, 4096, __VA_ARGS__)
#define READ_ONCE(value) (value)
#define WRITE_ONCE(value, next) ((value) = (next))
#define container_of(ptr, type, member) \
	((type *)((char *)(ptr) - offsetof(type, member)))
#define to_usb_interface(device) \
	container_of(device, struct usb_interface, dev)
#define mutex_lock(lock) do { assert(!*(lock)); *(lock) = 1; } while (0)
#define mutex_unlock(lock) do { assert(*(lock)); *(lock) = 0; } while (0)
#define msecs_to_jiffies(value) (value)

enum ms912x_fake_edid_mode {
	MS912X_FAKE_EDID_AUTO,
	MS912X_FAKE_EDID_OFF,
	MS912X_FAKE_EDID_ON,
};
enum ms912x_force_connected_mode {
	MS912X_FORCE_CONNECTED_AUTO,
	MS912X_FORCE_CONNECTED_OFF,
	MS912X_FORCE_CONNECTED_ON,
};

struct device { unsigned int adapter_id; };
struct device_attribute { unsigned int unused; };
struct drm_device { unsigned int adapter_id, hotplug_events; };
struct ms912x_device {
	struct drm_device drm;
	int detect_lock, edid_lock;
	bool fake_edid_override, fake_edid_active;
	enum ms912x_fake_edid_mode fake_edid_mode;
	bool force_connected_override;
	enum ms912x_force_connected_mode force_connected_mode;
	unsigned long detect_status_updated;
	bool edid_cache_valid[4];
};
struct usb_interface {
	struct device dev;
	struct ms912x_device *driver_data;
};

static unsigned long jiffies = 1000;

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
static void test_dev_info(struct device *dev, const char *format, ...)
{
	(void)dev;
	(void)format;
}
#define dev_info(dev, ...) test_dev_info(dev, __VA_ARGS__)

#include "detect-sysfs.h"

int main(void)
{
	char readback[4096];
	struct ms912x_device first = {
		.drm.adapter_id = 1,
		.fake_edid_override = true,
		.fake_edid_mode = MS912X_FAKE_EDID_OFF,
		.fake_edid_active = true,
		.edid_cache_valid = { true, true, true, true },
	};
	struct ms912x_device second = {
		.drm.adapter_id = 2,
		.fake_edid_override = true,
		.fake_edid_mode = MS912X_FAKE_EDID_OFF,
		.fake_edid_active = true,
		.force_connected_override = true,
		.force_connected_mode = MS912X_FORCE_CONNECTED_OFF,
		.edid_cache_valid = { true, true, true, true },
	};
	struct usb_interface first_interface = {
		.dev.adapter_id = 1, .driver_data = &first,
	};
	struct usb_interface second_interface = {
		.dev.adapter_id = 2, .driver_data = &second,
	};
	const char on[] = "on\n", inherit[] = "inherit\n";
	const char bad[] = "maybe\n", force_on[] = "true\n";
	const char force_off[] = "off\n";
	unsigned int i;

	assert(ms912x_force_connected_mode_for_device(&first) ==
	       MS912X_FORCE_CONNECTED_AUTO);
	assert(ms912x_force_connected_mode_for_device(&second) ==
	       MS912X_FORCE_CONNECTED_OFF);
	assert(ms912x_fake_edid_store(&first_interface.dev, NULL, on,
				      sizeof(on) - 1) == sizeof(on) - 1);
	assert(first.fake_edid_override &&
	       first.fake_edid_mode == MS912X_FAKE_EDID_ON &&
	       !first.fake_edid_active && first.detect_status_updated == 750 &&
	       first.drm.hotplug_events == 1);
	assert(ms912x_fake_edid_mode_for_device(&first) == MS912X_FAKE_EDID_ON);
	assert(ms912x_fake_edid_mode_for_device(&second) == MS912X_FAKE_EDID_OFF);
	for (i = 0; i < 4; i++)
		assert(!first.edid_cache_valid[i]);
	assert(second.fake_edid_override && second.fake_edid_active &&
	       second.fake_edid_mode == MS912X_FAKE_EDID_OFF &&
	       second.drm.hotplug_events == 0);
	for (i = 0; i < 4; i++)
		assert(second.edid_cache_valid[i]);

	assert(ms912x_fake_edid_store(&first_interface.dev, NULL, on,
				      sizeof(on) - 1) == sizeof(on) - 1);
	assert(first.drm.hotplug_events == 1);
	assert(ms912x_fake_edid_store(&second_interface.dev, NULL, bad,
				      sizeof(bad) - 1) == -EINVAL);
	assert(second.fake_edid_active && second.drm.hotplug_events == 0);

	assert(ms912x_force_connected_store(&second_interface.dev, NULL,
					    force_on,
					    sizeof(force_on) - 1) ==
	       sizeof(force_on) - 1);
	assert(second.force_connected_override &&
	       second.force_connected_mode == MS912X_FORCE_CONNECTED_ON &&
	       second.detect_status_updated == 750 &&
	       second.drm.hotplug_events == 1);
	assert(ms912x_force_connected_mode_for_device(&second) ==
	       MS912X_FORCE_CONNECTED_ON);
	assert(!first.force_connected_override && first.drm.hotplug_events == 1);

	assert(ms912x_force_connected_store(&first_interface.dev, NULL,
					    force_off,
					    sizeof(force_off) - 1) ==
	       sizeof(force_off) - 1);
	assert(first.force_connected_override &&
	       first.force_connected_mode == MS912X_FORCE_CONNECTED_OFF &&
	       first.drm.hotplug_events == 2);
	assert(second.force_connected_mode == MS912X_FORCE_CONNECTED_ON &&
	       second.drm.hotplug_events == 1);

	assert(ms912x_fake_edid_store(&first_interface.dev, NULL, inherit,
				      sizeof(inherit) - 1) == sizeof(inherit) - 1);
	assert(!first.fake_edid_override &&
	       first.fake_edid_mode == MS912X_FAKE_EDID_AUTO &&
	       first.drm.hotplug_events == 3);
	assert(ms912x_fake_edid_mode_for_device(&first) == MS912X_FAKE_EDID_AUTO);
	assert(second.fake_edid_override && second.fake_edid_active &&
	       second.drm.hotplug_events == 1);
	assert(!first.detect_lock && !first.edid_lock &&
	       !second.detect_lock && !second.edid_lock);

	assert(ms912x_fake_edid_show(&first_interface.dev, NULL, readback) == 8);
	assert(!strcmp(readback, "inherit\n"));
	assert(ms912x_fake_edid_show(&second_interface.dev, NULL, readback) == 4);
	assert(!strcmp(readback, "off\n"));
	assert(ms912x_force_connected_show(&first_interface.dev, NULL, readback) == 4);
	assert(!strcmp(readback, "off\n"));
	assert(ms912x_force_connected_show(&second_interface.dev, NULL, readback) == 3);
	assert(!strcmp(readback, "on\n"));
	first_interface.driver_data = NULL;
	assert(ms912x_fake_edid_show(&first_interface.dev, NULL, readback) == -ENODEV);
	assert(ms912x_force_connected_show(&first_interface.dev, NULL, readback) == -ENODEV);

	puts("PASS detection sysfs: EDID and connection overrides isolate cache and hotplug per adapter");
	return 0;
}
