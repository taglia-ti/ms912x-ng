// SPDX-License-Identifier: GPL-2.0-only

#include <asm/byteorder.h>
#include <linux/delay.h>
#include <linux/hid.h>
#include <linux/mutex.h>
#include <linux/string.h>
#include <linux/usb.h>

#include <drm/drm_drv.h>
#include <drm/drm_modes.h>

#include "ms912x.h"
#include "shared/ms912x_control_core.h"
#include "shared/ms912x_timing_core.h"

#define MS912X_CTRL_TIMEOUT_MS 1000
#define MS912X_CTRL_RETRIES 2

static void ms912x_log_control_error(struct ms912x_device *ms912x,
				     const char *operation, u32 address,
				     int ret)
{
	dev_err_ratelimited(&ms912x->intf->dev,
			     "USB control %s at 0x%04x failed: %d\n",
			     operation, address, ret);
}

static bool ms912x_control_error_retryable(int error)
{
	/* -ENODEV/-ESHUTDOWN mean the device is gone; retrying cannot help. */
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

static int ms912x_control_msg_send_retry(struct ms912x_device *ms912x,
					 struct usb_device *usb_dev,
					 unsigned int pipe, __u8 request,
					 __u8 requesttype, __u16 value,
					 __u16 index, void *data, __u16 size,
					 int timeout, gfp_t mem_flags)
{
	unsigned int attempt;
	int ret;

	for (attempt = 0; ; attempt++) {
		ret = usb_control_msg_send(usb_dev, pipe, request, requesttype,
					   value, index, data, size, timeout,
					   mem_flags);
		if (!ret || attempt == MS912X_CTRL_RETRIES ||
		    !ms912x_control_error_retryable(ret))
			return ret;

		usleep_range(10000U << attempt, 15000U << attempt);
	}
}

static int ms912x_register_read_u8_locked(struct ms912x_device *ms912x,
					  struct usb_device *usb_dev,
					  u16 address)
{
	struct ms912x_request request;
	unsigned int attempt;
	int ret;

	for (attempt = 0; attempt <= MS912X_CTRL_RETRIES; attempt++) {
		memset(&request, 0, sizeof(request));
		ms912x_build_register_read((u8 *)&request, address);

		ret = usb_control_msg_send(usb_dev, 0, HID_REQ_SET_REPORT,
					   USB_DIR_OUT | USB_TYPE_CLASS |
						   USB_RECIP_INTERFACE,
					   0x0300, 0, &request, sizeof(request),
					   MS912X_CTRL_TIMEOUT_MS, GFP_KERNEL);
		if (ret) {
			ms912x_log_control_error(ms912x, "SET_REPORT", address,
						 ret);
		} else {
			ret = usb_control_msg_recv(usb_dev, 0, HID_REQ_GET_REPORT,
						   USB_DIR_IN | USB_TYPE_CLASS |
							   USB_RECIP_INTERFACE,
						   0x0300, 0, &request,
						   sizeof(request),
						   MS912X_CTRL_TIMEOUT_MS,
						   GFP_KERNEL);
			if (!ret)
				return request.data[0];
			ms912x_log_control_error(ms912x, "GET_REPORT", address,
						 ret);
		}

		if (attempt == MS912X_CTRL_RETRIES ||
		    !ms912x_control_error_retryable(ret))
			return ret;
		usleep_range(10000U << attempt, 15000U << attempt);
	}

	return -EIO;
}

int ms912x_register_read_u8(struct ms912x_device *ms912x, u16 address)
{
	struct usb_device *usb_dev;
	int idx, ret;

	if (!drm_dev_enter(&ms912x->drm, &idx))
		return -ENODEV;

	mutex_lock(&ms912x->ctrl_lock);
	usb_dev = interface_to_usbdev(ms912x->intf);
	ret = ms912x_register_read_u8_locked(ms912x, usb_dev, address);
	mutex_unlock(&ms912x->ctrl_lock);
	drm_dev_exit(idx);

	return ret;
}

int ms912x_register_read_block(struct ms912x_device *ms912x, u16 address,
			       void *data, size_t len)
{
	const size_t header_size = offsetof(struct ms912x_block_read_request, data);
	struct ms912x_block_read_request *request;
	struct usb_device *usb_dev;
	size_t offset = 0;
	int idx, ret = 0;

	if (!len)
		return 0;
	if (!data)
		return -EINVAL;
	if (len - 1 > U16_MAX - address)
		return -ERANGE;

	if (!drm_dev_enter(&ms912x->drm, &idx))
		return -ENODEV;

	request = kzalloc(sizeof(*request), GFP_KERNEL);
	if (!request) {
		drm_dev_exit(idx);
		return -ENOMEM;
	}

	mutex_lock(&ms912x->ctrl_lock);
	usb_dev = interface_to_usbdev(ms912x->intf);

	while (offset < len) {
		size_t chunk = min_t(size_t, len - offset,
				    MS912X_BLOCK_READ_CHUNK_SIZE);
		u16 chunk_address = address + offset;

		memset(request, 0, sizeof(*request));
		request->type = MS912X_REQ_TYPE_READ_BLOCK;
		request->addr = cpu_to_be16(chunk_address);

		ret = ms912x_control_msg_send_retry(ms912x, usb_dev, 0,
					   HID_REQ_SET_REPORT,
					   USB_DIR_OUT | USB_TYPE_CLASS |
						   USB_RECIP_INTERFACE,
					   0x0300, 0, request,
					   sizeof(struct ms912x_request),
					   MS912X_CTRL_TIMEOUT_MS, GFP_KERNEL);
		if (ret) {
			ms912x_log_control_error(ms912x, "block SET_REPORT",
						 chunk_address, ret);
			goto ctrl_unlock;
		}

		/* The feature report has a 3-byte command/address prefix. */
		ret = usb_control_msg(usb_dev, usb_rcvctrlpipe(usb_dev, 0),
				      HID_REQ_GET_REPORT,
				      USB_DIR_IN | USB_TYPE_CLASS |
					      USB_RECIP_INTERFACE,
				      0x0300, 0, request, sizeof(*request),
				      MS912X_CTRL_TIMEOUT_MS);
		if (ret < 0) {
			ms912x_log_control_error(ms912x, "block GET_REPORT",
						 chunk_address, ret);
			goto ctrl_unlock;
		}
		if (ret < header_size + chunk) {
			dev_dbg_ratelimited(&ms912x->intf->dev,
					    "block GET_REPORT at 0x%04x returned %d bytes; need %zu\n",
					    chunk_address, ret,
					    header_size + chunk);
			ret = -EOPNOTSUPP;
			goto ctrl_unlock;
		}

		memcpy((u8 *)data + offset, request->data, chunk);
		offset += chunk;
	}

	ret = 0;

ctrl_unlock:
	mutex_unlock(&ms912x->ctrl_lock);
	kfree(request);
	drm_dev_exit(idx);
	return ret;
}

static int ms912x_flash_read(struct ms912x_device *ms912x, u32 address,
			     void *data, size_t len)
{
	struct ms912x_flash_read_request request;
	struct usb_device *usb_dev;
	size_t offset = 0;
	u8 *dst = data;
	int idx, ret = 0;

	if (address > 0xffffff || len > 0x1000000 - address)
		return -ERANGE;

	if (!drm_dev_enter(&ms912x->drm, &idx))
		return -ENODEV;

	mutex_lock(&ms912x->ctrl_lock);
	usb_dev = interface_to_usbdev(ms912x->intf);

	while (offset < len) {
		size_t chunk = min_t(size_t, len - offset, sizeof(request));
		u32 current_address = address + offset;

		memset(&request, 0, sizeof(request));
		ms912x_build_flash_read((u8 *)&request, current_address);

		ret = ms912x_control_msg_send_retry(ms912x, usb_dev, 0,
					   HID_REQ_SET_REPORT,
					   USB_DIR_OUT | USB_TYPE_CLASS |
						   USB_RECIP_INTERFACE,
					   0x0300, 0, &request,
					   sizeof(request),
					   USB_CTRL_SET_TIMEOUT, GFP_KERNEL);
		if (ret) {
			ms912x_log_control_error(ms912x, "flash SET_REPORT",
					 current_address, ret);
			goto ctrl_unlock;
		}

		ret = usb_control_msg_recv(usb_dev, 0, HID_REQ_GET_REPORT,
					   USB_DIR_IN | USB_TYPE_CLASS |
						   USB_RECIP_INTERFACE,
					   0x0300, 0, &request,
					   sizeof(request),
					   USB_CTRL_GET_TIMEOUT, GFP_KERNEL);
		if (ret) {
			ms912x_log_control_error(ms912x, "flash GET_REPORT",
					 current_address, ret);
			goto ctrl_unlock;
		}

		memcpy(dst + offset, &request, chunk);
		offset += chunk;
	}

ctrl_unlock:
	mutex_unlock(&ms912x->ctrl_lock);
	drm_dev_exit(idx);
	return ret;
}

static int ms912x_timing_read(void *context, unsigned int address)
{
    return ms912x_register_read_u8(context, address);
}
static int ms912x_timing_flash(void *context, unsigned int address, void *data, unsigned int length)
{
    return ms912x_flash_read(context, address, data, length);
}
int ms912x_timing_load_custom(struct ms912x_device *ms912x)
{
    const struct ms912x_timing_io ops = {ms912x_timing_read, ms912x_timing_flash};
    struct ms912x_mode modes[2];
    int count, i, ret;
    ms912x->num_custom_modes = 0;
    count = ms912x_custom_timing_load(ms912x, &ops, modes);
    if (count < 0) return count;
    for (i = 0; i < count; ++i) {
        struct ms912x_custom_mode *custom = &ms912x->custom_modes[ms912x->num_custom_modes];
        ret = ms912x_mode_to_display_mode(ms912x, &modes[i], &custom->display_mode);
        if (ret) return ret;
        custom->mode = modes[i];
        custom->display_mode.type |= DRM_MODE_TYPE_PREFERRED;
        ms912x->num_custom_modes++;
    }
    return ms912x->num_custom_modes;
}

static int ms912x_control_write_payload(struct ms912x_device *ms912x,
				u8 address, const void *data)
{
	struct ms912x_write_request request;
	struct usb_device *usb_dev;
	int idx, ret;

	if (!drm_dev_enter(&ms912x->drm, &idx))
		return -ENODEV;

	mutex_lock(&ms912x->ctrl_lock);

	usb_dev = interface_to_usbdev(ms912x->intf);
	ms912x_build_control_write((u8 *)&request, address, data);

	ret = ms912x_control_msg_send_retry(ms912x, usb_dev, 0, HID_REQ_SET_REPORT,
					   USB_DIR_OUT | USB_TYPE_CLASS |
						   USB_RECIP_INTERFACE,
				   0x0300, 0, &request, sizeof(request),
				   MS912X_CTRL_TIMEOUT_MS, GFP_KERNEL);
	if (ret)
		ms912x_log_control_error(ms912x, "write SET_REPORT", address,
					 ret);
	mutex_unlock(&ms912x->ctrl_lock);
	drm_dev_exit(idx);

	return ret;
}

int ms912x_display_enable(struct ms912x_device *ms912x)
{
	u8 data[6];

	memset(data, 0, sizeof(data));
	data[0] = 0x01;
	data[1] = 0x02;

	return ms912x_control_write_payload(ms912x, MS912X_CMD_POWERON, data);
}

int ms912x_display_disable(struct ms912x_device *ms912x)
{
	u8 data[6];

	memset(data, 0, sizeof(data));

	return ms912x_control_write_payload(ms912x, MS912X_CMD_POWERON, data);
}

int ms912x_sdram_detect(struct ms912x_device *ms912x)
{
	enum ms912x_sdram_type sdram_type;
	int value, ret = 0;

	mutex_lock(&ms912x->sdram_lock);
	if (READ_ONCE(ms912x->sdram_type) != MS912X_SDRAM_UNKNOWN)
		goto unlock;

	value = ms912x_register_read_u8(ms912x, MS912X_REG_SDRAM_TYPE);
	if (value < 0) {
		ret = value;
		goto unlock;
	}

	sdram_type = ms912x_sdram_type_from_register(value);
	WRITE_ONCE(ms912x->sdram_type, sdram_type);
	if (sdram_type == MS912X_SDRAM_UNKNOWN) {
		dev_warn(&ms912x->intf->dev,
			 "unrecognized SDRAM type value: %d\n", value);
		ret = -EINVAL;
		goto unlock;
	}

	if (sdram_type == MS912X_SDRAM_NONE)
		dev_warn(&ms912x->intf->dev,
			 "no external SDRAM detected; this driver does not support SDRAM-less operation, display modesets are disabled\n");

	unlock:
	mutex_unlock(&ms912x->sdram_lock);
	return ret;
}

static int ms912x_mode_write(void *context, unsigned char command, const unsigned char data[6])
{
	return ms912x_control_write_payload(context, command, data);
}

static int ms912x_mode_read(void *context, unsigned int address)
{
	return ms912x_register_read_u8(context, address);
}

int ms912x_display_set_mode(struct ms912x_device *ms912x,
			  const struct ms912x_mode *mode)
{
	int ret;
	static const struct ms912x_control_ops ops = {
		.write = ms912x_mode_write, .read = ms912x_mode_read,
	};
	struct usb_device *usbdev = interface_to_usbdev(ms912x->intf);

	int width = mode->width;
	int height = mode->height;
	unsigned int input_width, input_height;
	enum ms912x_scale_mode scale_mode;
	unsigned int requested_scale;
	int config_generation;
	u32 device_config_generation;
	bool geometry_changed;
	int sdram_ret;

	ms912x_scale_snapshot(ms912x, &requested_scale, &config_generation,
			      &device_config_generation);

	if (READ_ONCE(ms912x->sdram_type) == MS912X_SDRAM_UNKNOWN) {
		sdram_ret = ms912x_sdram_detect(ms912x);
		if (sdram_ret)
			return sdram_ret;
	}
	if (READ_ONCE(ms912x->sdram_type) == MS912X_SDRAM_NONE)
		return -EOPNOTSUPP;

	scale_mode = ms912x_scale_mode_from_setting(requested_scale);
	if (requested_scale == MS912X_SCALE_AUTO)
		scale_mode = ms912x_scale_auto_select(
			width, height, mode->hz,
			usbdev->speed != USB_SPEED_UNKNOWN &&
			usbdev->speed < USB_SPEED_SUPER);
	ms912x_scale_input_geometry(width, height, scale_mode,
				    &input_width, &input_height);

	ret = ms912x_program_mode(ms912x, &ops, mode->vic, input_width, input_height);
	if (ret < 0)
		return ret;

	mutex_lock(&ms912x->transfer_lock);
	geometry_changed = ms912x->framebuffer_width != width ||
		ms912x->framebuffer_height != height ||
		ms912x->video_in_width != input_width ||
		ms912x->video_in_height != input_height ||
		ms912x->scale_mode != scale_mode ||
		ms912x->scale_config_generation != config_generation ||
		ms912x->scale_device_applied_generation !=
			device_config_generation;
	if (geometry_changed) {
		ms912x->frame_generation++;
		ms912x->previous_damage_rect_count = 0;
		ms912x->pending_rect_count = 0;
		ms912x->pending_valid = false;
		ms912x->pending_force_full = false;
		ms912x->pending_deadline = 0;
		ms912x_damage_scan_invalidate(ms912x);
	}
	ms912x->framebuffer_width = width;
	ms912x->framebuffer_height = height;
	ms912x->video_in_width = input_width;
	ms912x->video_in_height = input_height;
	if (geometry_changed) {
		ms912x->frame_snapshot_width = 0;
		ms912x->frame_snapshot_height = 0;
	}
	ms912x->scale_mode = scale_mode;
	ms912x->scale_config_generation = config_generation;
	ms912x->scale_device_applied_generation = device_config_generation;
	mutex_unlock(&ms912x->transfer_lock);
	ms912x_cursor_reconfigure(ms912x);
	if (requested_scale != MS912X_SCALE_AUTO &&
	    scale_mode != MS912X_SCALE_MODE_NATIVE &&
	    input_width > MS912X_SCALE_AUTO_MAX_INPUT_WIDTH)
		dev_warn_ratelimited(&ms912x->intf->dev,
			"manual scale %u%% sets VIDEO_IN width to %u pixels, above the %u-pixel scaled-input limit; output may be corrupted\n",
			requested_scale, input_width,
			MS912X_SCALE_AUTO_MAX_INPUT_WIDTH);
	dev_info(&ms912x->intf->dev,
		 "scale mode %s (setting %u): VIDEO_IN/VIDEO_OUT canvas %ux%u, output mode %dx%d\n",
		 ms912x_scale_mode_name(scale_mode),
		 ms912x_scale_setting_for_mode(scale_mode), input_width, input_height,
		 width, height);
	return 0;
}
