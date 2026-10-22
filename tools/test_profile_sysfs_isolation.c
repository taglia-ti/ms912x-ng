// SPDX-License-Identifier: GPL-2.0-only

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#include "ms912x_profile_policy.h"

#define READ_ONCE(value) (value)
#define WRITE_ONCE(value, next) ((value) = (next))
#define USB_SPEED_SUPER 5
#define IS_ENABLED(option) 1
#define container_of(ptr, type, member) \
	((type *)((char *)(ptr) - offsetof(type, member)))

struct device { unsigned int adapter_id; };
struct device_attribute { unsigned int unused; };
struct ms912x_device {
	struct device drm_device;
	int damage_scan_lock;
	int usb_speed;
	bool profile_override;
	enum ms912x_profile profile_requested;
	enum ms912x_profile effective_profile;
	enum ms912x_damage_scan_mode damage_scan_mode_default;
	unsigned int damage_scan_stride_default;
	unsigned int damage_scan_device_config_generation;
};
struct usb_interface {
	struct device dev;
	struct ms912x_device *driver_data;
};
struct ms912x_parameters_test { enum ms912x_profile profile; };

static struct ms912x_parameters_test ms912x_parameters = {
	.profile = MS912X_PROFILE_QUALITY,
};
static int ms912x_profile_lock;
static unsigned int settings_logs;

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
static void ms912x_profile_log_settings(struct device *dev,
					struct ms912x_device *ms912x)
{
	(void)dev;
	(void)ms912x;
	settings_logs++;
}

#include "profile-sysfs.h"

int main(void)
{
	struct ms912x_device first = {
		.drm_device.adapter_id = 1,
		.usb_speed = 3,
		.effective_profile = MS912X_PROFILE_AUTO,
		.damage_scan_mode_default = MS912X_DAMAGE_SCAN_AUTO,
	};
	struct ms912x_device second = {
		.drm_device.adapter_id = 2,
		.usb_speed = USB_SPEED_SUPER,
		.effective_profile = MS912X_PROFILE_AUTO,
		.damage_scan_mode_default = MS912X_DAMAGE_SCAN_AUTO,
	};
	struct usb_interface first_interface = {
		.dev.adapter_id = 1, .driver_data = &first,
	};
	struct usb_interface second_interface = {
		.dev.adapter_id = 2, .driver_data = &second,
	};
	const char latency[] = "latency\n";
	const char efficiency[] = "efficiency\n";
	const char inherit[] = "inherit\n";
	const char invalid[] = "fast\n";

	assert(ms912x_profile_store(&first_interface.dev, NULL, latency,
				    sizeof(latency) - 1) == sizeof(latency) - 1);
	assert(first.profile_override &&
	       first.profile_requested == MS912X_PROFILE_LATENCY &&
	       first.effective_profile == MS912X_PROFILE_LATENCY &&
	       first.damage_scan_mode_default == MS912X_DAMAGE_SCAN_ON &&
	       first.damage_scan_stride_default == 2 &&
	       first.damage_scan_device_config_generation == 2);
	assert(second.effective_profile == MS912X_PROFILE_AUTO &&
	       second.damage_scan_mode_default == MS912X_DAMAGE_SCAN_AUTO &&
	       !second.damage_scan_device_config_generation);

	assert(ms912x_profile_store(&second_interface.dev, NULL, efficiency,
				    sizeof(efficiency) - 1) == sizeof(efficiency) - 1);
	assert(second.profile_override &&
	       second.profile_requested == MS912X_PROFILE_EFFICIENCY &&
	       second.effective_profile == MS912X_PROFILE_EFFICIENCY &&
	       second.damage_scan_mode_default == MS912X_DAMAGE_SCAN_OFF &&
	       second.damage_scan_stride_default == 5 &&
	       second.damage_scan_device_config_generation == 2);
	assert(first.effective_profile == MS912X_PROFILE_LATENCY &&
	       first.damage_scan_device_config_generation == 2);

	assert(ms912x_profile_store(&first_interface.dev, NULL, invalid,
				    sizeof(invalid) - 1) == -EINVAL);
	assert(first.effective_profile == MS912X_PROFILE_LATENCY &&
	       first.damage_scan_device_config_generation == 2);
	assert(ms912x_profile_store(&first_interface.dev, NULL, inherit,
				    sizeof(inherit) - 1) == sizeof(inherit) - 1);
	assert(!first.profile_override &&
	       first.effective_profile == MS912X_PROFILE_QUALITY &&
	       first.damage_scan_mode_default == MS912X_DAMAGE_SCAN_AUTO &&
	       first.damage_scan_stride_default == 1 &&
	       first.damage_scan_device_config_generation == 4);
	assert(second.effective_profile == MS912X_PROFILE_EFFICIENCY &&
	       second.damage_scan_device_config_generation == 2);
	assert(!first.damage_scan_lock && !second.damage_scan_lock &&
	       !ms912x_profile_lock && settings_logs == 3);

	puts("PASS profile sysfs: profile values and generation updates stay per-adapter");
	return 0;
}
