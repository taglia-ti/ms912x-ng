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

		MS912X_STAT_INC(ms912x, usb_control_retries);
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
		request.type = MS912X_REQ_TYPE_READ_BYTE;
		request.addr = cpu_to_be16(address);

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
		MS912X_STAT_INC(ms912x, usb_control_retries);
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
		request.type = MS912X_REQ_TYPE_READ_FLASH;
		request.addr[0] = current_address >> 16;
		request.addr[1] = current_address >> 8;
		request.addr[2] = current_address;

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

static int ms912x_timing_register_base(struct ms912x_device *ms912x)
{
	u8 signature[3];
	int ret;

	/* Read chip signature */
	ret = ms912x_register_read_u8(ms912x, MS913X_REG_CHIP_ID);
	if (ret < 0)
		return ret;
	signature[0] = ret;

	ret = ms912x_register_read_u8(ms912x, MS913X_REG_CHIP_ID + 1);
	if (ret < 0)
		return ret;
	signature[1] = ret;

	ret = ms912x_register_read_u8(ms912x, MS913X_REG_CHIP_ID + 2);
	if (ret < 0)
		return ret;
	signature[2] = ret;

	if (signature[1] == MS913X_CHIP_ID_SIGNATURE_MSB &&
	    signature[2] == MS91XX_CHIP_ID_SIGNATURE_LSB)
		return MS913X_CUSTOM_TIMING_BASE;

	ret = ms912x_register_read_u8(ms912x, MS912X_REG_CHIP_ID);
	if (ret < 0)
		return ret;
	signature[0] = ret;

	ret = ms912x_register_read_u8(ms912x, MS912X_REG_CHIP_ID + 1);
	if (ret < 0)
		return ret;
	signature[1] = ret;

	ret = ms912x_register_read_u8(ms912x, MS912X_REG_CHIP_ID + 2);
	if (ret < 0)
		return ret;
	signature[2] = ret;

	if (signature[1] == MS912X_CHIP_ID_SIGNATURE_MSB &&
	    signature[2] == MS91XX_CHIP_ID_SIGNATURE_LSB)
		return MS912X_CUSTOM_TIMING_BASE;

	return -ENODEV;
}

static int
ms912x_timing_decode_record(struct ms912x_custom_mode *custom_mode,
			    const struct ms912x_custom_timing_record *record)
{
	struct drm_display_mode *display_mode = &custom_mode->display_mode;
	u16 hsyncwidth = le16_to_cpu(record->hsyncwidth);
	u16 vsyncwidth = le16_to_cpu(record->vsyncwidth);
	u16 hoffset = le16_to_cpu(record->hoffset);
	u16 voffset = le16_to_cpu(record->voffset);
	u16 hactive = le16_to_cpu(record->hactive);
	u16 vactive = le16_to_cpu(record->vactive);
	u16 htotal = le16_to_cpu(record->htotal);
	u16 vtotal = le16_to_cpu(record->vtotal);
	u16 pixclk = le16_to_cpu(record->pixclk);
	u16 vfreq = le16_to_cpu(record->vfreq);
	unsigned int hblank, vblank;
	int refresh;

	if (!hactive || hactive > MS912X_MAX_WIDTH ||
	    !vactive || vactive > MS912X_MAX_HEIGHT ||
	    htotal <= hactive || vtotal <= vactive || !pixclk || !vfreq)
		return -EINVAL;

	hblank = htotal - hactive;
	vblank = vtotal - vactive;
	if (!hsyncwidth || hsyncwidth > hoffset || hoffset > hblank ||
	    !vsyncwidth || vsyncwidth > voffset || voffset > vblank)
		return -EINVAL;

	memset(display_mode, 0, sizeof(*display_mode));
	display_mode->clock = pixclk * 10;
	display_mode->hdisplay = hactive;
	display_mode->hsync_start = htotal - hoffset;
	display_mode->hsync_end = display_mode->hsync_start + hsyncwidth;
	display_mode->htotal = htotal;
	display_mode->vdisplay = vactive;
	display_mode->vsync_start = vtotal - voffset;
	display_mode->vsync_end = display_mode->vsync_start + vsyncwidth;
	display_mode->vtotal = vtotal;
	display_mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	display_mode->flags =
		record->polarity & MS912X_TIMING_POSITIVE_HSYNC ?
			DRM_MODE_FLAG_PHSYNC : DRM_MODE_FLAG_NHSYNC;
	display_mode->flags |=
		record->polarity & MS912X_TIMING_POSITIVE_VSYNC ?
			DRM_MODE_FLAG_PVSYNC : DRM_MODE_FLAG_NVSYNC;
	if (!(record->polarity & MS912X_TIMING_PROGRESSIVE))
		display_mode->flags |= DRM_MODE_FLAG_INTERLACE;

	refresh = DIV_ROUND_CLOSEST(vfreq, 100);
	if (!refresh || drm_mode_vrefresh(display_mode) != refresh)
		return -EINVAL;

	drm_mode_set_name(display_mode);
	custom_mode->mode.width = hactive;
	custom_mode->mode.height = vactive;
	custom_mode->mode.hz = refresh;
	custom_mode->mode.mode = record->vic;

	return 0;
}

int ms912x_timing_load_custom(struct ms912x_device *ms912x)
{
	struct ms912x_custom_timing_record record;
	unsigned int num_records, i;
	int error = 0;
	u8 marker[7];
	int base, ret;

	ms912x->num_custom_modes = 0;

	base = ms912x_timing_register_base(ms912x);
	if (base < 0)
		return base;

	ret = ms912x_flash_read(ms912x, base, marker, sizeof(marker));
	if (ret)
		return ret;

	if (!memcmp(marker, "modify1", sizeof(marker)))
		num_records = 1;
	else if (!memcmp(marker, "modify2", sizeof(marker)))
		num_records = 2;
	else
		return 0;

	for (i = 0; i < num_records; i++) {
		ret = ms912x_flash_read(ms912x,
					base + MS912X_CUSTOM_TIMING_OFFSET +
						i * MS912X_CUSTOM_TIMING_STRIDE,
					&record, sizeof(record));
		if (ret) {
			error = ret;
			continue;
		}

		ret = ms912x_timing_decode_record(
			&ms912x->custom_modes[ms912x->num_custom_modes],
			&record);
		if (ret) {
			error = ret;
			continue;
		}

		ms912x->num_custom_modes++;
	}

	return ms912x->num_custom_modes ? ms912x->num_custom_modes : error;
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
	request.type = MS912X_REQ_TYPE_WRITE_6_BYTES;
	request.addr = address;
	memcpy(request.data, data, sizeof(request.data));

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

	return ms912x_control_write_payload(ms912x, MS912X_CMD_POWER, data);
}

int ms912x_display_disable(struct ms912x_device *ms912x)
{
	u8 data[6];

	memset(data, 0, sizeof(data));

	return ms912x_control_write_payload(ms912x, MS912X_CMD_POWER, data);
}

int ms912x_display_set_mode(struct ms912x_device *ms912x,
			  const struct ms912x_mode *mode)
{
	int ret;
	u8 data[6];
	struct ms912x_resolution_request resolution_request;
	struct ms912x_mode_request mode_request;

	int width = mode->width;
	int height = mode->height;
	int mode_num = mode->mode;

	/* Program the adapter's required mode sequence. */
	memset(data, 0, sizeof(data));
	data[0] = 0;
	ret = ms912x_control_write_payload(ms912x, MS912X_CMD_UNKNOWN2, data);
	if (ret < 0)
		return ret;

	ret = ms912x_register_read_u8(ms912x, MS912X_REG_MODE_SEQUENCE_0);
	if (ret < 0)
		return ret;
	ret = ms912x_register_read_u8(ms912x, MS912X_REG_MODE_SEQUENCE_1);
	if (ret < 0)
		return ret;
	ret = ms912x_register_read_u8(ms912x, MS912X_REG_MODE_SEQUENCE_2);
	if (ret < 0)
		return ret;

	memset(data, 0, sizeof(data));
	data[0] = 0x03;
	ret = ms912x_control_write_payload(ms912x, MS912X_CMD_UNKNOWN1, data);
	if (ret < 0)
		return ret;

	/* Write resolution */
	resolution_request.width = cpu_to_be16(width);
	resolution_request.height = cpu_to_be16(height);
	resolution_request.pixel_format = MS912X_PIXFMT_UYVY;
	resolution_request.byte_select = MS912X_BYTE_SELECT_UYVY;
	ret = ms912x_control_write_payload(ms912x, MS912X_CMD_RESOLUTION,
				   &resolution_request);
	if (ret < 0)
		return ret;

	/* Write mode */
	mode_request.mode = mode_num;
	mode_request.pixel_format = 0x01;
	mode_request.width = cpu_to_be16(width);
	mode_request.height = cpu_to_be16(height);
	ret = ms912x_control_write_payload(ms912x, MS912X_CMD_MODE, &mode_request);
	if (ret < 0)
		return ret;

	memset(data, 0, sizeof(data));
	data[0] = 1;
	ret = ms912x_control_write_payload(ms912x, MS912X_CMD_UNKNOWN2, data);
	if (ret < 0)
		return ret;

	memset(data, 0, sizeof(data));
	data[0] = 1;
	ret = ms912x_control_write_payload(ms912x, MS912X_CMD_OUTPUT_ENABLE, data);
	if (ret < 0)
		return ret;

	return 0;
}
