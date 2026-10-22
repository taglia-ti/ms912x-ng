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
	.profile = MS912X_PROFILE_AUTO,
	.effective_profile = MS912X_PROFILE_AUTO,
	.damage_scan = MS912X_DAMAGE_SCAN_AUTO,
	.scale = MS912X_SCALE_100,
	.damage_scan_method = MS912X_DAMAGE_SCAN_METHOD_AUTO,
	.damage_scan_stride = 1,
	.damage_scan_tolerance = 1,
	.damage_scan_tolerance_max = 31,
	.damage_scan_override = false,
	.damage_scan_stride_override = false,
	.damage_scan_tolerance_override = false,
	.fast_chroma = MS912X_FAST_CHROMA_AUTO,
	.primary_format = MS912X_PRIMARY_FORMAT_QUALITY,
	.cursor_plane = true,
	.fake_edid = MS912X_FAKE_EDID_AUTO,
	.force_connected = MS912X_FORCE_CONNECTED_AUTO,
	.debugfs_enabled = false,
	.dbg_show_draws = false,
	.vrr = true,
	.damage_scan_generation = ATOMIC_INIT(0),
	.damage_scan_config_generation = ATOMIC_INIT(0),
	.scale_generation = ATOMIC_INIT(0),
};

static LIST_HEAD(ms912x_devices);
static DEFINE_MUTEX(ms912x_devices_lock);
static DEFINE_MUTEX(ms912x_profile_lock);
static DEFINE_MUTEX(ms912x_scale_lock);
static bool ms912x_profile_initialized;
static bool ms912x_auto_profile_resolved;
static enum ms912x_profile ms912x_auto_profile = MS912X_PROFILE_EFFICIENCY;

static void ms912x_scale_setting_format(char *buffer, size_t size,
					unsigned int scale);

const char *ms912x_profile_name(enum ms912x_profile profile)
{
	switch (profile) {
	case MS912X_PROFILE_EFFICIENCY:
		return "efficiency";
	case MS912X_PROFILE_LATENCY:
		return "latency";
	case MS912X_PROFILE_QUALITY:
		return "quality";
	case MS912X_PROFILE_AUTO:
	default:
		return "auto";
	}
}

enum ms912x_fake_edid_mode
ms912x_fake_edid_mode_for_device(struct ms912x_device *ms912x)
{
	return READ_ONCE(ms912x->fake_edid_override) ?
		READ_ONCE(ms912x->fake_edid_mode) :
		READ_ONCE(ms912x_parameters.fake_edid);
}

enum ms912x_force_connected_mode
ms912x_force_connected_mode_for_device(struct ms912x_device *ms912x)
{
	return READ_ONCE(ms912x->force_connected_override) ?
		READ_ONCE(ms912x->force_connected_mode) :
		READ_ONCE(ms912x_parameters.force_connected);
}

static enum ms912x_profile
ms912x_profile_for_usb_speed(enum usb_device_speed speed)
{
	return ms912x_profile_auto_select(IS_ENABLED(CONFIG_X86),
					 speed >= USB_SPEED_SUPER);
}

static enum ms912x_profile ms912x_profile_effective_locked(void)
{
	enum ms912x_profile profile = READ_ONCE(ms912x_parameters.profile);

	if (profile == MS912X_PROFILE_AUTO)
		return ms912x_auto_profile;
	return profile;
}

static enum ms912x_profile
ms912x_profile_for_device_locked(const struct ms912x_device *ms912x)
{
	enum ms912x_profile requested = READ_ONCE(ms912x->profile_override) ?
		READ_ONCE(ms912x->profile_requested) :
		READ_ONCE(ms912x_parameters.profile);

	if (requested == MS912X_PROFILE_AUTO)
		return ms912x_profile_for_usb_speed(ms912x->usb_speed);
	return requested;
}

static void ms912x_profile_configure_device_locked(
	struct ms912x_device *ms912x)
{
	struct ms912x_profile_values values;
	enum ms912x_profile profile =
		ms912x_profile_for_device_locked(ms912x);
	enum ms912x_profile old_profile;
	enum ms912x_damage_scan_mode old_scan;
	unsigned int old_stride;

	values = ms912x_profile_values_for(profile);
	mutex_lock(&ms912x->damage_scan_lock);
	old_profile = READ_ONCE(ms912x->effective_profile);
	old_scan = READ_ONCE(ms912x->damage_scan_mode_default);
	old_stride = READ_ONCE(ms912x->damage_scan_stride_default);
	WRITE_ONCE(ms912x->effective_profile, profile);
	WRITE_ONCE(ms912x->damage_scan_mode_default, values.damage_scan);
	WRITE_ONCE(ms912x->damage_scan_stride_default, values.stride);
	if (old_scan != values.damage_scan)
		ms912x->damage_scan_device_config_generation++;
	if (old_profile != profile || old_stride != values.stride)
		ms912x->damage_scan_device_config_generation++;
	mutex_unlock(&ms912x->damage_scan_lock);
}

static void ms912x_profile_apply_locked(enum ms912x_profile profile)
{
	struct ms912x_profile_values values =
		ms912x_profile_values_for(profile);
	enum ms912x_damage_scan_mode old_scan =
		READ_ONCE(ms912x_parameters.damage_scan);
	unsigned int old_stride = READ_ONCE(ms912x_parameters.damage_scan_stride);
	unsigned int old_tolerance =
		READ_ONCE(ms912x_parameters.damage_scan_tolerance);
	unsigned int old_tolerance_max =
		READ_ONCE(ms912x_parameters.damage_scan_tolerance_max);
	struct ms912x_device *ms912x;
	bool scan_changed, config_changed;

	if (!READ_ONCE(ms912x_parameters.damage_scan_override))
		WRITE_ONCE(ms912x_parameters.damage_scan, values.damage_scan);
	if (!READ_ONCE(ms912x_parameters.damage_scan_stride_override))
		WRITE_ONCE(ms912x_parameters.damage_scan_stride, values.stride);
	if (!READ_ONCE(ms912x_parameters.damage_scan_tolerance_override)) {
		WRITE_ONCE(ms912x_parameters.damage_scan_tolerance,
			   values.tolerance_min);
		WRITE_ONCE(ms912x_parameters.damage_scan_tolerance_max,
			   values.tolerance_max);
	}
	WRITE_ONCE(ms912x_parameters.effective_profile, profile);
	scan_changed = old_scan != READ_ONCE(ms912x_parameters.damage_scan);
	config_changed =
		old_stride != READ_ONCE(ms912x_parameters.damage_scan_stride) ||
		old_tolerance !=
			READ_ONCE(ms912x_parameters.damage_scan_tolerance) ||
		old_tolerance_max !=
			READ_ONCE(ms912x_parameters.damage_scan_tolerance_max);
	if (scan_changed)
		atomic_inc(&ms912x_parameters.damage_scan_generation);
	if (config_changed)
		atomic_inc(&ms912x_parameters.damage_scan_config_generation);

	mutex_lock(&ms912x_devices_lock);
	list_for_each_entry(ms912x, &ms912x_devices, device_list)
		ms912x_profile_configure_device_locked(ms912x);
	mutex_unlock(&ms912x_devices_lock);
}

static void ms912x_profile_log_settings(struct device *dev,
					struct ms912x_device *ms912x);

static int ms912x_profile_parse(const char *value,
				enum ms912x_profile *profile)
{
	if (sysfs_streq(value, "auto"))
		*profile = MS912X_PROFILE_AUTO;
	else if (sysfs_streq(value, "efficiency"))
		*profile = MS912X_PROFILE_EFFICIENCY;
	else if (sysfs_streq(value, "latency"))
		*profile = MS912X_PROFILE_LATENCY;
	else if (sysfs_streq(value, "quality"))
		*profile = MS912X_PROFILE_QUALITY;
	else
		return -EINVAL;
	return 0;
}

static void ms912x_profile_update(enum ms912x_profile profile)
{
	bool initialized;
	enum ms912x_profile old_profile;

	mutex_lock(&ms912x_profile_lock);
	old_profile = READ_ONCE(ms912x_parameters.profile);
	WRITE_ONCE(ms912x_parameters.profile, profile);
	initialized = ms912x_profile_initialized;
	if (initialized) {
		ms912x_profile_apply_locked(ms912x_profile_effective_locked());
		if (old_profile != profile) {
			atomic_inc(&ms912x_parameters.damage_scan_generation);
			atomic_inc(&ms912x_parameters.damage_scan_config_generation);
		}
	} else {
		WRITE_ONCE(ms912x_parameters.damage_scan_override, false);
		WRITE_ONCE(ms912x_parameters.damage_scan_stride_override, false);
		WRITE_ONCE(ms912x_parameters.damage_scan_tolerance_override,
			   false);
	}
	mutex_unlock(&ms912x_profile_lock);
	if (initialized)
		ms912x_profile_log_settings(NULL, NULL);
}

/* Restore one scan parameter to the currently selected profile. */
static void ms912x_profile_restore_default(bool *override)
{
	bool initialized;
	bool was_override;

	mutex_lock(&ms912x_profile_lock);
	was_override = READ_ONCE(*override);
	WRITE_ONCE(*override, false);
	initialized = ms912x_profile_initialized;
	if (initialized) {
		ms912x_profile_apply_locked(ms912x_profile_effective_locked());
		if (was_override) {
			atomic_inc(&ms912x_parameters.damage_scan_generation);
			atomic_inc(&ms912x_parameters.damage_scan_config_generation);
		}
	}
	mutex_unlock(&ms912x_profile_lock);
	if (initialized)
		ms912x_profile_log_settings(NULL, NULL);
}

static void ms912x_profile_log_settings(struct device *dev,
					struct ms912x_device *ms912x)
{
	enum ms912x_profile requested =
		ms912x && READ_ONCE(ms912x->profile_override) ?
		READ_ONCE(ms912x->profile_requested) :
		READ_ONCE(ms912x_parameters.profile);
	enum ms912x_profile effective =
		ms912x ? READ_ONCE(ms912x->effective_profile) :
		READ_ONCE(ms912x_parameters.effective_profile);
	enum ms912x_damage_scan_mode scan =
		ms912x && READ_ONCE(ms912x->damage_scan_mode_is_overridden) ?
		READ_ONCE(ms912x->damage_scan_mode_override) :
		ms912x && !READ_ONCE(ms912x_parameters.damage_scan_override) ?
		READ_ONCE(ms912x->damage_scan_mode_default) :
		READ_ONCE(ms912x_parameters.damage_scan);
	struct ms912x_profile_values profile_values =
		ms912x_profile_values_for_format(effective,
			ms912x ? READ_ONCE(ms912x->video_in_rgb565) : 0);
	unsigned int stride =
		ms912x && READ_ONCE(ms912x->damage_scan_stride_is_overridden) ?
		READ_ONCE(ms912x->damage_scan_stride_override_value) :
		ms912x && !READ_ONCE(ms912x_parameters.damage_scan_stride_override) ?
		READ_ONCE(ms912x->damage_scan_stride_default) :
		READ_ONCE(ms912x_parameters.damage_scan_stride);
	unsigned int tolerance_min =
		ms912x && READ_ONCE(ms912x->damage_scan_tolerance_is_overridden) ?
		READ_ONCE(ms912x->damage_scan_tolerance_override_min) :
		ms912x && !READ_ONCE(ms912x_parameters.damage_scan_tolerance_override) ?
		profile_values.tolerance_min :
		READ_ONCE(ms912x_parameters.damage_scan_tolerance);
	unsigned int tolerance_max =
		ms912x && READ_ONCE(ms912x->damage_scan_tolerance_is_overridden) ?
		READ_ONCE(ms912x->damage_scan_tolerance_override_max) :
		ms912x && !READ_ONCE(ms912x_parameters.damage_scan_tolerance_override) ?
		profile_values.tolerance_max :
		READ_ONCE(ms912x_parameters.damage_scan_tolerance_max);
	unsigned int scale =
		ms912x && READ_ONCE(ms912x->scale_is_overridden) ?
		READ_ONCE(ms912x->scale_override_setting) :
		READ_ONCE(ms912x_parameters.scale);
	const char *scale_source =
		ms912x && READ_ONCE(ms912x->scale_is_overridden) ?
		"adapter" : "module";
	char scale_name[8];

	ms912x_scale_setting_format(scale_name, sizeof(scale_name), scale);

	if (dev)
		dev_info(dev,
			 "profile=%s profile_source=%s effective=%s damage_scan=%s stride=%u tolerance=%u-%u scale=%s scale_source=%s\n",
			 ms912x_profile_name(requested),
			 ms912x && READ_ONCE(ms912x->profile_override) ?
			 "adapter" : "module",
			 ms912x_profile_name(effective),
			 ms912x_param_damage_scan_mode_name(scan), stride,
			 tolerance_min, tolerance_max, scale_name, scale_source);
	else
		pr_info("ms912x: profile=%s effective=%s damage_scan=%s stride=%u tolerance=%u-%u scale=%s\n",
			ms912x_profile_name(requested),
			ms912x_profile_name(effective),
			ms912x_param_damage_scan_mode_name(scan), stride,
			tolerance_min, tolerance_max, scale_name);
}

void ms912x_profile_configure_device(struct ms912x_device *ms912x,
				     enum usb_device_speed speed)
{
	enum ms912x_profile effective;

	mutex_lock(&ms912x_profile_lock);
	ms912x->usb_speed = speed;
	if (!ms912x_auto_profile_resolved) {
		ms912x_auto_profile = ms912x_profile_for_usb_speed(speed);
		ms912x_auto_profile_resolved = true;
	}

	effective = ms912x_profile_effective_locked();
	ms912x_profile_apply_locked(effective);
	ms912x_profile_initialized = true;
	ms912x_profile_configure_device_locked(ms912x);
	mutex_unlock(&ms912x_profile_lock);

	ms912x_profile_log_settings(&ms912x->intf->dev, ms912x);
}

static int ms912x_profile_set(const char *value,
			      const struct kernel_param *kp)
{
	enum ms912x_profile profile;
	int ret = ms912x_profile_parse(value, &profile);

	if (ret)
		return ret;
	ms912x_profile_update(profile);
	return 0;
}

static int ms912x_profile_get(char *buffer, const struct kernel_param *kp)
{
	return scnprintf(buffer, PAGE_SIZE, "%s\n",
			 ms912x_profile_name(
				 READ_ONCE(*(enum ms912x_profile *)kp->arg)));
}

static const struct kernel_param_ops ms912x_profile_ops = {
	.set = ms912x_profile_set,
	.get = ms912x_profile_get,
};
module_param_cb(profile, &ms912x_profile_ops, &ms912x_parameters.profile,
		0644);
MODULE_PARM_DESC(profile,
		 "Scan profile: auto (architecture and each adapter's USB link), efficiency, latency, or quality; RGB565 latency uses tolerance 3-7, XRGB8888 latency uses 3-15; explicit scan parameter values override profile defaults (runtime writable)");

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

static void ms912x_notify_devices_hotplug(void)
{
	struct ms912x_device **devices;
	size_t count, i;

	devices = ms912x_devices_snapshot(&count);
	if (IS_ERR(devices)) {
		pr_warn_ratelimited("ms912x: unable to snapshot adapters for hotplug notification\n");
		return;
	}
	for (i = 0; i < count; i++) {
		int idx;

		if (drm_dev_enter(&devices[i]->drm, &idx)) {
			WRITE_ONCE(devices[i]->fake_edid_active, false);
			drm_kms_helper_hotplug_event(&devices[i]->drm);
			drm_dev_exit(idx);
		}
		drm_dev_put(&devices[i]->drm);
	}
	kfree(devices);
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
	mutex_lock(&ms912x_profile_lock);
	mutex_lock(&ms912x_devices_lock);
	ms912x_profile_configure_device_locked(ms912x);
	list_add_tail(&ms912x->device_list, &ms912x_devices);
	mutex_unlock(&ms912x_devices_lock);
	mutex_unlock(&ms912x_profile_lock);
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

static int ms912x_primary_format_set(const char *value,
				     const struct kernel_param *kp)
{
	enum ms912x_primary_format mode;

	if (sysfs_streq(value, "rgb565"))
		mode = MS912X_PRIMARY_FORMAT_RGB565;
	else if (sysfs_streq(value, "xrgb8888") || sysfs_streq(value, "xrgb"))
		mode = MS912X_PRIMARY_FORMAT_XRGB8888;
	else if (sysfs_streq(value, "quality"))
		mode = MS912X_PRIMARY_FORMAT_QUALITY;
	else if (sysfs_streq(value, "efficiency"))
		mode = MS912X_PRIMARY_FORMAT_EFFICIENCY;
	else
		return -EINVAL;

	*(enum ms912x_primary_format *)kp->arg = mode;
	return 0;
}

static int ms912x_primary_format_get(char *buffer,
				     const struct kernel_param *kp)
{
	const char *name;

	switch (*(enum ms912x_primary_format *)kp->arg) {
	case MS912X_PRIMARY_FORMAT_RGB565:
		name = "rgb565";
		break;
	case MS912X_PRIMARY_FORMAT_XRGB8888:
		name = "xrgb8888";
		break;
	case MS912X_PRIMARY_FORMAT_EFFICIENCY:
		name = "efficiency";
		break;
	case MS912X_PRIMARY_FORMAT_QUALITY:
	default:
		name = "quality";
		break;
	}
	return scnprintf(buffer, PAGE_SIZE, "%s\n", name);
}

static const struct kernel_param_ops ms912x_primary_format_ops = {
	.set = ms912x_primary_format_set,
	.get = ms912x_primary_format_get,
};
module_param_cb(primary_format, &ms912x_primary_format_ops,
		&ms912x_parameters.primary_format, 0444);
MODULE_PARM_DESC(primary_format,
		 "Primary plane formats: rgb565 only, xrgb8888 only (alias xrgb), quality prefers XRGB8888, efficiency prefers RGB565; preference modes expose both formats (default: quality; load-time only)");

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

static int ms912x_fake_edid_set(const char *value,
				const struct kernel_param *kp)
{
	enum ms912x_fake_edid_mode mode;
	int ret = ms912x_fake_edid_parse(value, &mode);

	if (ret)
		return ret;
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
		 "EDID policy: auto falls back only for malformed EDID; on/y/Y/1/true or off/n/N/0/false (default: auto; runtime writable)");

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

static int ms912x_force_connected_set(const char *value,
				     const struct kernel_param *kp)
{
	enum ms912x_force_connected_mode mode;
	int ret = ms912x_force_connected_parse(value, &mode);

	if (ret)
		return ret;
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
		 "Connection policy: auto from status/EDID, on/y/Y/1/true, or off/n/N/0/false (default: auto; runtime writable)");

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

static int ms912x_scale_set(const char *value,
			    const struct kernel_param *kp)
{
	unsigned int scale, old;
	int ret = ms912x_scale_parse(value, &scale);

	if (ret)
		return ret;

	mutex_lock(&ms912x_scale_lock);
	old = READ_ONCE(ms912x_parameters.scale);
	if (old == scale) {
		mutex_unlock(&ms912x_scale_lock);
		return 0;
	}
	WRITE_ONCE(ms912x_parameters.scale, scale);
	atomic_inc(&ms912x_parameters.scale_generation);
	mutex_unlock(&ms912x_scale_lock);
	ms912x_notify_devices_hotplug();
	return 0;
}

void ms912x_scale_snapshot(struct ms912x_device *ms912x,
			   unsigned int *scale, int *generation,
			   u32 *device_generation)
{
	mutex_lock(&ms912x_scale_lock);
	*scale = READ_ONCE(ms912x->scale_is_overridden) ?
		READ_ONCE(ms912x->scale_override_setting) :
		READ_ONCE(ms912x_parameters.scale);
	*generation = atomic_read(&ms912x_parameters.scale_generation);
	*device_generation = READ_ONCE(ms912x->scale_device_config_generation);
	mutex_unlock(&ms912x_scale_lock);
}

static int ms912x_scale_get(char *buffer,
			    const struct kernel_param *kp)
{
	unsigned int scale = READ_ONCE(*(unsigned int *)kp->arg);

	if (scale == MS912X_SCALE_AUTO)
		return scnprintf(buffer, PAGE_SIZE, "auto\n");
	return scnprintf(buffer, PAGE_SIZE, "%u\n",
			 scale);
}

static const struct kernel_param_ops ms912x_scale_ops = {
	.set = ms912x_scale_set,
	.get = ms912x_scale_get,
};
module_param_cb(scale, &ms912x_scale_ops,
		&ms912x_parameters.scale, 0644);
MODULE_PARM_DESC(scale,
		 "Downscale preset: auto/on/1/y/Y, off/0/n/N, 50, 67, 80, or 100; on selects auto and off selects 100. Auto chooses the largest raster whose estimated full-frame UYVY transfer at 26 MB/s supports at least one quarter of mode refresh below USB 3, and uses 100 at USB 3 or faster; applied at modeset (default: 100; runtime writable)");

static int ms912x_damage_scan_set(const char *value,
				 const struct kernel_param *kp)
{
	enum ms912x_damage_scan_mode mode;
	enum ms912x_damage_scan_mode old;
	bool use_profile = sysfs_streq(value, "profile");
	bool initialized;

	if (use_profile) {
		ms912x_profile_restore_default(
			&ms912x_parameters.damage_scan_override);
		return 0;
	}
	if (sysfs_streq(value, "auto"))
		mode = MS912X_DAMAGE_SCAN_AUTO;
	else if (sysfs_streq(value, "off") || sysfs_streq(value, "n") ||
		 sysfs_streq(value, "N") || sysfs_streq(value, "0"))
		mode = MS912X_DAMAGE_SCAN_OFF;
	else if (sysfs_streq(value, "on") || sysfs_streq(value, "y") ||
		 sysfs_streq(value, "Y") || sysfs_streq(value, "1"))
		mode = MS912X_DAMAGE_SCAN_ON;
	else
		return -EINVAL;

	mutex_lock(&ms912x_profile_lock);
	initialized = ms912x_profile_initialized;
	old = READ_ONCE(*(enum ms912x_damage_scan_mode *)kp->arg);
	WRITE_ONCE(*(enum ms912x_damage_scan_mode *)kp->arg, mode);
	WRITE_ONCE(ms912x_parameters.damage_scan_override, true);
	if (old != mode)
		atomic_inc(&ms912x_parameters.damage_scan_generation);
	mutex_unlock(&ms912x_profile_lock);
	if (initialized)
		ms912x_profile_log_settings(NULL, NULL);
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
		 "Software damage scan: profile, auto, on/y/1, or off/n/0; explicit modes override the selected profile (default: profile; runtime writable)");

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

static int ms912x_param_damage_scan_tolerance_set(const char *value,
						 const struct kernel_param *kp)
{
	unsigned int minimum, maximum, old_minimum, old_maximum;
	const char *separator;
	char *minimum_text;
	bool initialized;
	int ret;

	if (sysfs_streq(value, "profile")) {
		ms912x_profile_restore_default(
			&ms912x_parameters.damage_scan_tolerance_override);
		return 0;
	}
	separator = strchr(value, '-');
	if (separator) {
		if (strchr(separator + 1, '-'))
			return -EINVAL;
		minimum_text = kstrndup(value, separator - value, GFP_KERNEL);
		if (!minimum_text)
			return -ENOMEM;
		ret = kstrtouint(minimum_text, 0, &minimum);
		kfree(minimum_text);
		if (ret)
			return ret;
		ret = kstrtouint(separator + 1, 0, &maximum);
		if (ret)
			return ret;
	} else {
		ret = kstrtouint(value, 0, &minimum);
		if (ret)
			return ret;
		maximum = minimum;
	}
	if (minimum > 31 || maximum > 31 || minimum > maximum)
		return -EINVAL;
	minimum = ms912x_damage_scan_tolerance_bucket(minimum);
	maximum = ms912x_damage_scan_tolerance_bucket(maximum);

	mutex_lock(&ms912x_profile_lock);
	old_minimum = READ_ONCE(*(unsigned int *)kp->arg);
	old_maximum = READ_ONCE(ms912x_parameters.damage_scan_tolerance_max);
	initialized = ms912x_profile_initialized;
	WRITE_ONCE(*(unsigned int *)kp->arg, minimum);
	WRITE_ONCE(ms912x_parameters.damage_scan_tolerance_max, maximum);
	WRITE_ONCE(ms912x_parameters.damage_scan_tolerance_override, true);
	if (old_minimum != minimum || old_maximum != maximum)
		atomic_inc(&ms912x_parameters.damage_scan_config_generation);
	mutex_unlock(&ms912x_profile_lock);
	if (initialized)
		ms912x_profile_log_settings(NULL, NULL);
	return 0;
}

static int ms912x_param_damage_scan_tolerance_get(char *buffer,
						 const struct kernel_param *kp)
{
	{
		unsigned int minimum = READ_ONCE(*(unsigned int *)kp->arg);
		unsigned int maximum = READ_ONCE(
			ms912x_parameters.damage_scan_tolerance_max);

		if (minimum == maximum)
			return scnprintf(buffer, PAGE_SIZE, "%u\n", minimum);
		return scnprintf(buffer, PAGE_SIZE, "%u-%u\n", minimum, maximum);
	}
}

static const struct kernel_param_ops ms912x_param_damage_scan_tolerance_ops = {
	.set = ms912x_param_damage_scan_tolerance_set,
	.get = ms912x_param_damage_scan_tolerance_get,
};
module_param_cb(damage_scan_tolerance,
		&ms912x_param_damage_scan_tolerance_ops,
		&ms912x_parameters.damage_scan_tolerance, 0644);
MODULE_PARM_DESC(damage_scan_tolerance,
		 "RGB bucket tolerance: profile, a fixed integer 0-31, or a dynamic min-max range such as 3-31; values round down to 0,1,3,7,15,31 and queue pressure selects bucket levels between watermarks; ignored by memcmp (runtime writable)");

static int ms912x_param_damage_scan_stride_set(const char *value,
					      const struct kernel_param *kp)
{
	unsigned int stride, old;
	bool initialized;
	int ret;

	if (sysfs_streq(value, "profile")) {
		ms912x_profile_restore_default(
			&ms912x_parameters.damage_scan_stride_override);
		return 0;
	}
	ret = kstrtouint(value, 0, &stride);

	if (ret)
		return ret;
	if (stride < 1 || stride > 16)
		return -EINVAL;

	mutex_lock(&ms912x_profile_lock);
	old = READ_ONCE(*(unsigned int *)kp->arg);
	initialized = ms912x_profile_initialized;
	WRITE_ONCE(*(unsigned int *)kp->arg, stride);
	WRITE_ONCE(ms912x_parameters.damage_scan_stride_override, true);
	if (old != stride)
		atomic_inc(&ms912x_parameters.damage_scan_config_generation);
	mutex_unlock(&ms912x_profile_lock);
	if (initialized)
		ms912x_profile_log_settings(NULL, NULL);
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
		 "Damage scan row stride: profile or an explicit value from 1 to 16 (runtime writable)");


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

static struct ms912x_device *ms912x_device_from_sysfs(struct device *dev)
{
	struct usb_interface *intf = to_usb_interface(dev);

	return usb_get_intfdata(intf);
}

static ssize_t ms912x_profile_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct ms912x_device *ms912x = ms912x_device_from_sysfs(dev);

	if (!ms912x)
		return -ENODEV;
	if (!READ_ONCE(ms912x->profile_override))
		return sysfs_emit(buf, "inherit\n");
	return sysfs_emit(buf, "%s\n",
			  ms912x_profile_name(
				  READ_ONCE(ms912x->profile_requested)));
}

static ssize_t ms912x_profile_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct ms912x_device *ms912x = ms912x_device_from_sysfs(dev);
	enum ms912x_profile profile = MS912X_PROFILE_AUTO;
	bool override = !sysfs_streq(buf, "inherit");
	int ret = 0;

	if (!ms912x)
		return -ENODEV;
	if (override) {
		ret = ms912x_profile_parse(buf, &profile);
		if (ret)
			return ret;
	}

	mutex_lock(&ms912x_profile_lock);
	WRITE_ONCE(ms912x->profile_requested, profile);
	WRITE_ONCE(ms912x->profile_override, override);
	ms912x_profile_configure_device_locked(ms912x);
	mutex_unlock(&ms912x_profile_lock);
	ms912x_profile_log_settings(dev, ms912x);
	return count;
}
static DEVICE_ATTR_RW(ms912x_profile);

static ssize_t ms912x_scale_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct ms912x_device *ms912x = ms912x_device_from_sysfs(dev);
	char setting[8];

	if (!ms912x)
		return -ENODEV;
	if (!READ_ONCE(ms912x->scale_is_overridden))
		return sysfs_emit(buf, "inherit\n");
	ms912x_scale_setting_format(setting, sizeof(setting),
				    READ_ONCE(ms912x->scale_override_setting));
	return sysfs_emit(buf, "%s\n", setting);
}

static ssize_t ms912x_scale_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct ms912x_device *ms912x = ms912x_device_from_sysfs(dev);
	unsigned int scale = MS912X_SCALE_100;
	bool override = !sysfs_streq(buf, "inherit");
	bool changed = false;
	int ret = 0;

	if (!ms912x)
		return -ENODEV;
	if (override) {
		ret = ms912x_scale_parse(buf, &scale);
		if (ret)
			return ret;
	}

	mutex_lock(&ms912x_scale_lock);
	if (READ_ONCE(ms912x->scale_is_overridden) != override ||
	    (override &&
	     READ_ONCE(ms912x->scale_override_setting) != scale)) {
		WRITE_ONCE(ms912x->scale_override_setting, scale);
		WRITE_ONCE(ms912x->scale_is_overridden, override);
		WRITE_ONCE(ms912x->scale_device_config_generation,
			   READ_ONCE(ms912x->scale_device_config_generation) + 1);
		changed = true;
	}
	mutex_unlock(&ms912x_scale_lock);
	if (changed) {
		drm_kms_helper_hotplug_event(&ms912x->drm);
		ms912x_profile_log_settings(dev, ms912x);
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

	if (!ms912x)
		return -ENODEV;
	if (!READ_ONCE(ms912x->fake_edid_override))
		return sysfs_emit(buf, "inherit\n");
	return sysfs_emit(buf, "%s\n",
			  ms912x_fake_edid_mode_name(
				  READ_ONCE(ms912x->fake_edid_mode)));
}

static ssize_t ms912x_fake_edid_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct ms912x_device *ms912x = ms912x_device_from_sysfs(dev);
	enum ms912x_fake_edid_mode mode = MS912X_FAKE_EDID_AUTO;
	bool override = !sysfs_streq(buf, "inherit");
	bool changed = false;
	int ret = 0;

	if (!ms912x)
		return -ENODEV;
	if (override) {
		ret = ms912x_fake_edid_parse(buf, &mode);
		if (ret)
			return ret;
	}

	mutex_lock(&ms912x->detect_lock);
	if (READ_ONCE(ms912x->fake_edid_override) != override ||
	    (override && READ_ONCE(ms912x->fake_edid_mode) != mode)) {
		if (override) {
			WRITE_ONCE(ms912x->fake_edid_mode, mode);
			WRITE_ONCE(ms912x->fake_edid_override, true);
		} else {
			WRITE_ONCE(ms912x->fake_edid_override, false);
			WRITE_ONCE(ms912x->fake_edid_mode, mode);
		}
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
			 override ? ms912x_fake_edid_mode_name(mode) : "inherit");
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

	if (!ms912x)
		return -ENODEV;
	if (!READ_ONCE(ms912x->force_connected_override))
		return sysfs_emit(buf, "inherit\n");
	return sysfs_emit(buf, "%s\n",
			  ms912x_force_connected_mode_name(
				  READ_ONCE(ms912x->force_connected_mode)));
}

static ssize_t ms912x_force_connected_store(struct device *dev,
					    struct device_attribute *attr,
					    const char *buf, size_t count)
{
	struct ms912x_device *ms912x = ms912x_device_from_sysfs(dev);
	enum ms912x_force_connected_mode mode = MS912X_FORCE_CONNECTED_AUTO;
	bool override = !sysfs_streq(buf, "inherit");
	bool changed = false;
	int ret = 0;

	if (!ms912x)
		return -ENODEV;
	if (override) {
		ret = ms912x_force_connected_parse(buf, &mode);
		if (ret)
			return ret;
	}

	mutex_lock(&ms912x->detect_lock);
	if (READ_ONCE(ms912x->force_connected_override) != override ||
	    (override && READ_ONCE(ms912x->force_connected_mode) != mode)) {
		if (override) {
			WRITE_ONCE(ms912x->force_connected_mode, mode);
			WRITE_ONCE(ms912x->force_connected_override, true);
		} else {
			WRITE_ONCE(ms912x->force_connected_override, false);
			WRITE_ONCE(ms912x->force_connected_mode, mode);
		}
		ms912x->detect_status_updated =
			jiffies - msecs_to_jiffies(250);
		changed = true;
	}
	mutex_unlock(&ms912x->detect_lock);
	if (changed) {
		drm_kms_helper_hotplug_event(&ms912x->drm);
		dev_info(dev, "force_connected=%s source=adapter\n",
			 override ? ms912x_force_connected_mode_name(mode) :
			 "inherit");
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

	if (!ms912x)
		return -ENODEV;
	if (!READ_ONCE(ms912x->damage_scan_mode_is_overridden))
		return sysfs_emit(buf, "inherit\n");
	mode = READ_ONCE(ms912x->damage_scan_mode_override);
	return sysfs_emit(buf, "%s\n",
			  ms912x_param_damage_scan_mode_name(mode));
}

static ssize_t ms912x_damage_scan_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct ms912x_device *ms912x = ms912x_device_from_sysfs(dev);
	enum ms912x_damage_scan_mode mode = MS912X_DAMAGE_SCAN_AUTO;
	bool override = !sysfs_streq(buf, "inherit");
	int ret = 0;

	if (!ms912x)
		return -ENODEV;
	if (override) {
		ret = ms912x_scan_mode_parse(buf, &mode);
		if (ret)
			return ret;
	}

	mutex_lock(&ms912x->damage_scan_lock);
	if (READ_ONCE(ms912x->damage_scan_mode_is_overridden) != override ||
	    (override && READ_ONCE(ms912x->damage_scan_mode_override) != mode)) {
		WRITE_ONCE(ms912x->damage_scan_mode_override, mode);
		WRITE_ONCE(ms912x->damage_scan_mode_is_overridden, override);
		ms912x->damage_scan_device_config_generation++;
	}
	mutex_unlock(&ms912x->damage_scan_lock);
	ms912x_profile_log_settings(dev, ms912x);
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
	if (*minimum > 31 || *maximum > 31 || *minimum > *maximum)
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

	if (!ms912x)
		return -ENODEV;
	if (!READ_ONCE(ms912x->damage_scan_tolerance_is_overridden))
		return sysfs_emit(buf, "inherit\n");
	minimum = READ_ONCE(ms912x->damage_scan_tolerance_override_min);
	maximum = READ_ONCE(ms912x->damage_scan_tolerance_override_max);
	if (minimum == maximum)
		return sysfs_emit(buf, "%u\n", minimum);
	return sysfs_emit(buf, "%u-%u\n", minimum, maximum);
}

static ssize_t ms912x_damage_scan_tolerance_store(struct device *dev,
						  struct device_attribute *attr,
						  const char *buf,
						  size_t count)
{
	struct ms912x_device *ms912x = ms912x_device_from_sysfs(dev);
	unsigned int minimum = 0, maximum = 0;
	bool override = !sysfs_streq(buf, "inherit");
	int ret = 0;

	if (!ms912x)
		return -ENODEV;
	if (override) {
		ret = ms912x_tolerance_parse(buf, &minimum, &maximum);
		if (ret)
			return ret;
	}

	mutex_lock(&ms912x->damage_scan_lock);
	if (READ_ONCE(ms912x->damage_scan_tolerance_is_overridden) != override ||
	    (override &&
	     (READ_ONCE(ms912x->damage_scan_tolerance_override_min) != minimum ||
	      READ_ONCE(ms912x->damage_scan_tolerance_override_max) != maximum))) {
		WRITE_ONCE(ms912x->damage_scan_tolerance_override_min, minimum);
		WRITE_ONCE(ms912x->damage_scan_tolerance_override_max, maximum);
		WRITE_ONCE(ms912x->damage_scan_tolerance_is_overridden, override);
		ms912x->damage_scan_device_config_generation++;
	}
	mutex_unlock(&ms912x->damage_scan_lock);
	ms912x_profile_log_settings(dev, ms912x);
	return count;
}
static DEVICE_ATTR_RW(ms912x_damage_scan_tolerance);

static ssize_t ms912x_damage_scan_stride_show(struct device *dev,
					      struct device_attribute *attr,
					      char *buf)
{
	struct ms912x_device *ms912x = ms912x_device_from_sysfs(dev);

	if (!ms912x)
		return -ENODEV;
	if (!READ_ONCE(ms912x->damage_scan_stride_is_overridden))
		return sysfs_emit(buf, "inherit\n");
	return sysfs_emit(buf, "%u\n",
			  READ_ONCE(ms912x->damage_scan_stride_override_value));
}

static ssize_t ms912x_damage_scan_stride_store(struct device *dev,
					       struct device_attribute *attr,
					       const char *buf, size_t count)
{
	struct ms912x_device *ms912x = ms912x_device_from_sysfs(dev);
	unsigned int stride = 0;
	bool override = !sysfs_streq(buf, "inherit");
	int ret = 0;

	if (!ms912x)
		return -ENODEV;
	if (override) {
		ret = kstrtouint(buf, 0, &stride);
		if (ret)
			return ret;
		if (stride < 1 || stride > 16)
			return -EINVAL;
	}

	mutex_lock(&ms912x->damage_scan_lock);
	if (READ_ONCE(ms912x->damage_scan_stride_is_overridden) != override ||
	    (override &&
	     READ_ONCE(ms912x->damage_scan_stride_override_value) != stride)) {
		WRITE_ONCE(ms912x->damage_scan_stride_override_value, stride);
		WRITE_ONCE(ms912x->damage_scan_stride_is_overridden, override);
		ms912x->damage_scan_device_config_generation++;
	}
	mutex_unlock(&ms912x->damage_scan_lock);
	ms912x_profile_log_settings(dev, ms912x);
	return count;
}
static DEVICE_ATTR_RW(ms912x_damage_scan_stride);

static struct attribute *ms912x_device_attrs[] = {
	&dev_attr_ms912x_profile.attr,
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

int ms912x_device_sysfs_register(struct ms912x_device *ms912x)
{
	return devm_device_add_group(&ms912x->intf->dev,
				     &ms912x_device_attr_group);
}
