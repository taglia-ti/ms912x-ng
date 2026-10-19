// SPDX-License-Identifier: GPL-2.0-only

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/string.h>

#include <drm/drm_probe_helper.h>

#include "ms912x.h"

struct ms912x_parameters ms912x_parameters = {
	.damage_scan = MS912X_DAMAGE_SCAN_AUTO,
	.damage_scan_method = MS912X_DAMAGE_SCAN_METHOD_AUTO,
	.damage_scan_stride = 3,
	.damage_scan_tolerance = 64,
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
};

static LIST_HEAD(ms912x_devices);
static DEFINE_MUTEX(ms912x_devices_lock);

static void ms912x_refresh_devices_after_draws_off(void)
{
	struct ms912x_device *ms912x;

	mutex_lock(&ms912x_devices_lock);
	list_for_each_entry(ms912x, &ms912x_devices, device_list)
		ms912x_transfer_force_full_update(ms912x);
	mutex_unlock(&ms912x_devices_lock);
}

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
	} else if (sysfs_streq(value, "sse2")) {
#ifdef CONFIG_X86_64
		mode = MS912X_FAST_CHROMA_SSE2;
#else
		return -EINVAL;
#endif
	} else if (sysfs_streq(value, "neon")) {
#ifdef CONFIG_ARM64
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
		 "Conversion backend: auto, avx2, neon, sse2, or scalar; auto selects best available SIMD, explicit SIMD forces that backend on supported architectures; numeric 0=scalar, any positive value=auto (runtime writable)");

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
	else if (sysfs_streq(value, "off") || sysfs_streq(value, "n") ||
		 sysfs_streq(value, "N") || sysfs_streq(value, "0"))
		mode = MS912X_DAMAGE_SCAN_OFF;
	else if (sysfs_streq(value, "on") || sysfs_streq(value, "y") ||
		 sysfs_streq(value, "Y") || sysfs_streq(value, "1"))
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
		 "Software damage scan: auto, on/y/1, or off/n/0 (runtime writable; default: auto)");

static int ms912x_param_damage_scan_method_set(const char *value,
					       const struct kernel_param *kp)
{
	enum ms912x_damage_scan_method method;
	enum ms912x_damage_scan_method old;

	if (sysfs_streq(value, "scalar") || sysfs_streq(value, "0"))
		method = MS912X_DAMAGE_SCAN_SCALAR;
	else if (sysfs_streq(value, "memcmp") || sysfs_streq(value, "1"))
		method = MS912X_DAMAGE_SCAN_MEMCMP;
	else if (sysfs_streq(value, "avx2")) {
#ifdef CONFIG_X86_64
		method = MS912X_DAMAGE_SCAN_AVX2;
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
#ifdef CONFIG_ARM64
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
	case MS912X_DAMAGE_SCAN_AVX2:
		return scnprintf(buffer, PAGE_SIZE, "avx2\n");
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
	case MS912X_DAMAGE_SCAN_AVX2:
		return "avx2";
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
		 "Damage scan method: auto, avx2, neon, sse2, memcmp, or scalar; explicit SIMD methods force that backend on supported architectures; auto selects AVX2, NEON, SSE2, memcmp, then scalar (runtime writable)");

static int ms912x_param_damage_scan_tolerance_set(const char *value,
						 const struct kernel_param *kp)
{
	unsigned int tolerance, old;
	bool old_override = READ_ONCE(
		ms912x_parameters.damage_scan_tolerance_override);
	int ret;

	if (sysfs_streq(value, "auto")) {
		if (!old_override)
			return 0;
		WRITE_ONCE(ms912x_parameters.damage_scan_tolerance_override,
			   false);
		atomic_inc(&ms912x_parameters.damage_scan_config_generation);
		return 0;
	}
	ret = kstrtouint(value, 0, &tolerance);

	if (ret)
		return ret;
	if (tolerance > 255)
		return -EINVAL;

	old = READ_ONCE(*(unsigned int *)kp->arg);
	if (old_override && old == tolerance)
		return 0;
	WRITE_ONCE(*(unsigned int *)kp->arg, tolerance);
	WRITE_ONCE(ms912x_parameters.damage_scan_tolerance_override, true);
	atomic_inc(&ms912x_parameters.damage_scan_config_generation);
	return 0;
}

static int ms912x_param_damage_scan_tolerance_get(char *buffer,
						 const struct kernel_param *kp)
{
	if (!READ_ONCE(ms912x_parameters.damage_scan_tolerance_override))
		return scnprintf(buffer, PAGE_SIZE, "auto\n");
	return scnprintf(buffer, PAGE_SIZE, "%u\n",
			 READ_ONCE(*(unsigned int *)kp->arg));
}

static const struct kernel_param_ops ms912x_param_damage_scan_tolerance_ops = {
	.set = ms912x_param_damage_scan_tolerance_set,
	.get = ms912x_param_damage_scan_tolerance_get,
};
module_param_cb(damage_scan_tolerance,
		&ms912x_param_damage_scan_tolerance_ops,
		&ms912x_parameters.damage_scan_tolerance, 0644);
MODULE_PARM_DESC(damage_scan_tolerance,
		 "Per-channel RGB threshold: auto (USB2: 64, USB3: 0), or 0-255; 0 selects exact scalar/AVX2 and nonzero selects relaxed comparison; ignored by memcmp (runtime writable)");

static int ms912x_param_damage_scan_stride_set(const char *value,
					      const struct kernel_param *kp)
{
	unsigned int stride, old;
	bool old_override = READ_ONCE(
		ms912x_parameters.damage_scan_stride_override);
	int ret;

	if (sysfs_streq(value, "auto")) {
		if (!old_override)
			return 0;
		WRITE_ONCE(ms912x_parameters.damage_scan_stride_override, false);
		atomic_inc(&ms912x_parameters.damage_scan_config_generation);
		return 0;
	}
	ret = kstrtouint(value, 0, &stride);

	if (ret)
		return ret;
	if (stride < 1 || stride > 16)
		return -EINVAL;

	old = READ_ONCE(*(unsigned int *)kp->arg);
	if (old_override && old == stride)
		return 0;
	WRITE_ONCE(*(unsigned int *)kp->arg, stride);
	WRITE_ONCE(ms912x_parameters.damage_scan_stride_override, true);
	atomic_inc(&ms912x_parameters.damage_scan_config_generation);
	return 0;
}

static int ms912x_param_damage_scan_stride_get(char *buffer,
					      const struct kernel_param *kp)
{
	if (!READ_ONCE(ms912x_parameters.damage_scan_stride_override))
		return scnprintf(buffer, PAGE_SIZE, "auto\n");
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
		 "Damage scan row stride: auto (USB2: 3, USB3: 1), or 1-16 (runtime writable)");


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
