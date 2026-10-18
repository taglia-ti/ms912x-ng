// SPDX-License-Identifier: GPL-2.0-only

#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pm.h>
#include <linux/string.h>
#include <linux/usb.h>
#include <linux/vmalloc.h>
#include <linux/workqueue.h>

#include <drm/clients/drm_client_setup.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_fbdev_shmem.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_managed.h>
#include <drm/drm_modeset_helper.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>

#include "ms912x.h"
#include "ms912x_scan_methods.h"

#define MS912X_DETECT_RESET_COOLDOWN (5 * HZ)
#define MS912X_INITIAL_DETECT_DELAY_MS 1000

static void ms912x_append_mode(char *buffer, size_t size, const char *mode,
			       const char *selected)
{
	size_t used = strlen(buffer);

	if (used < size)
		scnprintf(buffer + used, size - used, "%s%s%s%s",
			  used ? ", " : "", !strcmp(mode, selected) ? "[" : "",
			  mode, !strcmp(mode, selected) ? "]" : "");
}

static void ms912x_log_scan_backends(struct ms912x_device *ms912x)
{
	char scan_modes[80] = "";
	char chroma_modes[64] = "";
	const char *scan_selected =
		ms912x_damage_scan_selected_backend_name(ms912x);
	const char *chroma_selected =
		ms912x_fast_chroma_selected_backend_name();

	if (ms912x_scan_avx2_available()) {
		ms912x_append_mode(scan_modes, sizeof(scan_modes), "avx2",
				   scan_selected);
		ms912x_append_mode(chroma_modes, sizeof(chroma_modes), "avx2",
				   chroma_selected);
	}
	if (ms912x_scan_neon_available()) {
		ms912x_append_mode(scan_modes, sizeof(scan_modes), "neon",
				   scan_selected);
		ms912x_append_mode(chroma_modes, sizeof(chroma_modes), "neon",
				   chroma_selected);
	}
	if (ms912x_scan_sse2_available()) {
		ms912x_append_mode(scan_modes, sizeof(scan_modes), "sse2",
				   scan_selected);
		ms912x_append_mode(chroma_modes, sizeof(chroma_modes), "sse2",
				   chroma_selected);
	}
	ms912x_append_mode(scan_modes, sizeof(scan_modes), "memcmp",
			   scan_selected);
	ms912x_append_mode(scan_modes, sizeof(scan_modes), "scalar",
			   scan_selected);
	ms912x_append_mode(chroma_modes, sizeof(chroma_modes), "scalar",
			   chroma_selected);
	dev_info(&ms912x->intf->dev, "damage_method: %s; fast_chroma: %s\n",
		 scan_modes, chroma_modes);
}

static bool ms912x_detection_error_resettable(int error)
{
	switch (error) {
	case -EPIPE:
	case -ETIMEDOUT:
	case -EAGAIN:
	case -EPROTO:
	case -EILSEQ:
	case -ECOMM:
	case -EREMOTEIO:
	case -ENODATA:
		return true;
	default:
		return false;
	}
}

static void ms912x_detect_reset_worker(struct work_struct *work)
{
	struct ms912x_device *ms912x =
		container_of(work, struct ms912x_device, detect_reset_work);

	if (READ_ONCE(ms912x->detect_reset_stopping))
		goto done;

	mutex_lock(&ms912x->detect_lock);
	if (READ_ONCE(ms912x->detect_reset_stopping)) {
		mutex_unlock(&ms912x->detect_lock);
		goto done;
	}
	/* Exclude concurrent detection I/O without holding the lock across reset. */
	ms912x->detect_reset_running = true;
	mutex_unlock(&ms912x->detect_lock);

	/*
	 * A synchronous reset can unbind this interface and call disconnect()
	 * inline. disconnect() cancels detect_reset_work, so doing that from this
	 * work item deadlocks. Queue the reset on the USB core workqueue instead.
	 * The reset may free ms912x as soon as this call returns; do not dereference
	 * it or touch detect_reset_pending after queueing.
	 */
	dev_info(&ms912x->intf->dev,
		 "queueing USB reset after repeated detection communication errors\n");
	usb_queue_reset_device(ms912x->intf);
	return;

done:
	atomic_set(&ms912x->detect_reset_pending, 0);
}

void ms912x_schedule_detect_reset(struct ms912x_device *ms912x, int error)
{
	if (!ms912x_detection_error_resettable(error) ||
	    READ_ONCE(ms912x->detect_reset_stopping))
		return;
	if (ms912x->detect_reset_last &&
	    time_before(jiffies, ms912x->detect_reset_last +
			MS912X_DETECT_RESET_COOLDOWN))
		return;
	if (atomic_cmpxchg(&ms912x->detect_reset_pending, 0, 1))
		return;

	ms912x->detect_reset_last = jiffies;
	schedule_work(&ms912x->detect_reset_work);
}

static void ms912x_free_damage_scan_snapshot(struct drm_device *drm, void *data)
{
	struct ms912x_device *ms912x = data;

	vfree(ms912x->damage_scan_snapshot);
	vfree(ms912x->damage_scan_snapshot_back);
}

static void ms912x_free_conversion_snapshots(struct drm_device *drm,
					     void *data)
{
	struct ms912x_device *ms912x = data;

	vfree(ms912x->conversion_xrgb);
	vfree(ms912x->latest_xrgb);
}

static void ms912x_free_scan_frames(struct drm_device *drm, void *data)
{
	struct ms912x_device *ms912x = data;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(ms912x->scan_frames); i++)
		vfree(ms912x->scan_frames[i].pixels);
}

static int ms912x_usb_suspend(struct usb_interface *interface,
			      pm_message_t message)
{
	struct ms912x_device *ms912x = usb_get_intfdata(interface);

	return drm_mode_config_helper_suspend(&ms912x->drm);
}

static int ms912x_usb_resume(struct usb_interface *interface)
{
	struct ms912x_device *ms912x = usb_get_intfdata(interface);

	return drm_mode_config_helper_resume(&ms912x->drm);
}

DEFINE_DRM_GEM_FOPS(ms912x_driver_fops);

static const struct drm_driver ms912x_drm_driver = {
	.driver_features = DRIVER_ATOMIC | DRIVER_GEM | DRIVER_MODESET,

	/* GEM hooks */
	.fops = &ms912x_driver_fops,
	DRM_GEM_SHMEM_DRIVER_OPS,
	DRM_FBDEV_SHMEM_DRIVER_OPS,

	.name = MS912X_DRIVER_NAME,
	.desc = MS912X_DRIVER_DESC,
	.major = MS912X_DRIVER_MAJOR,
	.minor = MS912X_DRIVER_MINOR,
};

static const u8 ms912x_bulk_out_endpoints[] = {
	MS912X_BULK_OUT_ENDPOINT, 0
};

static int ms912x_usb_probe(struct usb_interface *interface,
			    const struct usb_device_id *id)
{
	int ret;
	unsigned int i;
	struct ms912x_device *ms912x;
	struct drm_device *dev;
	struct device *dma_dev;
	struct usb_device *usbdev;

	ms912x = devm_drm_dev_alloc(&interface->dev, &ms912x_drm_driver,
				    struct ms912x_device, drm);
	if (IS_ERR(ms912x))
		return PTR_ERR(ms912x);
	INIT_LIST_HEAD(&ms912x->device_list);
	dev = &ms912x->drm;

	atomic_set(&ms912x->damage_scan_invalid, 1);
	ms912x->usb_speed_mean_bps = MS912X_USB_INITIAL_RATE_BPS;
	ms912x->usb_speed_samples = 1;
	ms912x->usb_mean_payload_bytes = MS912X_MAX_TRANSFER_LEN;
	ms912x->damage_scan_mode_generation =
		atomic_read(&ms912x_parameters.damage_scan_generation);
	ms912x->damage_scan_config_generation =
		atomic_read(&ms912x_parameters.damage_scan_config_generation);
	ms912x->damage_scan_probe_config_generation =
		atomic_read(&ms912x_parameters.damage_scan_config_generation);

	ret = devm_mutex_init(&interface->dev, &ms912x->damage_scan_lock);
	if (ret)
		return ret;
	ret = devm_mutex_init(&interface->dev, &ms912x->transfer_lock);
	if (ret)
		return ret;
	ret = devm_mutex_init(&interface->dev, &ms912x->scan_queue_lock);
	if (ret)
		return ret;
	ms912x->damage_scan_snapshot = vzalloc(
		MS912X_MAX_WIDTH * MS912X_MAX_HEIGHT *
		sizeof(*ms912x->damage_scan_snapshot));
	ms912x->damage_scan_snapshot_back = vzalloc(
		MS912X_MAX_WIDTH * MS912X_MAX_HEIGHT *
		sizeof(*ms912x->damage_scan_snapshot_back));
	if (!ms912x->damage_scan_snapshot ||
	    !ms912x->damage_scan_snapshot_back) {
		vfree(ms912x->damage_scan_snapshot_back);
		vfree(ms912x->damage_scan_snapshot);
		return -ENOMEM;
	}
	ret = drmm_add_action_or_reset(dev, ms912x_free_damage_scan_snapshot,
				       ms912x);
	if (ret)
		return ret;
	ms912x->latest_xrgb = vzalloc(
		MS912X_MAX_WIDTH * MS912X_MAX_HEIGHT *
		sizeof(*ms912x->latest_xrgb));
	ms912x->conversion_xrgb = vzalloc(
		MS912X_MAX_WIDTH * MS912X_MAX_HEIGHT *
		sizeof(*ms912x->conversion_xrgb));
	if (!ms912x->latest_xrgb || !ms912x->conversion_xrgb) {
		vfree(ms912x->conversion_xrgb);
		vfree(ms912x->latest_xrgb);
		ms912x->conversion_xrgb = NULL;
		ms912x->latest_xrgb = NULL;
		return -ENOMEM;
	}
	ret = drmm_add_action_or_reset(dev, ms912x_free_conversion_snapshots,
				       ms912x);
	if (ret)
		return ret;
	ret = drmm_add_action_or_reset(dev, ms912x_free_scan_frames, ms912x);
	if (ret)
		return ret;
	for (i = 0; i < ARRAY_SIZE(ms912x->scan_frames); i++) {
		ms912x->scan_frames[i].pixels = vzalloc(
			MS912X_MAX_WIDTH * MS912X_MAX_HEIGHT *
			sizeof(*ms912x->scan_frames[i].pixels));
		if (!ms912x->scan_frames[i].pixels)
			return -ENOMEM;
	}

	ms912x->intf = interface;
	ret = devm_mutex_init(&interface->dev, &ms912x->ctrl_lock);
	if (ret)
		return ret;
	ret = devm_mutex_init(&interface->dev, &ms912x->detect_lock);
	if (ret)
		return ret;
	ret = devm_mutex_init(&interface->dev, &ms912x->edid_lock);
	if (ret)
		return ret;
	INIT_WORK(&ms912x->detect_reset_work, ms912x_detect_reset_worker);
	atomic_set(&ms912x->detect_reset_pending, 0);

	if (!usb_check_bulk_endpoints(interface, ms912x_bulk_out_endpoints))
		return -ENXIO;

	ret = ms912x_timing_load_custom(ms912x);
	if (ret < 0) {
		drm_warn(&ms912x->drm,
			 "failed to read custom timing: %d\n", ret);
	}
	for (i = 0; i < ms912x->num_custom_modes; i++) {
		const struct ms912x_mode *mode = &ms912x->custom_modes[i].mode;

		drm_info(&ms912x->drm,
			 "custom mode %dx%d@%d uses mode 0x%02x\n",
			 mode->width, mode->height, mode->hz, mode->mode);
	}

	usbdev = interface_to_usbdev(interface);
	if (usbdev->speed >= USB_SPEED_SUPER) {
		ms912x->damage_scan_stride_default = 1;
		ms912x->damage_scan_tolerance_default = 0;
		dev_info(&interface->dev,
			 "USB 3 link: automatic damage scan stride=1 tolerance=0\n");
	} else {
		ms912x->damage_scan_stride_default = 3;
		ms912x->damage_scan_tolerance_default = 8;
		dev_info(&interface->dev,
			 "USB 2 link: automatic damage scan stride=3 tolerance=8\n");
	}
	ms912x_log_scan_backends(ms912x);
	ms912x->bulk_pipe =
		usb_sndbulkpipe(usbdev, MS912X_BULK_OUT_ENDPOINT);
	dma_dev = usb_intf_get_dma_device(interface);
	if (dma_dev) {
		drm_dev_set_dma_dev(dev, dma_dev);
		put_device(dma_dev);
	} else {
		drm_warn(dev,
			 "buffer sharing not supported\n"); /* not an error */
	}
	ret = drmm_mode_config_init(dev);
	if (ret)
		return ret;

	ms912x_kms_mode_config_init(dev);

	/* Select a known bootstrap timing before registering the DRM device. */
	ret = ms912x_display_set_mode(ms912x, ms912x_default_mode());
	if (ret)
		return ret;

	ms912x->workqueue =
		drmm_alloc_ordered_workqueue(dev, MS912X_DRIVER_NAME, 0);
	if (IS_ERR(ms912x->workqueue))
		return PTR_ERR(ms912x->workqueue);
	ms912x->conversion_workqueue =
		drmm_alloc_ordered_workqueue(dev, MS912X_DRIVER_NAME "-convert", 0);
	if (IS_ERR(ms912x->conversion_workqueue))
		return PTR_ERR(ms912x->conversion_workqueue);
	ms912x_transfer_work_init(ms912x);

	ret = ms912x_usb_request_init(ms912x, &ms912x->requests[0],
				  MS912X_MAX_TRANSFER_LEN);
	if (ret)
		return ret;

	ret = ms912x_usb_request_init(ms912x, &ms912x->requests[1],
				  MS912X_MAX_TRANSFER_LEN);
	if (ret)
		goto err_free_request_0;

	ret = ms912x_kms_init(ms912x);
	if (ret)
		goto err_free_request_1;

	ret = ms912x_connector_register(ms912x);
	if (ret)
		goto err_free_request_1;

	drm_mode_config_reset(dev);

	/* Give the adapter time to bring up its sink/EDID detection after USB probe. */
	msleep(MS912X_INITIAL_DETECT_DELAY_MS);

	usb_set_intfdata(interface, ms912x);

	drmm_kms_helper_poll_init(dev);

	ret = drm_dev_register(dev, 0);
	if (ret)
		goto err_free_request_1;

	ms912x_debugfs_register(dev);

	drm_client_setup(dev, NULL);
	ms912x_device_registry_add(ms912x);

	return 0;

err_free_request_1:
	ms912x_usb_request_release(&ms912x->requests[1]);
err_free_request_0:
	ms912x_usb_request_release(&ms912x->requests[0]);
	return ret;
}

static void ms912x_usb_disconnect(struct usb_interface *interface)
{
	struct ms912x_device *ms912x = usb_get_intfdata(interface);
	struct drm_device *dev = &ms912x->drm;

	WRITE_ONCE(ms912x->detect_reset_stopping, true);
	cancel_work_sync(&ms912x->detect_reset_work);
	ms912x_device_registry_remove(ms912x);

	ms912x_transfer_stop(ms912x);
	drm_atomic_helper_shutdown(dev);
	drm_dev_unplug(dev);
	ms912x_transfer_free_requests(ms912x);
}

static void ms912x_usb_shutdown(struct usb_interface *interface)
{
	struct ms912x_device *ms912x = usb_get_intfdata(interface);

	drm_atomic_helper_shutdown(&ms912x->drm);
}

static const struct usb_device_id id_table[] = {
	/* USB 2 */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x534d, 0x6021, 0xff, 0x00, 0x00) },
	/* USB 2 Sometimes this PID will pop up */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x534d, 0x0821, 0xff, 0x00, 0x00) },
	/* USB 3 */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x345f, 0x9132, 0xff, 0x00, 0x00) },
	{ USB_DEVICE_AND_INTERFACE_INFO(0x345f, 0x9133, 0xff, 0x00, 0x00) },
	{ USB_DEVICE_AND_INTERFACE_INFO(0x345f, 0x9135, 0xff, 0x00, 0x00) },
	{ }
};
MODULE_DEVICE_TABLE(usb, id_table);

static struct usb_driver ms912x_driver = {
	.name = MS912X_DRIVER_NAME,
	.probe = ms912x_usb_probe,
	.disconnect = ms912x_usb_disconnect,
	.suspend = ms912x_usb_suspend,
	.resume = ms912x_usb_resume,
	.reset_resume = ms912x_usb_resume,
	.shutdown = ms912x_usb_shutdown,
	.id_table = id_table,
};

static int __init ms912x_init(void)
{
	int ret;

	if (ms912x_parameters.debugfs_enabled)
		static_branch_enable(&ms912x_stats_enabled_key);

	ret = usb_register(&ms912x_driver);
	if (ret && ms912x_parameters.debugfs_enabled)
		static_branch_disable(&ms912x_stats_enabled_key);

	return ret;
}
module_init(ms912x_init);

static void __exit ms912x_exit(void)
{
	usb_deregister(&ms912x_driver);
	if (ms912x_parameters.debugfs_enabled)
		static_branch_disable(&ms912x_stats_enabled_key);
}
module_exit(ms912x_exit);
MODULE_DESCRIPTION(MS912X_DRIVER_DESC);
MODULE_VERSION(MS912X_BUILD_VERSION);
MODULE_LICENSE("GPL");
