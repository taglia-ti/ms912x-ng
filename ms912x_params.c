// SPDX-License-Identifier: GPL-2.0-only

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/string.h>

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
		WRITE_ONCE(ms912x->damage_scan_stride_default, values.stride);
	mutex_unlock(&ms912x_devices_lock);
}

static void ms912x_profile_log_settings(struct device *dev);

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

	mutex_lock(&ms912x_profile_lock);
	WRITE_ONCE(ms912x_parameters.profile, profile);
	initialized = ms912x_profile_initialized;
	if (initialized) {
		ms912x_profile_apply_locked(ms912x_profile_effective_locked());
	} else {
		WRITE_ONCE(ms912x_parameters.damage_scan_override, false);
		WRITE_ONCE(ms912x_parameters.damage_scan_stride_override, false);
		WRITE_ONCE(ms912x_parameters.damage_scan_tolerance_override,
			   false);
	}
	mutex_unlock(&ms912x_profile_lock);
	if (initialized)
		ms912x_profile_log_settings(NULL);
}

/* Restore one scan parameter to the currently selected profile. */
static void ms912x_profile_restore_default(bool *override)
{
	bool initialized;

	mutex_lock(&ms912x_profile_lock);
	WRITE_ONCE(*override, false);
	initialized = ms912x_profile_initialized;
	if (initialized)
		ms912x_profile_apply_locked(ms912x_profile_effective_locked());
	mutex_unlock(&ms912x_profile_lock);
	if (initialized)
		ms912x_profile_log_settings(NULL);
}

static void ms912x_profile_log_settings(struct device *dev)
{
	enum ms912x_profile requested =
		READ_ONCE(ms912x_parameters.profile);
	enum ms912x_profile effective =
		READ_ONCE(ms912x_parameters.effective_profile);
	enum ms912x_damage_scan_mode scan =
		READ_ONCE(ms912x_parameters.damage_scan);
	unsigned int stride = READ_ONCE(ms912x_parameters.damage_scan_stride);
	unsigned int tolerance_min =
		READ_ONCE(ms912x_parameters.damage_scan_tolerance);
	unsigned int tolerance_max =
		READ_ONCE(ms912x_parameters.damage_scan_tolerance_max);
	unsigned int scale = READ_ONCE(ms912x_parameters.scale);
	char scale_name[8];

	if (scale == MS912X_SCALE_AUTO)
		strscpy(scale_name, "auto", sizeof(scale_name));
	else
		scnprintf(scale_name, sizeof(scale_name), "%u", scale);

	if (dev)
		dev_info(dev,
			 "profile=%s effective=%s damage_scan=%s stride=%u tolerance=%u-%u scale=%s\n",
			 ms912x_profile_name(requested),
			 ms912x_profile_name(effective),
			 ms912x_param_damage_scan_mode_name(scan), stride,
			 tolerance_min, tolerance_max, scale_name);
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
	struct ms912x_profile_values values;
	enum ms912x_profile effective;
	bool auto_speed_mismatch = false;

	mutex_lock(&ms912x_profile_lock);
	if (!ms912x_auto_profile_resolved) {
		ms912x_auto_profile = ms912x_profile_for_usb_speed(speed);
		ms912x_auto_profile_resolved = true;
	} else if (READ_ONCE(ms912x_parameters.profile) ==
		   MS912X_PROFILE_AUTO) {
		auto_speed_mismatch =
			ms912x_profile_for_usb_speed(speed) != ms912x_auto_profile;
	}

	effective = ms912x_profile_effective_locked();
	ms912x_profile_apply_locked(effective);
	ms912x_profile_initialized = true;
	values = ms912x_profile_values_for(effective);
	WRITE_ONCE(ms912x->damage_scan_stride_default, values.stride);
	mutex_unlock(&ms912x_profile_lock);

	if (auto_speed_mismatch)
		dev_warn(&ms912x->intf->dev,
			 "profile=auto uses module-global settings selected for the first adapter; this adapter negotiated a different USB speed class\n");
	ms912x_profile_log_settings(&ms912x->intf->dev);
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
		 "Scan profile: auto (architecture and first USB link), efficiency, latency, or quality; RGB565 latency uses tolerance 3-7, XRGB8888 latency uses 3-15; explicit scan parameter values override profile defaults (runtime writable)");

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

static void ms912x_refresh_devices_after_draws_off(void)
{
	struct ms912x_device *ms912x;

	mutex_lock(&ms912x_devices_lock);
	list_for_each_entry(ms912x, &ms912x_devices, device_list)
		ms912x_transfer_force_full_update(ms912x);
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

static int ms912x_fake_edid_set(const char *value,
				const struct kernel_param *kp)
{
	enum ms912x_fake_edid_mode mode;

	if (sysfs_streq(value, "auto"))
		mode = MS912X_FAKE_EDID_AUTO;
	else if (sysfs_streq(value, "on") || sysfs_streq(value, "y") ||
		 sysfs_streq(value, "Y") || sysfs_streq(value, "1") ||
		 sysfs_streq(value, "true"))
		mode = MS912X_FAKE_EDID_ON;
	else if (sysfs_streq(value, "off") || sysfs_streq(value, "n") ||
		 sysfs_streq(value, "N") || sysfs_streq(value, "0") ||
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
		 "EDID policy: auto falls back only for malformed EDID; on/y/Y/1/true or off/n/N/0/false (default: auto; runtime writable)");

static int ms912x_force_connected_set(const char *value,
				     const struct kernel_param *kp)
{
	enum ms912x_force_connected_mode mode;

	if (sysfs_streq(value, "auto"))
		mode = MS912X_FORCE_CONNECTED_AUTO;
	else if (sysfs_streq(value, "on") || sysfs_streq(value, "y") ||
		 sysfs_streq(value, "Y") || sysfs_streq(value, "1") ||
		 sysfs_streq(value, "true"))
		mode = MS912X_FORCE_CONNECTED_ON;
	else if (sysfs_streq(value, "off") || sysfs_streq(value, "n") ||
		 sysfs_streq(value, "N") || sysfs_streq(value, "0") ||
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
		 "Connection policy: auto from status/EDID, on/y/Y/1/true, or off/n/N/0/false (default: auto; runtime writable)");

module_param_named(vrr, ms912x_parameters.vrr, bool, 0444);
MODULE_PARM_DESC(vrr,
		 "Force the DRM connector vrr_capable property on (default: true; load-time only)");

static int ms912x_scale_set(const char *value,
			    const struct kernel_param *kp)
{
	unsigned int scale;
	unsigned int old;

	if (sysfs_streq(value, "auto") || sysfs_streq(value, "on") ||
	    sysfs_streq(value, "1") || sysfs_streq(value, "y") ||
	    sysfs_streq(value, "Y"))
		scale = MS912X_SCALE_AUTO;
	else if (sysfs_streq(value, "off") || sysfs_streq(value, "0") ||
		 sysfs_streq(value, "n") || sysfs_streq(value, "N"))
		scale = MS912X_SCALE_100;
	else if (sysfs_streq(value, "50"))
		scale = MS912X_SCALE_50;
	else if (sysfs_streq(value, "67"))
		scale = MS912X_SCALE_67;
	else if (sysfs_streq(value, "80"))
		scale = MS912X_SCALE_80;
	else if (sysfs_streq(value, "100"))
		scale = MS912X_SCALE_100;
	else
		return -EINVAL;

	mutex_lock(&ms912x_scale_lock);
	old = READ_ONCE(*(unsigned int *)kp->arg);
	if (old == scale) {
		mutex_unlock(&ms912x_scale_lock);
		return 0;
	}
	WRITE_ONCE(*(unsigned int *)kp->arg, scale);
	atomic_inc(&ms912x_parameters.scale_generation);
	mutex_unlock(&ms912x_scale_lock);
	ms912x_notify_devices_hotplug();
	return 0;
}

void ms912x_scale_snapshot(unsigned int *scale, int *generation)
{
	mutex_lock(&ms912x_scale_lock);
	*scale = READ_ONCE(ms912x_parameters.scale);
	*generation = atomic_read(&ms912x_parameters.scale_generation);
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
		ms912x_profile_log_settings(NULL);
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
		ms912x_profile_log_settings(NULL);
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
		ms912x_profile_log_settings(NULL);
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
