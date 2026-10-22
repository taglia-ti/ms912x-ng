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

#ifdef CONFIG_X86_64
#include <asm/cpufeature.h>
#include <asm/simd.h>
#endif

#include <drm/clients/drm_client_setup.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_fbdev_shmem.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_managed.h>
#include <drm/drm_modeset_helper.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>

#include "ms912x.h"
#include "ms912x_scan_rects.h"
#include "ms912x_scan_methods.h"

#define MS912X_DETECT_RESET_COOLDOWN (5 * HZ)
#define MS912X_INITIAL_DETECT_DELAY_MS 1500

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

	if (ms912x_scan_avx_available())
		ms912x_append_mode(scan_modes, sizeof(scan_modes), "avx",
				   scan_selected);
#ifdef CONFIG_X86_64
	if (boot_cpu_has(X86_FEATURE_AVX) && boot_cpu_has(X86_FEATURE_AVX2) &&
	    may_use_simd())
		ms912x_append_mode(chroma_modes, sizeof(chroma_modes), "avx2",
				   chroma_selected);
	if (boot_cpu_has(X86_FEATURE_SSSE3) && may_use_simd())
		ms912x_append_mode(chroma_modes, sizeof(chroma_modes), "ssse3",
				   chroma_selected);
#endif
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
	vfree(ms912x->rgb565_scale_scratch);
	ms912x->damage_scan_snapshot = NULL;
	ms912x->damage_scan_snapshot_back = NULL;
	ms912x->rgb565_scale_scratch = NULL;
}

static void ms912x_free_conversion_snapshots(struct drm_device *drm,
					     void *data)
{
	struct ms912x_device *ms912x = data;

	vfree(ms912x->conversion_pixels);
	vfree(ms912x->latest_pixels);
	ms912x->conversion_pixels = NULL;
	ms912x->latest_pixels = NULL;
}

static void ms912x_free_scan_frames(struct drm_device *drm, void *data)
{
	struct ms912x_device *ms912x = data;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(ms912x->scan_frames); i++) {
		vfree(ms912x->scan_frames[i].pixels);
		ms912x->scan_frames[i].pixels = NULL;
	}
}

static void ms912x_free_cursor_buffers(struct drm_device *drm, void *data)
{
	struct ms912x_device *ms912x = data;

	vfree(ms912x->cursor_buffer_storage);
	ms912x->cursor_buffer_storage = NULL;
	ms912x->cursor_source_pixels = NULL;
	ms912x->cursor_staging_pixels = NULL;
	ms912x->cursor_prepare_pixels = NULL;
	ms912x->cursor_pixels = NULL;
}

static int ms912x_init_cursor_buffers(struct ms912x_device *ms912x)
{
	unsigned int pixels = MS912X_CURSOR_MAX_WIDTH * MS912X_CURSOR_MAX_HEIGHT;

	ms912x->cursor_buffer_storage = vzalloc(4 * pixels * sizeof(u32));
	if (!ms912x->cursor_buffer_storage)
		return -ENOMEM;
	ms912x->cursor_source_pixels = ms912x->cursor_buffer_storage;
	ms912x->cursor_staging_pixels = ms912x->cursor_source_pixels + pixels;
	ms912x->cursor_prepare_pixels = ms912x->cursor_staging_pixels + pixels;
	ms912x->cursor_pixels = ms912x->cursor_prepare_pixels + pixels;
	return drmm_add_action_or_reset(&ms912x->drm,
				       ms912x_free_cursor_buffers, ms912x);
}

static void ms912x_free_rect_vectors(struct drm_device *drm, void *data)
{
	struct ms912x_device *ms912x = data;

	kvfree(ms912x->payload_compare_rects);
	ms912x_scan_rect_index_free(ms912x->scan_rect_index);
	kvfree(ms912x->scan_damage_rects);
	kvfree(ms912x->submit_damage_rects);
	kvfree(ms912x->pending_rects);
	kvfree(ms912x->previous_damage_rects);
	ms912x->payload_compare_rects = NULL;
	ms912x->scan_rect_index = NULL;
	ms912x->scan_damage_rects = NULL;
	ms912x->submit_damage_rects = NULL;
	ms912x->pending_rects = NULL;
	ms912x->previous_damage_rects = NULL;
	ms912x->scan_damage_rect_capacity = 0;
	ms912x->submit_damage_rect_capacity = 0;
	ms912x->pending_rect_capacity = 0;
	ms912x->previous_damage_rect_capacity = 0;
}

static void ms912x_free_runtime_buffers(struct ms912x_device *ms912x)
{
	ms912x_free_rect_vectors(&ms912x->drm, ms912x);
	ms912x_free_damage_scan_snapshot(&ms912x->drm, ms912x);
	ms912x_free_conversion_snapshots(&ms912x->drm, ms912x);
	ms912x_free_scan_frames(&ms912x->drm, ms912x);
	ms912x_free_cursor_buffers(&ms912x->drm, ms912x);
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

const char *ms912x_usb_speed_name(enum usb_device_speed speed)
{
	switch (speed) {
	case USB_SPEED_LOW:
		return "Low-Speed (1.5 Mbps)";
	case USB_SPEED_FULL:
		return "Full-Speed (12 Mbps)";
	case USB_SPEED_HIGH:
		return "High-Speed (480 Mbps)";
	case USB_SPEED_WIRELESS:
		return "Wireless USB";
	case USB_SPEED_SUPER:
		return "SuperSpeed (5 Gbps)";
	case USB_SPEED_SUPER_PLUS:
		return "SuperSpeedPlus";
	case USB_SPEED_UNKNOWN:
	default:
		return "unknown";
	}
}

static const char *ms912x_sdram_name(enum ms912x_sdram_type sdram_type)
{
	switch (sdram_type) {
	case MS912X_SDRAM_2M:
		return "2M";
	case MS912X_SDRAM_4M:
		return "4M";
	case MS912X_SDRAM_8M:
		return "8M";
	case MS912X_SDRAM_16M:
		return "16M";
	case MS912X_SDRAM_NONE:
		return "none";
	case MS912X_SDRAM_UNKNOWN:
	default:
		return "unknown";
	}
}

static void ms912x_log_device_found(struct ms912x_device *ms912x,
				    struct usb_device *usbdev)
{
	const char *model = usbdev->product ? usbdev->product : "unknown";
	u16 vendor = le16_to_cpu(usbdev->descriptor.idVendor);
	u16 product = le16_to_cpu(usbdev->descriptor.idProduct);

	if (usbdev->serial && usbdev->serial[0])
		dev_info(&ms912x->intf->dev,
			 "found USB display: model=\"%s\" id=%04x:%04x interface=%s serial=\"%s\" SDRAM=%s USB speed=%s\n",
			 model, vendor, product,
			 dev_name(&ms912x->intf->dev), usbdev->serial,
			 ms912x_sdram_name(READ_ONCE(ms912x->sdram_type)),
			 ms912x_usb_speed_name(usbdev->speed));
	else
		dev_info(&ms912x->intf->dev,
			 "found USB display: model=\"%s\" id=%04x:%04x interface=%s SDRAM=%s USB speed=%s\n",
			 model, vendor, product, dev_name(&ms912x->intf->dev),
			 ms912x_sdram_name(READ_ONCE(ms912x->sdram_type)),
			 ms912x_usb_speed_name(usbdev->speed));
}

static const struct drm_format_info *ms912x_client_primary_format(void)
{
	enum ms912x_primary_format primary_format =
		READ_ONCE(ms912x_parameters.primary_format);

	if (primary_format == MS912X_PRIMARY_FORMAT_RGB565 ||
	    primary_format == MS912X_PRIMARY_FORMAT_EFFICIENCY)
		return drm_format_info(DRM_FORMAT_RGB565);

	return drm_format_info(DRM_FORMAT_XRGB8888);
}

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
	ret = ms912x_init_cursor_buffers(ms912x);
	if (ret)
		return ret;
	ret = devm_mutex_init(&interface->dev, &ms912x->cursor_prepare_lock);
	if (ret)
		return ret;
	INIT_LIST_HEAD(&ms912x->device_list);
	dev = &ms912x->drm;

	atomic_set(&ms912x->damage_scan_invalid, 1);
	ms912x->usb_speed_mean_bytes_per_sec =
		MS912X_USB_INITIAL_RATE_BYTES_PER_SEC;
	ms912x->usb_speed_samples = 1;
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
	ret = devm_mutex_init(&interface->dev, &ms912x->snapshot_lock);
	if (ret)
		return ret;
	ret = devm_mutex_init(&interface->dev, &ms912x->submit_lock);
	if (ret)
		return ret;
	ret = devm_mutex_init(&interface->dev, &ms912x->scan_queue_lock);
	if (ret)
		return ret;
	ret = drmm_add_action_or_reset(dev, ms912x_free_rect_vectors, ms912x);
	if (ret)
		return ret;
	ms912x->scan_rect_index = ms912x_scan_rect_index_alloc();
	if (!ms912x->scan_rect_index)
		return -ENOMEM;
	ms912x->scan_damage_rects = ms912x_rect_vector_grow(NULL,
			&ms912x->scan_damage_rect_capacity,
			MS912X_INITIAL_DAMAGE_RECTS, MS912X_MAX_DAMAGE_RECTS);
	ms912x->submit_damage_rects = ms912x_rect_vector_grow(NULL,
			&ms912x->submit_damage_rect_capacity,
			MS912X_INITIAL_DAMAGE_RECTS, MS912X_MAX_DAMAGE_RECTS);
	ms912x->pending_rects = ms912x_rect_vector_grow(NULL,
			&ms912x->pending_rect_capacity,
			MS912X_INITIAL_DAMAGE_RECTS, MS912X_MAX_DAMAGE_RECTS);
	ms912x->previous_damage_rects = ms912x_rect_vector_grow(NULL,
			&ms912x->previous_damage_rect_capacity,
			MS912X_INITIAL_DAMAGE_RECTS, MS912X_MAX_DAMAGE_RECTS);
	ms912x->payload_compare_rects = kvcalloc(MS912X_MAX_BATCH_RECTS,
			sizeof(*ms912x->payload_compare_rects), GFP_KERNEL);
	if (!ms912x->scan_damage_rects || !ms912x->submit_damage_rects ||
	    !ms912x->pending_rects || !ms912x->previous_damage_rects ||
	    !ms912x->payload_compare_rects)
		return -ENOMEM;
	ms912x->damage_scan_snapshot = vzalloc(
		MS912X_MAX_WIDTH * MS912X_MAX_HEIGHT *
		sizeof(*ms912x->damage_scan_snapshot));
	ms912x->damage_scan_snapshot_back = vzalloc(
		MS912X_MAX_WIDTH * MS912X_MAX_HEIGHT *
		sizeof(*ms912x->damage_scan_snapshot_back));
	ms912x->rgb565_scale_scratch = vzalloc(
		MS912X_MAX_WIDTH * MS912X_MAX_HEIGHT *
		sizeof(*ms912x->rgb565_scale_scratch));
	if (!ms912x->damage_scan_snapshot ||
	    !ms912x->damage_scan_snapshot_back ||
	    !ms912x->rgb565_scale_scratch) {
		vfree(ms912x->rgb565_scale_scratch);
		vfree(ms912x->damage_scan_snapshot_back);
		vfree(ms912x->damage_scan_snapshot);
		return -ENOMEM;
	}
	ret = drmm_add_action_or_reset(dev, ms912x_free_damage_scan_snapshot,
				       ms912x);
	if (ret)
		return ret;
	ms912x->latest_pixels = vzalloc(
		MS912X_MAX_WIDTH * MS912X_MAX_HEIGHT *
		sizeof(*ms912x->latest_pixels));
	ms912x->conversion_pixels = vzalloc(
		MS912X_MAX_WIDTH * MS912X_MAX_HEIGHT *
		sizeof(*ms912x->conversion_pixels));
	if (!ms912x->latest_pixels || !ms912x->conversion_pixels) {
		vfree(ms912x->conversion_pixels);
		vfree(ms912x->latest_pixels);
		ms912x->conversion_pixels = NULL;
		ms912x->latest_pixels = NULL;
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
	ret = devm_mutex_init(&interface->dev, &ms912x->sdram_lock);
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

	usbdev = interface_to_usbdev(interface);
	ret = ms912x_sdram_detect(ms912x);
	if (ret)
		dev_warn(&interface->dev,
			 "failed to detect SDRAM type: %d; display modesets will remain disabled until detection succeeds\n",
			 ret);
	ms912x_log_device_found(ms912x, usbdev);
	ms912x_profile_configure_device(ms912x, usbdev->speed);

	ret = ms912x_timing_load_custom(ms912x);
	if (ret < 0) {
		drm_warn(&ms912x->drm,
			 "failed to read custom timing: %d\n", ret);
	}
	for (i = 0; i < ms912x->num_custom_modes; i++) {
		const struct ms912x_mode *mode = &ms912x->custom_modes[i].mode;

		drm_info(&ms912x->drm,
			 "custom mode %dx%d@%d uses mode 0x%02x\n",
			 mode->width, mode->height, mode->hz, mode->vic);
	}

	ms912x->cursor_plane_enabled =
		READ_ONCE(ms912x_parameters.cursor_plane);
	dev_info(&interface->dev, "cursor plane: %s\n",
		 ms912x->cursor_plane_enabled ? "enabled" : "disabled");
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

	ms912x->workqueue =
		drmm_alloc_ordered_workqueue(dev, MS912X_DRIVER_NAME, 0);
	if (IS_ERR(ms912x->workqueue))
		return PTR_ERR(ms912x->workqueue);
	ms912x->conversion_workqueue =
		drmm_alloc_ordered_workqueue(dev, MS912X_DRIVER_NAME "-convert", 0);
	if (IS_ERR(ms912x->conversion_workqueue))
		return PTR_ERR(ms912x->conversion_workqueue);
	ms912x->scan_workqueue =
		drmm_alloc_ordered_workqueue(dev, MS912X_DRIVER_NAME "-scan", 0);
	if (IS_ERR(ms912x->scan_workqueue))
		return PTR_ERR(ms912x->scan_workqueue);
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

	ret = ms912x_device_sysfs_register(ms912x);
	if (ret) {
		usb_set_intfdata(interface, NULL);
		goto err_free_request_1;
	}

	drmm_kms_helper_poll_init(dev);

	ret = drm_dev_register(dev, 0);
	if (ret) {
		usb_set_intfdata(interface, NULL);
		goto err_free_request_1;
	}

	ms912x_debugfs_register(dev);

	drm_client_setup(dev, ms912x_client_primary_format());
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

	dev_info(&interface->dev, "USB adapter disconnected\n");
	WRITE_ONCE(ms912x->detect_reset_stopping, true);
	cancel_work_sync(&ms912x->detect_reset_work);
	ms912x_device_registry_remove(ms912x);

	ms912x_transfer_stop(ms912x);
	drm_atomic_helper_shutdown(dev);
	drm_dev_unplug(dev);
	ms912x_transfer_free_requests(ms912x);
	ms912x_free_runtime_buffers(ms912x);
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
