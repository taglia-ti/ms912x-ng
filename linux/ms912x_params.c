// SPDX-License-Identifier: GPL-2.0-only

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/sysfs.h>

#include <drm/drm_drv.h>
#include <drm/drm_probe_helper.h>

#include "ms912x.h"

struct ms912x_parameters ms912x_parameters = {
	.damage_scan_method = MS912X_DAMAGE_SCAN_METHOD_AUTO,
	.fast_chroma = MS912X_FAST_CHROMA_AUTO,
	.cursor_plane = true,
	.debugfs_enabled = false,
	.dbg_show_draws = false,
	.vrr = true,
	.damage_scan_config_generation = ATOMIC_INIT(0),
};

static LIST_HEAD(ms912x_devices);
static DEFINE_MUTEX(ms912x_devices_lock);
static DEFINE_MUTEX(ms912x_scale_lock);

enum ms912x_fake_edid_mode
ms912x_fake_edid_mode_for_device(struct ms912x_device *ms912x)
{
	return READ_ONCE(ms912x->fake_edid_mode);
}

enum ms912x_force_connected_mode
ms912x_force_connected_mode_for_device(struct ms912x_device *ms912x)
{
	return READ_ONCE(ms912x->force_connected_mode);
}

/* Retain DRM lifetime under the registry lock; callers run callbacks unlocked. */
static struct ms912x_device **ms912x_devices_snapshot(size_t *count)
{
	struct ms912x_device **devices;
	struct ms912x_device *ms912x;
	size_t capacity, count_now, i;

	for (;;) {
		mutex_lock(&ms912x_devices_lock);
		capacity = 0;
		list_for_each_entry(ms912x, &ms912x_devices, device_list)
			capacity++;
		mutex_unlock(&ms912x_devices_lock);

		if (!capacity) {
			*count = 0;
			return NULL;
		}
		devices = kcalloc(capacity, sizeof(*devices), GFP_KERNEL);
		if (!devices)
			return ERR_PTR(-ENOMEM);

		mutex_lock(&ms912x_devices_lock);
		count_now = 0;
		list_for_each_entry(ms912x, &ms912x_devices, device_list)
			count_now++;
		if (count_now > capacity) {
			mutex_unlock(&ms912x_devices_lock);
			kfree(devices);
			continue;
		}

		i = 0;
		list_for_each_entry(ms912x, &ms912x_devices, device_list) {
			drm_dev_get(&ms912x->drm);
			devices[i++] = ms912x;
		}
		mutex_unlock(&ms912x_devices_lock);
		*count = i;
		return devices;
	}
}

static void ms912x_refresh_devices_after_draws_off(void)
{
	struct ms912x_device **devices;
	size_t count, i;

	devices = ms912x_devices_snapshot(&count);
	if (IS_ERR(devices)) {
		pr_warn_ratelimited("ms912x: unable to snapshot adapters for full refresh\n");
		return;
	}
	for (i = 0; i < count; i++) {
		ms912x_transfer_force_full_update(devices[i]);
		drm_dev_put(&devices[i]->drm);
	}
	kfree(devices);
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

static int ms912x_dbg_show_draws_set(const char *value,
				     const struct kernel_param *kp)
{
	bool enabled;
	bool old;
	int ret = kstrtobool(value, &enabled);

	if (ret)
		return ret;
	old = READ_ONCE(*(bool *)kp->arg);
	if (old == enabled)
		return 0;
	WRITE_ONCE(*(bool *)kp->arg, enabled);
	if (old && !enabled)
		ms912x_refresh_devices_after_draws_off();
	return 0;
}

static int ms912x_dbg_show_draws_get(char *buffer,
				     const struct kernel_param *kp)
{
	return scnprintf(buffer, PAGE_SIZE, "%s\n",
			 READ_ONCE(*(bool *)kp->arg) ? "on" : "off");
}

static const struct kernel_param_ops ms912x_dbg_show_draws_ops = {
	.set = ms912x_dbg_show_draws_set,
	.get = ms912x_dbg_show_draws_get,
};
module_param_cb(dbg_show_draws, &ms912x_dbg_show_draws_ops,
		&ms912x_parameters.dbg_show_draws, 0644);
MODULE_PARM_DESC(dbg_show_draws,
		 "Draw an inverted 2-pixel UYVY border only around new base damage after conversion. The on-wire anti-flicker/Fibonacci union may be larger and its replayed area stays unmarked; disabling forces a clean full-screen refresh (default: false; runtime writable)");

static int ms912x_param_fast_chroma_set(const char *value,
					 const struct kernel_param *kp)
{
	enum ms912x_fast_chroma_backend mode;
	unsigned int numeric;
	int ret;

	if (sysfs_streq(value, "scalar") || sysfs_streq(value, "0"))
		mode = MS912X_FAST_CHROMA_SCALAR;
	else if (sysfs_streq(value, "auto"))
		mode = MS912X_FAST_CHROMA_AUTO;
	else if (sysfs_streq(value, "avx2")) {
#ifdef CONFIG_X86_64
		mode = MS912X_FAST_CHROMA_AVX2;
#else
		return -EINVAL;
#endif
	} else if (sysfs_streq(value, "ssse3")) {
#ifdef CONFIG_X86_64
		mode = MS912X_FAST_CHROMA_SSSE3;
#else
		return -EINVAL;
#endif
	} else if (sysfs_streq(value, "sse2")) {
#ifdef CONFIG_X86_64
		mode = MS912X_FAST_CHROMA_SSE2;
#else
		return -EINVAL;
#endif
	} else if (sysfs_streq(value, "neon")) {
#if defined(CONFIG_ARM64) || defined(CONFIG_ARM)
		mode = MS912X_FAST_CHROMA_NEON;
#else
		return -EINVAL;
#endif
	} else {
		ret = kstrtouint(value, 0, &numeric);
		if (ret)
			return ret;
		mode = numeric ? MS912X_FAST_CHROMA_AUTO :
			MS912X_FAST_CHROMA_SCALAR;
	}

	WRITE_ONCE(*(enum ms912x_fast_chroma_backend *)kp->arg, mode);
	return 0;
}

static int ms912x_param_fast_chroma_get(char *buffer,
				 const struct kernel_param *kp)
{
	switch (READ_ONCE(*(enum ms912x_fast_chroma_backend *)kp->arg)) {
	case MS912X_FAST_CHROMA_SCALAR:
		return scnprintf(buffer, PAGE_SIZE, "scalar\n");
	case MS912X_FAST_CHROMA_AVX2:
		return scnprintf(buffer, PAGE_SIZE, "avx2\n");
	case MS912X_FAST_CHROMA_NEON:
		return scnprintf(buffer, PAGE_SIZE, "neon\n");
	case MS912X_FAST_CHROMA_SSSE3:
		return scnprintf(buffer, PAGE_SIZE, "ssse3\n");
	case MS912X_FAST_CHROMA_SSE2:
		return scnprintf(buffer, PAGE_SIZE, "sse2\n");
	case MS912X_FAST_CHROMA_AUTO:
	default:
		return scnprintf(buffer, PAGE_SIZE, "auto\n");
	}
}

static const struct kernel_param_ops ms912x_param_fast_chroma_ops = {
	.set = ms912x_param_fast_chroma_set,
	.get = ms912x_param_fast_chroma_get,
};
module_param_cb(fast_chroma, &ms912x_param_fast_chroma_ops,
		&ms912x_parameters.fast_chroma, 0644);
MODULE_PARM_DESC(fast_chroma,
		 "Conversion backend: auto, avx2, ssse3, neon, sse2, or scalar; auto selects best available SIMD, explicit SIMD forces that backend on supported architectures; numeric 0=scalar, any positive value=auto (runtime writable)");


static int ms912x_cursor_plane_set(const char *value,
				   const struct kernel_param *kp)
{
	bool enabled;
	int ret;

	if (sysfs_streq(value, "on"))
		enabled = true;
	else if (sysfs_streq(value, "off"))
		enabled = false;
	else {
		ret = kstrtobool(value, &enabled);
		if (ret)
			return -EINVAL;
	}

	WRITE_ONCE(*(bool *)kp->arg, enabled);
	return 0;
}

static int ms912x_cursor_plane_get(char *buffer,
				   const struct kernel_param *kp)
{
	return scnprintf(buffer, PAGE_SIZE, "%s\n",
			 READ_ONCE(*(bool *)kp->arg) ? "on" : "off");
}

static const struct kernel_param_ops ms912x_cursor_plane_ops = {
	.set = ms912x_cursor_plane_set,
	.get = ms912x_cursor_plane_get,
};
module_param_cb(cursor_plane, &ms912x_cursor_plane_ops,
		&ms912x_parameters.cursor_plane, 0444);
MODULE_PARM_DESC(cursor_plane,
		 "Expose and render the cursor plane (on/off; default: on; load-time only)");

static int ms912x_fake_edid_parse(const char *value,
				  enum ms912x_fake_edid_mode *mode)
{
	if (sysfs_streq(value, "auto"))
		*mode = MS912X_FAKE_EDID_AUTO;
	else if (sysfs_streq(value, "on") || sysfs_streq(value, "y") ||
		 sysfs_streq(value, "Y") || sysfs_streq(value, "1") ||
		 sysfs_streq(value, "true"))
		*mode = MS912X_FAKE_EDID_ON;
	else if (sysfs_streq(value, "off") || sysfs_streq(value, "n") ||
		 sysfs_streq(value, "N") || sysfs_streq(value, "0") ||
		 sysfs_streq(value, "false"))
		*mode = MS912X_FAKE_EDID_OFF;
	else
		return -EINVAL;
	return 0;
}

static int ms912x_force_connected_parse(
				 const char *value,
				 enum ms912x_force_connected_mode *mode)
{
	if (sysfs_streq(value, "auto"))
		*mode = MS912X_FORCE_CONNECTED_AUTO;
	else if (sysfs_streq(value, "on") || sysfs_streq(value, "y") ||
		 sysfs_streq(value, "Y") || sysfs_streq(value, "1") ||
		 sysfs_streq(value, "true"))
		*mode = MS912X_FORCE_CONNECTED_ON;
	else if (sysfs_streq(value, "off") || sysfs_streq(value, "n") ||
		 sysfs_streq(value, "N") || sysfs_streq(value, "0") ||
		 sysfs_streq(value, "false"))
		*mode = MS912X_FORCE_CONNECTED_OFF;
	else
		return -EINVAL;
	return 0;
}

module_param_named(vrr, ms912x_parameters.vrr, bool, 0444);
MODULE_PARM_DESC(vrr,
		 "Force the DRM connector vrr_capable property on (default: true; load-time only)");

static int ms912x_scale_parse(const char *value, unsigned int *scale)
{
	if (sysfs_streq(value, "auto") || sysfs_streq(value, "on") ||
	    sysfs_streq(value, "1") || sysfs_streq(value, "y") ||
	    sysfs_streq(value, "Y"))
		*scale = MS912X_SCALE_AUTO;
	else if (sysfs_streq(value, "off") || sysfs_streq(value, "0") ||
		 sysfs_streq(value, "n") || sysfs_streq(value, "N"))
		*scale = MS912X_SCALE_100;
	else if (sysfs_streq(value, "50"))
		*scale = MS912X_SCALE_50;
	else if (sysfs_streq(value, "67"))
		*scale = MS912X_SCALE_67;
	else if (sysfs_streq(value, "80"))
		*scale = MS912X_SCALE_80;
	else if (sysfs_streq(value, "100"))
		*scale = MS912X_SCALE_100;
	else
		return -EINVAL;
	return 0;
}

static void ms912x_scale_setting_format(char *buffer, size_t size,
					unsigned int scale)
{
	if (scale == MS912X_SCALE_AUTO)
		strscpy(buffer, "auto", size);
	else
		scnprintf(buffer, size, "%u", scale);
}

void ms912x_scale_snapshot(struct ms912x_device *ms912x,
			   unsigned int *scale, u32 *device_generation)
{
	mutex_lock(&ms912x_scale_lock);
	*scale = READ_ONCE(ms912x->scale_setting);
	*device_generation = READ_ONCE(ms912x->scale_device_config_generation);
	mutex_unlock(&ms912x_scale_lock);
}

static int ms912x_param_damage_scan_method_set(const char *value,
					       const struct kernel_param *kp)
{
	enum ms912x_damage_scan_method method;
	enum ms912x_damage_scan_method old;

	if (sysfs_streq(value, "scalar") || sysfs_streq(value, "0"))
		method = MS912X_DAMAGE_SCAN_SCALAR;
	else if (sysfs_streq(value, "memcmp") || sysfs_streq(value, "1"))
		method = MS912X_DAMAGE_SCAN_MEMCMP;
	else if (sysfs_streq(value, "avx") || sysfs_streq(value, "avx1")) {
#ifdef CONFIG_X86_64
		method = MS912X_DAMAGE_SCAN_AVX;
#else
		return -EINVAL;
#endif
	} else if (sysfs_streq(value, "sse2")) {
#ifdef CONFIG_X86_64
		method = MS912X_DAMAGE_SCAN_SSE2;
#else
		return -EINVAL;
#endif
	} else if (sysfs_streq(value, "neon")) {
#if defined(CONFIG_ARM64) || defined(CONFIG_ARM)
		method = MS912X_DAMAGE_SCAN_NEON;
#else
		return -EINVAL;
#endif
	} else if (sysfs_streq(value, "auto"))
		method = MS912X_DAMAGE_SCAN_METHOD_AUTO;
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

	switch (method) {
	case MS912X_DAMAGE_SCAN_SCALAR:
		return scnprintf(buffer, PAGE_SIZE, "scalar\n");
	case MS912X_DAMAGE_SCAN_MEMCMP:
		return scnprintf(buffer, PAGE_SIZE, "memcmp\n");
	case MS912X_DAMAGE_SCAN_AVX:
		return scnprintf(buffer, PAGE_SIZE, "avx\n");
	case MS912X_DAMAGE_SCAN_SSE2:
		return scnprintf(buffer, PAGE_SIZE, "sse2\n");
	case MS912X_DAMAGE_SCAN_NEON:
		return scnprintf(buffer, PAGE_SIZE, "neon\n");
	case MS912X_DAMAGE_SCAN_METHOD_AUTO:
		return scnprintf(buffer, PAGE_SIZE, "auto\n");
	default:
		return -EINVAL;
	}
}

static const struct kernel_param_ops ms912x_param_damage_scan_method_ops = {
	.set = ms912x_param_damage_scan_method_set,
	.get = ms912x_param_damage_scan_method_get,
};

const char *ms912x_param_damage_scan_method_name(
	enum ms912x_damage_scan_method method)
{
	switch (method) {
	case MS912X_DAMAGE_SCAN_SCALAR:
		return "scalar";
	case MS912X_DAMAGE_SCAN_MEMCMP:
		return "memcmp";
	case MS912X_DAMAGE_SCAN_AVX:
		return "avx";
	case MS912X_DAMAGE_SCAN_NEON:
		return "neon";
	case MS912X_DAMAGE_SCAN_SSE2:
		return "sse2";
	case MS912X_DAMAGE_SCAN_METHOD_AUTO:
	default:
		return "auto";
	}
}
module_param_cb(damage_scan_method, &ms912x_param_damage_scan_method_ops,
		&ms912x_parameters.damage_scan_method, 0644);
MODULE_PARM_DESC(damage_scan_method,
		 "Damage scan method: auto, avx (alias avx1), neon, sse2, memcmp, or scalar; explicit SIMD methods force that backend on supported architectures; auto selects AVX, NEON, SSE2, memcmp, then scalar (runtime writable)");

const char *ms912x_damage_scan_mode_name(
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

static struct ms912x_device *ms912x_device_from_sysfs(struct device *dev)
{
	struct usb_interface *intf = to_usb_interface(dev);

	return usb_get_intfdata(intf);
}

static void ms912x_log_settings(struct device *dev,
				struct ms912x_device *ms912x)
{
	char scale[8];

	ms912x_scale_setting_format(scale, sizeof(scale),
		READ_ONCE(ms912x->scale_setting));
	dev_info(dev, "damage_scan=%s stride=%u tolerance=%u-%u scale=%s\n",
		ms912x_damage_scan_mode_name(
			READ_ONCE(ms912x->damage_scan_mode)),
		READ_ONCE(ms912x->damage_scan_stride),
		READ_ONCE(ms912x->damage_scan_tolerance_min),
		READ_ONCE(ms912x->damage_scan_tolerance_max),
		scale);
}

static ssize_t ms912x_scale_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct ms912x_device *ms912x = ms912x_device_from_sysfs(dev);
	char setting[8];
	ssize_t result;

	if (!ms912x)
		return -ENODEV;
	mutex_lock(&ms912x_scale_lock);
	ms912x_scale_setting_format(setting, sizeof(setting),
				    READ_ONCE(ms912x->scale_setting));
	result = sysfs_emit(buf, "%s\n", setting);
	mutex_unlock(&ms912x_scale_lock);
	return result;
}

static ssize_t ms912x_scale_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct ms912x_device *ms912x = ms912x_device_from_sysfs(dev);
	unsigned int scale = MS912X_SCALE_100;
	bool changed = false;
	int ret = 0;

	if (!ms912x)
		return -ENODEV;
	ret = ms912x_scale_parse(buf, &scale);
	if (ret)
		return ret;

	mutex_lock(&ms912x_scale_lock);
	if (READ_ONCE(ms912x->scale_setting) != scale) {
		WRITE_ONCE(ms912x->scale_setting, scale);
		WRITE_ONCE(ms912x->scale_device_config_generation,
			   READ_ONCE(ms912x->scale_device_config_generation) + 1);
		changed = true;
	}
	mutex_unlock(&ms912x_scale_lock);
	if (changed) {
		drm_kms_helper_hotplug_event(&ms912x->drm);
		ms912x_log_settings(dev, ms912x);
	}
	return count;
}
static DEVICE_ATTR_RW(ms912x_scale);

const char *ms912x_fake_edid_mode_name(
	enum ms912x_fake_edid_mode mode)
{
	switch (mode) {
	case MS912X_FAKE_EDID_OFF:
		return "off";
	case MS912X_FAKE_EDID_ON:
		return "on";
	case MS912X_FAKE_EDID_AUTO:
	default:
		return "auto";
	}
}

static ssize_t ms912x_fake_edid_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct ms912x_device *ms912x = ms912x_device_from_sysfs(dev);
	ssize_t result;

	if (!ms912x)
		return -ENODEV;
	mutex_lock(&ms912x->detect_lock);
	result = sysfs_emit(buf, "%s\n",
			  ms912x_fake_edid_mode_name(
				  READ_ONCE(ms912x->fake_edid_mode)));
	mutex_unlock(&ms912x->detect_lock);
	return result;
}

static ssize_t ms912x_fake_edid_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct ms912x_device *ms912x = ms912x_device_from_sysfs(dev);
	enum ms912x_fake_edid_mode mode = MS912X_FAKE_EDID_AUTO;
	bool changed = false;
	int ret = 0;

	if (!ms912x)
		return -ENODEV;
	ret = ms912x_fake_edid_parse(buf, &mode);
	if (ret)
		return ret;

	mutex_lock(&ms912x->detect_lock);
	if (READ_ONCE(ms912x->fake_edid_mode) != mode) {
		WRITE_ONCE(ms912x->fake_edid_mode, mode);
		WRITE_ONCE(ms912x->fake_edid_active, false);
		ms912x->detect_status_updated =
			jiffies - msecs_to_jiffies(250);
		mutex_lock(&ms912x->edid_lock);
		memset(ms912x->edid_cache_valid, 0,
		       sizeof(ms912x->edid_cache_valid));
		mutex_unlock(&ms912x->edid_lock);
		changed = true;
	}
	mutex_unlock(&ms912x->detect_lock);
	if (changed) {
		drm_kms_helper_hotplug_event(&ms912x->drm);
		dev_info(dev, "fake_edid=%s source=adapter\n",
			 ms912x_fake_edid_mode_name(mode));
	}
	return count;
}
static DEVICE_ATTR_RW(ms912x_fake_edid);

const char *ms912x_force_connected_mode_name(
	enum ms912x_force_connected_mode mode)
{
	switch (mode) {
	case MS912X_FORCE_CONNECTED_OFF:
		return "off";
	case MS912X_FORCE_CONNECTED_ON:
		return "on";
	case MS912X_FORCE_CONNECTED_AUTO:
	default:
		return "auto";
	}
}

static ssize_t ms912x_force_connected_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct ms912x_device *ms912x = ms912x_device_from_sysfs(dev);
	ssize_t result;

	if (!ms912x)
		return -ENODEV;
	mutex_lock(&ms912x->detect_lock);
	result = sysfs_emit(buf, "%s\n",
			  ms912x_force_connected_mode_name(
				  READ_ONCE(ms912x->force_connected_mode)));
	mutex_unlock(&ms912x->detect_lock);
	return result;
}

static ssize_t ms912x_force_connected_store(struct device *dev,
					    struct device_attribute *attr,
					    const char *buf, size_t count)
{
	struct ms912x_device *ms912x = ms912x_device_from_sysfs(dev);
	enum ms912x_force_connected_mode mode = MS912X_FORCE_CONNECTED_AUTO;
	bool changed = false;
	int ret = 0;

	if (!ms912x)
		return -ENODEV;
	ret = ms912x_force_connected_parse(buf, &mode);
	if (ret)
		return ret;

	mutex_lock(&ms912x->detect_lock);
	if (READ_ONCE(ms912x->force_connected_mode) != mode) {
		WRITE_ONCE(ms912x->force_connected_mode, mode);
		ms912x->detect_status_updated =
			jiffies - msecs_to_jiffies(250);
		changed = true;
	}
	mutex_unlock(&ms912x->detect_lock);
	if (changed) {
		drm_kms_helper_hotplug_event(&ms912x->drm);
		dev_info(dev, "force_connected=%s source=adapter\n",
			 ms912x_force_connected_mode_name(mode));
	}
	return count;
}
static DEVICE_ATTR_RW(ms912x_force_connected);

static int ms912x_scan_mode_parse(const char *buf,
				  enum ms912x_damage_scan_mode *mode)
{
	if (sysfs_streq(buf, "auto"))
		*mode = MS912X_DAMAGE_SCAN_AUTO;
	else if (sysfs_streq(buf, "off"))
		*mode = MS912X_DAMAGE_SCAN_OFF;
	else if (sysfs_streq(buf, "on"))
		*mode = MS912X_DAMAGE_SCAN_ON;
	else
		return -EINVAL;
	return 0;
}

static ssize_t ms912x_damage_scan_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct ms912x_device *ms912x = ms912x_device_from_sysfs(dev);
	enum ms912x_damage_scan_mode mode;
	ssize_t result;

	if (!ms912x)
		return -ENODEV;
	mutex_lock(&ms912x->damage_scan_lock);
	mode = READ_ONCE(ms912x->damage_scan_mode);
	result = sysfs_emit(buf, "%s\n",
			  ms912x_damage_scan_mode_name(mode));
	mutex_unlock(&ms912x->damage_scan_lock);
	return result;
}

static ssize_t ms912x_damage_scan_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct ms912x_device *ms912x = ms912x_device_from_sysfs(dev);
	enum ms912x_damage_scan_mode mode = MS912X_DAMAGE_SCAN_AUTO;
	int ret = 0;

	if (!ms912x)
		return -ENODEV;
	ret = ms912x_scan_mode_parse(buf, &mode);
	if (ret)
		return ret;

	mutex_lock(&ms912x->damage_scan_lock);
	if (READ_ONCE(ms912x->damage_scan_mode) != mode) {
		WRITE_ONCE(ms912x->damage_scan_mode, mode);
		ms912x->damage_scan_device_config_generation++;
	}
	mutex_unlock(&ms912x->damage_scan_lock);
	ms912x_log_settings(dev, ms912x);
	return count;
}
static DEVICE_ATTR_RW(ms912x_damage_scan);

static int ms912x_tolerance_parse(const char *buf, unsigned int *minimum,
				  unsigned int *maximum)
{
	const char *separator = strchr(buf, '-');
	char *minimum_text;
	int ret;

	if (separator) {
		if (strchr(separator + 1, '-'))
			return -EINVAL;
		minimum_text = kstrndup(buf, separator - buf, GFP_KERNEL);
		if (!minimum_text)
			return -ENOMEM;
		ret = kstrtouint(minimum_text, 0, minimum);
		kfree(minimum_text);
		if (ret)
			return ret;
		ret = kstrtouint(separator + 1, 0, maximum);
		if (ret)
			return ret;
	} else {
		ret = kstrtouint(buf, 0, minimum);
		if (ret)
			return ret;
		*maximum = *minimum;
	}
	if (*minimum > MS912X_DAMAGE_SCAN_TOLERANCE_MAX ||
	    *maximum > MS912X_DAMAGE_SCAN_TOLERANCE_MAX || *minimum > *maximum)
		return -EINVAL;
	*minimum = ms912x_damage_scan_tolerance_bucket(*minimum);
	*maximum = ms912x_damage_scan_tolerance_bucket(*maximum);
	return 0;
}

static ssize_t ms912x_damage_scan_tolerance_show(struct device *dev,
						 struct device_attribute *attr,
						 char *buf)
{
	struct ms912x_device *ms912x = ms912x_device_from_sysfs(dev);
	unsigned int minimum, maximum;
	ssize_t result;

	if (!ms912x)
		return -ENODEV;
	mutex_lock(&ms912x->damage_scan_lock);
	minimum = READ_ONCE(ms912x->damage_scan_tolerance_min);
	maximum = READ_ONCE(ms912x->damage_scan_tolerance_max);
	if (minimum == maximum)
		result = sysfs_emit(buf, "%u\n", minimum);
	else
		result = sysfs_emit(buf, "%u-%u\n", minimum, maximum);
	mutex_unlock(&ms912x->damage_scan_lock);
	return result;
}

static ssize_t ms912x_damage_scan_tolerance_store(struct device *dev,
						  struct device_attribute *attr,
						  const char *buf,
						  size_t count)
{
	struct ms912x_device *ms912x = ms912x_device_from_sysfs(dev);
	unsigned int minimum = 0, maximum = 0;
	int ret = 0;

	if (!ms912x)
		return -ENODEV;
	ret = ms912x_tolerance_parse(buf, &minimum, &maximum);
	if (ret)
		return ret;

	mutex_lock(&ms912x->damage_scan_lock);
	if (READ_ONCE(ms912x->damage_scan_tolerance_min) != minimum ||
	    READ_ONCE(ms912x->damage_scan_tolerance_max) != maximum) {
		WRITE_ONCE(ms912x->damage_scan_tolerance_min, minimum);
		WRITE_ONCE(ms912x->damage_scan_tolerance_max, maximum);
		ms912x->damage_scan_device_config_generation++;
	}
	mutex_unlock(&ms912x->damage_scan_lock);
	ms912x_log_settings(dev, ms912x);
	return count;
}
static DEVICE_ATTR_RW(ms912x_damage_scan_tolerance);

static ssize_t ms912x_damage_scan_stride_show(struct device *dev,
					      struct device_attribute *attr,
					      char *buf)
{
	struct ms912x_device *ms912x = ms912x_device_from_sysfs(dev);
	ssize_t result;

	if (!ms912x)
		return -ENODEV;
	mutex_lock(&ms912x->damage_scan_lock);
	result = sysfs_emit(buf, "%u\n",
			  READ_ONCE(ms912x->damage_scan_stride));
	mutex_unlock(&ms912x->damage_scan_lock);
	return result;
}

static ssize_t ms912x_damage_scan_stride_store(struct device *dev,
					       struct device_attribute *attr,
					       const char *buf, size_t count)
{
	struct ms912x_device *ms912x = ms912x_device_from_sysfs(dev);
	unsigned int stride = 0;
	int ret = 0;

	if (!ms912x)
		return -ENODEV;
	ret = kstrtouint(buf, 0, &stride);
	if (ret)
		return ret;
	if (stride < 1 || stride > 16)
		return -EINVAL;

	mutex_lock(&ms912x->damage_scan_lock);
	if (READ_ONCE(ms912x->damage_scan_stride) != stride) {
		WRITE_ONCE(ms912x->damage_scan_stride, stride);
		ms912x->damage_scan_device_config_generation++;
	}
	mutex_unlock(&ms912x->damage_scan_lock);
	ms912x_log_settings(dev, ms912x);
	return count;
}
static DEVICE_ATTR_RW(ms912x_damage_scan_stride);

static struct attribute *ms912x_device_attrs[] = {
	&dev_attr_ms912x_scale.attr,
	&dev_attr_ms912x_fake_edid.attr,
	&dev_attr_ms912x_force_connected.attr,
	&dev_attr_ms912x_damage_scan.attr,
	&dev_attr_ms912x_damage_scan_stride.attr,
	&dev_attr_ms912x_damage_scan_tolerance.attr,
	NULL,
};

static const struct attribute_group ms912x_device_attr_group = {
	.attrs = ms912x_device_attrs,
};

/* The driver core creates these after probe and drains them before disconnect. */
const struct attribute_group *ms912x_device_groups[] = {
	&ms912x_device_attr_group,
	NULL,
};
