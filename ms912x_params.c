// SPDX-License-Identifier: GPL-2.0-only

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/string.h>

#include <drm/drm_probe_helper.h>

#include "ms912x.h"

struct ms912x_parameters ms912x_parameters = {
	.damage_scan = MS912X_DAMAGE_SCAN_AUTO,
	.damage_scan_method = MS912X_DAMAGE_SCAN_MEMCMP,
	.damage_scan_stride = 4,
	.fast_chroma = 2,
	.fake_edid = MS912X_FAKE_EDID_AUTO,
	.force_connected = MS912X_FORCE_CONNECTED_AUTO,
	.debugfs_enabled = false,
	.vrr = true,
	.damage_scan_generation = ATOMIC_INIT(0),
	.damage_scan_config_generation = ATOMIC_INIT(0),
};

static LIST_HEAD(ms912x_devices);
static DEFINE_MUTEX(ms912x_devices_lock);

static void ms912x_notify_devices_hotplug(void)
{
	struct ms912x_device *ms912x;

	mutex_lock(&ms912x_devices_lock);
	list_for_each_entry(ms912x, &ms912x_devices, device_list) {
		WRITE_ONCE(ms912x->fake_edid_active, false);
		drm_kms_helper_hotplug_event(&ms912x->drm);
	}
	mutex_unlock(&ms912x_devices_lock);
}

void ms912x_device_registry_add(struct ms912x_device *ms912x)
{
	mutex_lock(&ms912x_devices_lock);
	list_add_tail(&ms912x->device_list, &ms912x_devices);
	mutex_unlock(&ms912x_devices_lock);
}

void ms912x_device_registry_remove(struct ms912x_device *ms912x)
{
	mutex_lock(&ms912x_devices_lock);
	list_del_init(&ms912x->device_list);
	mutex_unlock(&ms912x_devices_lock);
}

static int ms912x_debugfs_set(const char *value,
			      const struct kernel_param *kp)
{
	bool enabled;
	int ret = kstrtobool(value, &enabled);

	if (ret)
		return ret;
	WRITE_ONCE(*(bool *)kp->arg, enabled);
	return 0;
}

static int ms912x_debugfs_get(char *buffer, const struct kernel_param *kp)
{
	return scnprintf(buffer, PAGE_SIZE, "%s\n",
			 READ_ONCE(*(bool *)kp->arg) ? "on" : "off");
}

static const struct kernel_param_ops ms912x_debugfs_ops = {
	.set = ms912x_debugfs_set,
	.get = ms912x_debugfs_get,
};
module_param_cb(debugfs, &ms912x_debugfs_ops,
		&ms912x_parameters.debugfs_enabled, 0444);
MODULE_PARM_DESC(debugfs,
		 "Enable debugfs statistics (on/off; default: off; load-time only)");

static int ms912x_param_fast_chroma_set(const char *value,
				 const struct kernel_param *kp)
{
	unsigned int mode;
	int ret = kstrtouint(value, 0, &mode);

	if (ret)
		return ret;
	if (mode > 2)
		return -EINVAL;

	WRITE_ONCE(*(unsigned int *)kp->arg, mode);
	return 0;
}

static int ms912x_param_fast_chroma_get(char *buffer,
				 const struct kernel_param *kp)
{
	return scnprintf(buffer, PAGE_SIZE, "%u\n",
		 READ_ONCE(*(unsigned int *)kp->arg));
}

static const struct kernel_param_ops ms912x_param_fast_chroma_ops = {
	.set = ms912x_param_fast_chroma_set,
	.get = ms912x_param_fast_chroma_get,
};
module_param_cb(fast_chroma, &ms912x_param_fast_chroma_ops,
		&ms912x_parameters.fast_chroma, 0644);
MODULE_PARM_DESC(fast_chroma,
		 "Conversion mode: 0 scalar Q16, 1 SSE2/NEON, 2 AVX2/NEON (default: 2; runtime writable; unsupported SIMD falls back)");

static int ms912x_fake_edid_set(const char *value,
				const struct kernel_param *kp)
{
	enum ms912x_fake_edid_mode mode;

	if (sysfs_streq(value, "auto"))
		mode = MS912X_FAKE_EDID_AUTO;
	else if (sysfs_streq(value, "on") || sysfs_streq(value, "1") ||
		 sysfs_streq(value, "true"))
		mode = MS912X_FAKE_EDID_ON;
	else if (sysfs_streq(value, "off") || sysfs_streq(value, "0") ||
		 sysfs_streq(value, "false"))
		mode = MS912X_FAKE_EDID_OFF;
	else
		return -EINVAL;

	WRITE_ONCE(*(enum ms912x_fake_edid_mode *)kp->arg, mode);
	ms912x_notify_devices_hotplug();
	return 0;
}

static int ms912x_fake_edid_get(char *buffer,
				const struct kernel_param *kp)
{
	enum ms912x_fake_edid_mode mode =
		READ_ONCE(*(enum ms912x_fake_edid_mode *)kp->arg);
	const char *value;

	switch (mode) {
	case MS912X_FAKE_EDID_OFF:
		value = "off";
		break;
	case MS912X_FAKE_EDID_ON:
		value = "on";
		break;
	case MS912X_FAKE_EDID_AUTO:
	default:
		value = "auto";
		break;
	}

	return scnprintf(buffer, PAGE_SIZE, "%s\n", value);
}

static const struct kernel_param_ops ms912x_fake_edid_ops = {
	.set = ms912x_fake_edid_set,
	.get = ms912x_fake_edid_get,
};
module_param_cb(fake_edid, &ms912x_fake_edid_ops,
		&ms912x_parameters.fake_edid, 0644);
MODULE_PARM_DESC(fake_edid,
		 "EDID policy: auto falls back only for malformed EDID, on, or off (default: auto; runtime writable)");

static int ms912x_force_connected_set(const char *value,
				     const struct kernel_param *kp)
{
	enum ms912x_force_connected_mode mode;

	if (sysfs_streq(value, "auto"))
		mode = MS912X_FORCE_CONNECTED_AUTO;
	else if (sysfs_streq(value, "on") || sysfs_streq(value, "1") ||
		 sysfs_streq(value, "true"))
		mode = MS912X_FORCE_CONNECTED_ON;
	else if (sysfs_streq(value, "off") || sysfs_streq(value, "0") ||
		 sysfs_streq(value, "false"))
		mode = MS912X_FORCE_CONNECTED_OFF;
	else
		return -EINVAL;

	WRITE_ONCE(*(enum ms912x_force_connected_mode *)kp->arg, mode);
	ms912x_notify_devices_hotplug();
	return 0;
}

static int ms912x_force_connected_get(char *buffer,
				      const struct kernel_param *kp)
{
	enum ms912x_force_connected_mode mode =
		READ_ONCE(*(enum ms912x_force_connected_mode *)kp->arg);
	const char *value;

	switch (mode) {
	case MS912X_FORCE_CONNECTED_OFF:
		value = "off";
		break;
	case MS912X_FORCE_CONNECTED_ON:
		value = "on";
		break;
	case MS912X_FORCE_CONNECTED_AUTO:
	default:
		value = "auto";
		break;
	}

	return scnprintf(buffer, PAGE_SIZE, "%s\n", value);
}

static const struct kernel_param_ops ms912x_force_connected_ops = {
	.set = ms912x_force_connected_set,
	.get = ms912x_force_connected_get,
};
module_param_cb(force_connected, &ms912x_force_connected_ops,
		&ms912x_parameters.force_connected, 0644);
MODULE_PARM_DESC(force_connected,
		 "Connection policy: auto from status/EDID, on, or off (default: auto; runtime writable)");

module_param_named(vrr, ms912x_parameters.vrr, bool, 0444);
MODULE_PARM_DESC(vrr,
		 "Force the DRM connector vrr_capable property on (default: true; load-time only)");

const char *ms912x_param_damage_scan_mode_name(
	enum ms912x_damage_scan_mode mode);

static int ms912x_damage_scan_set(const char *value,
				 const struct kernel_param *kp)
{
	enum ms912x_damage_scan_mode mode;

	if (sysfs_streq(value, "auto"))
		mode = MS912X_DAMAGE_SCAN_AUTO;
	else if (sysfs_streq(value, "off"))
		mode = MS912X_DAMAGE_SCAN_OFF;
	else if (sysfs_streq(value, "on"))
		mode = MS912X_DAMAGE_SCAN_ON;
	else
		return -EINVAL;

	WRITE_ONCE(*(enum ms912x_damage_scan_mode *)kp->arg, mode);
	atomic_inc(&ms912x_parameters.damage_scan_generation);
	return 0;
}

static int ms912x_damage_scan_get(char *buffer,
				 const struct kernel_param *kp)
{
	enum ms912x_damage_scan_mode mode =
		READ_ONCE(*(enum ms912x_damage_scan_mode *)kp->arg);

	return scnprintf(buffer, PAGE_SIZE, "%s\n",
			 ms912x_param_damage_scan_mode_name(mode));
}

static const struct kernel_param_ops ms912x_damage_scan_ops = {
	.set = ms912x_damage_scan_set,
	.get = ms912x_damage_scan_get,
};
module_param_cb(damage_scan, &ms912x_damage_scan_ops,
		&ms912x_parameters.damage_scan, 0644);
MODULE_PARM_DESC(damage_scan,
		 "Software damage scan: auto, off, or on (runtime writable; default: auto)");

static int ms912x_param_damage_scan_method_set(const char *value,
					       const struct kernel_param *kp)
{
	enum ms912x_damage_scan_method method;
	enum ms912x_damage_scan_method old;

	if (sysfs_streq(value, "scalar") || sysfs_streq(value, "0"))
		method = MS912X_DAMAGE_SCAN_SCALAR;
	else if (sysfs_streq(value, "memcmp") || sysfs_streq(value, "1"))
		method = MS912X_DAMAGE_SCAN_MEMCMP;
	else
		return -EINVAL;

	old = READ_ONCE(*(enum ms912x_damage_scan_method *)kp->arg);
	if (old == method)
		return 0;
	WRITE_ONCE(*(enum ms912x_damage_scan_method *)kp->arg, method);
	atomic_inc(&ms912x_parameters.damage_scan_config_generation);
	return 0;
}

static int ms912x_param_damage_scan_method_get(char *buffer,
					       const struct kernel_param *kp)
{
	enum ms912x_damage_scan_method method =
		READ_ONCE(*(enum ms912x_damage_scan_method *)kp->arg);

	return scnprintf(buffer, PAGE_SIZE, "%s\n",
			 method == MS912X_DAMAGE_SCAN_SCALAR ? "scalar" : "memcmp");
}

static const struct kernel_param_ops ms912x_param_damage_scan_method_ops = {
	.set = ms912x_param_damage_scan_method_set,
	.get = ms912x_param_damage_scan_method_get,
};
module_param_cb(damage_scan_method, &ms912x_param_damage_scan_method_ops,
		&ms912x_parameters.damage_scan_method, 0644);
MODULE_PARM_DESC(damage_scan_method,
		 "Damage scan row method: scalar exact pixels or memcmp 128-byte blocks (default: memcmp; runtime writable)");

static int ms912x_param_damage_scan_stride_set(const char *value,
					      const struct kernel_param *kp)
{
	unsigned int stride, old;
	int ret = kstrtouint(value, 0, &stride);

	if (ret)
		return ret;
	if (stride < 1 || stride > 16)
		return -EINVAL;

	old = READ_ONCE(*(unsigned int *)kp->arg);
	if (old == stride)
		return 0;
	WRITE_ONCE(*(unsigned int *)kp->arg, stride);
	atomic_inc(&ms912x_parameters.damage_scan_config_generation);
	return 0;
}

static int ms912x_param_damage_scan_stride_get(char *buffer,
					      const struct kernel_param *kp)
{
	return scnprintf(buffer, PAGE_SIZE, "%u\n",
			 READ_ONCE(*(unsigned int *)kp->arg));
}

static const struct kernel_param_ops ms912x_param_damage_scan_stride_ops = {
	.set = ms912x_param_damage_scan_stride_set,
	.get = ms912x_param_damage_scan_stride_get,
};
module_param_cb(damage_scan_stride, &ms912x_param_damage_scan_stride_ops,
		&ms912x_parameters.damage_scan_stride, 0644);
MODULE_PARM_DESC(damage_scan_stride,
		 "Damage scan row stride, 1-16 (default: 4; runtime writable)");


const char *ms912x_param_damage_scan_mode_name(
					 enum ms912x_damage_scan_mode mode)
{
	switch (mode) {
	case MS912X_DAMAGE_SCAN_OFF:
		return "off";
	case MS912X_DAMAGE_SCAN_ON:
		return "on";
	case MS912X_DAMAGE_SCAN_AUTO:
	default:
		return "auto";
	}
}
