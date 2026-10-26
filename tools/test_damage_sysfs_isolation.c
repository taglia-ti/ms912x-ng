// SPDX-License-Identifier: GPL-2.0-only

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "ms912x_damage_policy.h"

typedef uint32_t u32;

#define sysfs_emit(buf, ...) snprintf(buf, 4096, __VA_ARGS__)
#define READ_ONCE(value) (value)
#define WRITE_ONCE(value, next) ((value) = (next))
#define GFP_KERNEL 0
#define container_of(ptr, type, member) \
	((type *)((char *)(ptr) - offsetof(type, member)))

struct device { unsigned int adapter_id; };
struct device_attribute { unsigned int unused; };
struct ms912x_device {
	struct device drm_device;
	int damage_scan_lock;
	bool damage_scan_mode_is_overridden;
	enum ms912x_damage_scan_mode damage_scan_mode_override;
	bool damage_scan_tolerance_is_overridden;
	unsigned int damage_scan_tolerance_override_min;
	unsigned int damage_scan_tolerance_override_max;
	bool damage_scan_stride_is_overridden;
	unsigned int damage_scan_stride_override_value;
	u32 damage_scan_device_config_generation;
	u64 present_queue_pressure_ns;
	u64 present_queue_low_watermark_ns;
	u64 present_queue_high_watermark_ns;
};
struct usb_interface {
	struct device dev;
	struct ms912x_device *driver_data;
};

#define to_usb_interface(device) \
	container_of(device, struct usb_interface, dev)
#define mutex_lock(lock) do { assert(!*(lock)); *(lock) = 1; } while (0)
#define mutex_unlock(lock) do { assert(*(lock)); *(lock) = 0; } while (0)

static unsigned int settings_logs;

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
static char *kstrndup(const char *source, size_t length, int flags)
{
	char *copy = malloc(length + 1);
	(void)flags;
	if (copy) {
		memcpy(copy, source, length);
		copy[length] = '\0';
	}
	return copy;
}
static void kfree(void *ptr) { free(ptr); }
static int kstrtouint(const char *string, unsigned int base,
		      unsigned int *value)
{
	char *end;
	unsigned long parsed;

	errno = 0;
	parsed = strtoul(string, &end, base);
	if (errno || end == string ||
	    (*end && !(*end == '\n' && !end[1])) || parsed > UINT_MAX)
		return -EINVAL;
	*value = parsed;
	return 0;
}
static void ms912x_log_settings(struct device *dev,
					struct ms912x_device *ms912x)
{
	(void)dev;
	(void)ms912x;
	settings_logs++;
}

#include "damage-sysfs.h"

int main(void)
{
	char readback[4096];
	struct ms912x_device first = {}, second = {};
	struct usb_interface first_interface = {
		.dev.adapter_id = 1, .driver_data = &first,
	};
	struct usb_interface second_interface = {
		.dev.adapter_id = 2, .driver_data = &second,
	};
	const char on[] = "on\n", off[] = "off\n", inherit[] = "inherit\n";
	const char tolerance[] = "3-15\n", zero[] = "0\n";
	const char bad_tolerance[] = "16-3\n", stride[] = "4\n";
	const char bad_stride[] = "17\n", stride_two[] = "2\n";

	assert(ms912x_damage_scan_effective_tolerance(&first) == 0);
	assert(ms912x_damage_scan_effective_tolerance(&second) == 0);
	first.present_queue_low_watermark_ns = 10;
	first.present_queue_high_watermark_ns = 30;
	assert(ms912x_damage_scan_store(&first_interface.dev, NULL, on,
					 sizeof(on) - 1) == sizeof(on) - 1);
	assert(first.damage_scan_mode_is_overridden &&
	       first.damage_scan_mode_override == MS912X_DAMAGE_SCAN_ON &&
	       first.damage_scan_device_config_generation == 1);
	assert(!second.damage_scan_mode_is_overridden &&
	       !second.damage_scan_device_config_generation);
	assert(ms912x_damage_scan_store(&first_interface.dev, NULL, on,
					 sizeof(on) - 1) == sizeof(on) - 1);
	assert(first.damage_scan_device_config_generation == 1);
	assert(ms912x_damage_scan_store(&second_interface.dev, NULL, off,
					 sizeof(off) - 1) == sizeof(off) - 1);
	assert(second.damage_scan_mode_is_overridden &&
	       second.damage_scan_mode_override == MS912X_DAMAGE_SCAN_OFF &&
	       second.damage_scan_device_config_generation == 1);
	assert(ms912x_damage_scan_store(&first_interface.dev, NULL, inherit,
					 sizeof(inherit) - 1) == sizeof(inherit) - 1);
	assert(!first.damage_scan_mode_is_overridden &&
	       first.damage_scan_device_config_generation == 2);

	assert(ms912x_damage_scan_tolerance_store(&first_interface.dev, NULL,
						 tolerance,
						 sizeof(tolerance) - 1) ==
	       sizeof(tolerance) - 1);
	assert(first.damage_scan_tolerance_is_overridden &&
	       first.damage_scan_tolerance_override_min == 3 &&
	       first.damage_scan_tolerance_override_max == 15 &&
	       first.damage_scan_device_config_generation == 3);
	assert(ms912x_damage_scan_effective_tolerance(&first) == 3);
	assert(ms912x_damage_scan_tolerance_show(&first_interface.dev, NULL, readback) == 5);
	assert(!strcmp(readback, "3-15\n"));
	first.present_queue_pressure_ns = 20;
	assert(ms912x_damage_scan_effective_tolerance(&first) == 7);
	first.present_queue_pressure_ns = 30;
	assert(ms912x_damage_scan_effective_tolerance(&first) == 15);
	assert(ms912x_damage_scan_effective_tolerance(&second) == 0);
	assert(ms912x_damage_scan_tolerance_store(&first_interface.dev, NULL,
						 tolerance,
						 sizeof(tolerance) - 1) ==
	       sizeof(tolerance) - 1);
	assert(first.damage_scan_device_config_generation == 3);
	assert(ms912x_damage_scan_tolerance_store(&second_interface.dev, NULL,
						 zero,
						 sizeof(zero) - 1) ==
	       sizeof(zero) - 1);
	assert(second.damage_scan_tolerance_is_overridden &&
	       second.damage_scan_tolerance_override_min == 0 &&
	       second.damage_scan_tolerance_override_max == 0 &&
	       second.damage_scan_device_config_generation == 2);
	assert(ms912x_damage_scan_tolerance_store(&first_interface.dev, NULL,
						 bad_tolerance,
						 sizeof(bad_tolerance) - 1) == -EINVAL);
	assert(first.damage_scan_device_config_generation == 3);
	assert(ms912x_damage_scan_tolerance_store(&first_interface.dev, NULL,
						 inherit,
						 sizeof(inherit) - 1) ==
	       sizeof(inherit) - 1);
	assert(!first.damage_scan_tolerance_is_overridden &&
	       first.damage_scan_device_config_generation == 4);
	assert(ms912x_damage_scan_effective_tolerance(&first) == 0);

	assert(ms912x_damage_scan_stride_store(&first_interface.dev, NULL,
					       stride, sizeof(stride) - 1) ==
	       sizeof(stride) - 1);
	assert(first.damage_scan_stride_is_overridden &&
	       first.damage_scan_stride_override_value == 4 &&
	       first.damage_scan_device_config_generation == 5);
	assert(ms912x_damage_scan_stride_store(&first_interface.dev, NULL,
					       bad_stride,
					       sizeof(bad_stride) - 1) == -EINVAL);
	assert(first.damage_scan_device_config_generation == 5);
	assert(ms912x_damage_scan_stride_store(&second_interface.dev, NULL,
					       stride_two,
					       sizeof(stride_two) - 1) ==
	       sizeof(stride_two) - 1);
	assert(second.damage_scan_stride_is_overridden &&
	       second.damage_scan_stride_override_value == 2 &&
	       second.damage_scan_device_config_generation == 3);
	assert(ms912x_damage_scan_stride_store(&first_interface.dev, NULL,
					       inherit,
					       sizeof(inherit) - 1) ==
	       sizeof(inherit) - 1);
	assert(!first.damage_scan_stride_is_overridden &&
	       first.damage_scan_device_config_generation == 6);
	assert(!first.damage_scan_lock && !second.damage_scan_lock);
	assert(ms912x_damage_scan_show(&second_interface.dev, NULL, readback) == 4);
	assert(!strcmp(readback, "off\n"));
	assert(ms912x_damage_scan_stride_show(&second_interface.dev, NULL, readback) == 2);
	assert(!strcmp(readback, "2\n"));
	assert(ms912x_damage_scan_tolerance_show(&second_interface.dev, NULL, readback) == 2);
	assert(!strcmp(readback, "0\n"));
	assert(ms912x_damage_scan_show(&first_interface.dev, NULL, readback) == 8);
	assert(!strcmp(readback, "inherit\n"));
	assert(ms912x_damage_scan_stride_show(&first_interface.dev, NULL, readback) == 8);
	assert(!strcmp(readback, "inherit\n"));
	assert(ms912x_damage_scan_tolerance_show(&first_interface.dev, NULL, readback) == 8);
	assert(!strcmp(readback, "inherit\n"));
	first_interface.driver_data = NULL;
	assert(ms912x_damage_scan_show(&first_interface.dev, NULL, readback) == -ENODEV);
	assert(ms912x_damage_scan_stride_show(&first_interface.dev, NULL, readback) == -ENODEV);
	assert(ms912x_damage_scan_tolerance_show(&first_interface.dev, NULL, readback) == -ENODEV);
	assert(settings_logs == 11);

	puts("PASS damage sysfs: mode, tolerance and stride updates remain per-adapter");
	return 0;
}
