// SPDX-License-Identifier: GPL-2.0-only

#include <linux/array_size.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/jiffies.h>
#include <linux/limits.h>
#include <linux/slab.h>
#include <linux/string.h>

#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_connector.h>
#include <drm/drm_edid.h>
#include <drm/drm_encoder.h>
#include <drm/drm_modes.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_print.h>

#include "ms912x.h"
#include "ms912x_edid.h"
#include "ms912x_fake_edid.h"

static bool ms912x_edid_encode_detailed_timing(
					 u8 *descriptor,
					 const struct drm_display_mode *mode)
{
	u32 sync_flags = 0;

	if (mode->flags & DRM_MODE_FLAG_PVSYNC)
		sync_flags |= MS912X_EDID_SYNC_POSITIVE_VSYNC;
	if (mode->flags & DRM_MODE_FLAG_PHSYNC)
		sync_flags |= MS912X_EDID_SYNC_POSITIVE_HSYNC;
	if (mode->flags & DRM_MODE_FLAG_INTERLACE)
		sync_flags |= MS912X_EDID_SYNC_INTERLACED;

	return ms912x_edid_encode_detailed_timing_fields(
		descriptor, mode->clock, mode->hdisplay, mode->hsync_start,
		mode->hsync_end, mode->htotal, mode->vdisplay,
		mode->vsync_start, mode->vsync_end, mode->vtotal, sync_flags);
}

static unsigned int ms912x_edid_dtd_offset(unsigned int index)
{
	if (index < MS912X_EDID_BASE_DTD_COUNT)
		return 54 + index * 18;

	index -= MS912X_EDID_BASE_DTD_COUNT;
	return (1 + index / MS912X_EDID_CTA_DTD_COUNT) * EDID_LENGTH + 4 +
		(index % MS912X_EDID_CTA_DTD_COUNT) * 18;
}

static unsigned int ms912x_edid_block_for_dtd(unsigned int index)
{
	if (index < MS912X_EDID_BASE_DTD_COUNT)
		return 0;

	return 1 + (index - MS912X_EDID_BASE_DTD_COUNT) /
		MS912X_EDID_CTA_DTD_COUNT;
}

static void ms912x_edid_update_range(u8 *edid,
				     const struct ms912x_fake_edid_mode_info *modes,
				     unsigned int mode_count)
{
	unsigned int min_hfreq = U32_MAX, max_hfreq = 0;
	unsigned int min_vrefresh = U32_MAX, max_vrefresh = 0;
	unsigned int max_pixel_clock = 0, i;
	u8 *range = edid + 108;

	for (i = 0; i < mode_count; i++) {
		min_hfreq = min(min_hfreq, modes[i].hfreq_khz);
		max_hfreq = max(max_hfreq, modes[i].hfreq_khz);
		min_vrefresh = min(min_vrefresh, (unsigned int)modes[i].hz);
		max_vrefresh = max(max_vrefresh, (unsigned int)modes[i].hz);
		max_pixel_clock = max(max_pixel_clock,
				      (unsigned int)modes[i].clock_10mhz);
	}

	if (max(min_hfreq, max_hfreq) > U8_MAX ||
	    max(min_vrefresh, max_vrefresh) > U8_MAX ||
	    max_pixel_clock > U8_MAX)
		memset(range, 0, 18);
	else {
		range[3] = 0xfd;
		range[5] = min_vrefresh;
		range[6] = max_vrefresh;
		range[7] = min_hfreq;
		range[8] = max_hfreq;
		range[9] = max_pixel_clock;
	}
	ms912x_edid_set_block_checksum(edid);
}

static const struct drm_edid *
ms912x_edid_create_synthetic(struct ms912x_device *ms912x)
{
	struct ms912x_fake_edid_mode_info modes[
		MS912X_FAKE_EDID_MODE_CAPACITY];
	unsigned int mode_count = MS912X_FAKE_EDID_FIXED_MODE_COUNT;
	u8 *fake_edid;
	const struct drm_edid *edid;
	unsigned int i, j, block;

	if (!ms912x->num_custom_modes)
		edid = drm_edid_alloc(ms912x_fake_edid_data,
				      sizeof(ms912x_fake_edid_data));
	else {
		fake_edid = kmemdup(ms912x_fake_edid_data,
				    sizeof(ms912x_fake_edid_data), GFP_KERNEL);
		if (!fake_edid)
			return NULL;

		for (i = 0; i < mode_count; i++)
			modes[i] = ms912x_fake_edid_modes[i];

		for (i = 0; i < ms912x->num_custom_modes; i++) {
			const struct ms912x_custom_mode *custom =
				&ms912x->custom_modes[i];
			const struct ms912x_mode *custom_mode = &custom->mode;
			struct drm_display_mode timing;
			u8 dtd[18];
			unsigned int mode_index;
			bool already_advertised = false;

			timing = custom->display_mode;
			if (!ms912x_edid_encode_detailed_timing(dtd, &timing)) {
				kfree(fake_edid);
				return NULL;
			}
			for (j = 0; j < mode_count; j++) {
				if (!memcmp(fake_edid + ms912x_edid_dtd_offset(j), dtd,
					    sizeof(dtd))) {
					already_advertised = true;
					break;
				}
			}
			if (already_advertised)
				continue;
			if (mode_count >= MS912X_FAKE_EDID_MODE_CAPACITY) {
				kfree(fake_edid);
				return NULL;
			}
			mode_index = mode_count++;
			memcpy(fake_edid + ms912x_edid_dtd_offset(mode_index), dtd,
			       sizeof(dtd));
			block = ms912x_edid_block_for_dtd(mode_index);
			ms912x_edid_set_block_checksum(fake_edid + block * EDID_LENGTH);

			modes[mode_index].width = custom_mode->width;
			modes[mode_index].height = custom_mode->height;
			modes[mode_index].hz = custom_mode->hz;
			modes[mode_index].hfreq_khz =
				DIV_ROUND_UP((u32)timing.clock, timing.htotal);
			modes[mode_index].clock_10mhz =
				DIV_ROUND_UP((u32)timing.clock, 10000);
		}
		ms912x_edid_update_range(fake_edid, modes, mode_count);
		edid = drm_edid_alloc(fake_edid, sizeof(ms912x_fake_edid_data));
		kfree(fake_edid);
	}

	if (edid && !READ_ONCE(ms912x->fake_edid_active)) {
		drm_warn(&ms912x->drm,
			 "using synthetic EDID; advertised modes may not match the connected display\n");
		WRITE_ONCE(ms912x->fake_edid_active, true);
	}

	return edid;
}

static int ms912x_connector_detect_type(struct ms912x_device *ms912x)
{
	int port_type;

	port_type = ms912x_register_read_u8(ms912x, MS912X_REG_VIDEO_PORT);
	if (port_type < 0) {
		drm_warn(&ms912x->drm,
			 "failed to read video port type: %d\n", port_type);
		ms912x->port_type = MS912X_VIDEO_PORT_UNKNOWN;
		return DRM_MODE_CONNECTOR_Unknown;
	}

	ms912x->port_type = port_type;

	switch (ms912x->port_type) {
	case MS912X_VIDEO_PORT_CVBS:
		return DRM_MODE_CONNECTOR_Composite;
	case MS912X_VIDEO_PORT_SVIDEO:
		return DRM_MODE_CONNECTOR_SVIDEO;
	case MS912X_VIDEO_PORT_VGA:
		return DRM_MODE_CONNECTOR_VGA;
	case MS912X_VIDEO_PORT_YPBPR:
		return DRM_MODE_CONNECTOR_Component;
	case MS912X_VIDEO_PORT_HDMI:
		return DRM_MODE_CONNECTOR_HDMIA;
	case MS912X_VIDEO_PORT_DIGITAL:
		return DRM_MODE_CONNECTOR_DPI;
	case MS912X_VIDEO_PORT_CVBS_SVIDEO:
		return DRM_MODE_CONNECTOR_9PinDIN;
	default:
		drm_warn(&ms912x->drm,
			 "unknown video port type: %d\n", port_type);
		ms912x->port_type = MS912X_VIDEO_PORT_UNKNOWN;
		return DRM_MODE_CONNECTOR_Unknown;
	}
}

static int
ms912x_encoder_type_for_port(enum ms912x_video_port port_type)
{
	switch (port_type) {
	case MS912X_VIDEO_PORT_VGA:
		return DRM_MODE_ENCODER_DAC;
	case MS912X_VIDEO_PORT_CVBS:
	case MS912X_VIDEO_PORT_SVIDEO:
	case MS912X_VIDEO_PORT_YPBPR:
	case MS912X_VIDEO_PORT_CVBS_SVIDEO:
		return DRM_MODE_ENCODER_TVDAC;
	case MS912X_VIDEO_PORT_HDMI:
		return DRM_MODE_ENCODER_TMDS;
	case MS912X_VIDEO_PORT_DIGITAL:
		return DRM_MODE_ENCODER_DPI;
	case MS912X_VIDEO_PORT_UNKNOWN:
	default:
		return DRM_MODE_ENCODER_NONE;
	}
}

struct ms912x_edid_read_context {
	struct ms912x_device *ms912x;
	int read_error;
	bool malformed_header;
};

static bool ms912x_edid_repair_header_byte1(u8 *block, size_t len)
{
	static const u8 header_tail[] = {
		0xff, 0xff, 0xff, 0xff, 0xff, 0x00,
	};
	u8 checksum = 0;
	size_t i;

	if (len != EDID_LENGTH || block[0] != 0x00 || block[1] != 0x00 ||
	    memcmp(block + 2, header_tail, sizeof(header_tail)))
		return false;

	block[1] = 0xff;
	for (i = 0; i < len; i++)
		checksum += block[i];
	if (checksum)
		block[1] = 0x00;

	return !checksum;
}

static int ms912x_edid_read_block(void *data, u8 *buf, unsigned int block,
				  size_t len)
{
	static const u8 edid_header[] = {
		0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00,
	};
	struct ms912x_edid_read_context *context = data;
	struct ms912x_device *ms912x = context->ms912x;
	size_t offset = (size_t)block * EDID_LENGTH;
	size_t i;
	u16 address;
	bool cacheable = len == MS912X_EDID_CACHE_BLOCK_SIZE &&
		block < MS912X_EDID_CACHE_BLOCKS;
	bool check_header = block == 0 && len >= sizeof(edid_header);
	bool malformed_header = false;
	int ret;

	if (offset > U16_MAX - MS912X_REG_EDID_BASE ||
	    (len && len - 1 > U16_MAX - MS912X_REG_EDID_BASE - offset)) {
		context->read_error = -ERANGE;
		return -ERANGE;
	}

	mutex_lock(&ms912x->edid_lock);
	if (cacheable && ms912x->edid_cache_valid[block]) {
		memcpy(buf, ms912x->edid_cache[block], len);
		mutex_unlock(&ms912x->edid_lock);
		return 0;
	}
	if (cacheable)
		ms912x->edid_cache_valid[block] = false;

	if (!ms912x->edid_block_read_unsupported) {
		ret = ms912x_register_read_block(ms912x,
						 MS912X_REG_EDID_BASE + offset,
						 buf, len);
		if (ret == -EOPNOTSUPP) {
			ms912x->edid_block_read_unsupported = true;
			dev_info(&ms912x->intf->dev,
				 "block EDID reports unsupported; using byte reads\n");
		} else if (ret) {
			goto unlock_edid;
		}
	}

	if (ms912x->edid_block_read_unsupported) {
		address = MS912X_REG_EDID_BASE + offset;
		for (i = 0; i < len; i++) {
			ret = ms912x_register_read_u8(ms912x, address + i);
			if (ret < 0)
				goto unlock_edid;
			buf[i] = ret;
		}
	}
	ret = 0;
	if (check_header && memcmp(buf, edid_header, sizeof(edid_header))) {
		if (!ms912x_edid_repair_header_byte1(buf, len)) {
			ret = -EINVAL;
			malformed_header = true;
			goto unlock_edid;
		}
		dev_warn_ratelimited(&ms912x->intf->dev,
			"EDID base header byte 1 was 0x00; corrected to 0xff (block checksum valid)\n");
	}

	if (cacheable) {
		memcpy(ms912x->edid_cache[block], buf, len);
		ms912x->edid_cache_valid[block] = true;
	}

unlock_edid:
	if (malformed_header)
		context->malformed_header = true;
	else if (ret)
		context->read_error = ret;
	mutex_unlock(&ms912x->edid_lock);

	return ret;
}

static void ms912x_edid_cache_invalidate(struct ms912x_device *ms912x)
{
	mutex_lock(&ms912x->edid_lock);
	memset(ms912x->edid_cache_valid, 0,
	       sizeof(ms912x->edid_cache_valid));
	mutex_unlock(&ms912x->edid_lock);
}

static int ms912x_connector_add_cea_modes(struct drm_connector *connector,
				const u8 *vics, size_t num_vics)
{
	struct drm_display_mode *mode;
	unsigned int i;
	int count = 0;

	drm_edid_connector_update(connector, NULL);

	for (i = 0; i < num_vics; i++) {
		mode = drm_display_mode_from_cea_vic(connector->dev, vics[i]);
		if (!mode)
			continue;

		if (!count)
			mode->type |= DRM_MODE_TYPE_PREFERRED;
		drm_mode_probed_add(connector, mode);
		count++;
	}

	return count;
}

static int ms912x_connector_add_sd_modes(struct drm_connector *connector)
{
	static const u8 vics[] = {
		2,  /* 720x480p60 */
		17, /* 720x576p50 */
	};

	return ms912x_connector_add_cea_modes(connector, vics, ARRAY_SIZE(vics));
}

static int ms912x_connector_add_component_modes(struct drm_connector *connector)
{
	static const u8 vics[] = {
		4,  /* 1280x720p60 */
		16, /* 1920x1080p60 */
		2,  /* 720x480p60 */
		17, /* 720x576p50 */
	};

	return ms912x_connector_add_cea_modes(connector, vics, ARRAY_SIZE(vics));
}

static int
ms912x_connector_add_default_modes(struct drm_connector *connector)
{
	struct drm_mode_config *mode_config = &connector->dev->mode_config;
	int count;

	drm_edid_connector_update(connector, NULL);
	count = drm_add_modes_noedid(connector, mode_config->max_width,
				     mode_config->max_height);
	if (count)
		drm_set_preferred_mode(connector, 1024, 768);

	return count;
}

static bool ms912x_port_supports_edid(enum ms912x_video_port port_type)
{
	return port_type == MS912X_VIDEO_PORT_VGA ||
		port_type == MS912X_VIDEO_PORT_HDMI ||
		port_type == MS912X_VIDEO_PORT_DIGITAL ||
		port_type == MS912X_VIDEO_PORT_UNKNOWN;
}

static const struct drm_edid *
ms912x_connector_read_edid(struct drm_connector *connector,
			   enum ms912x_fake_edid_mode fake_edid_mode)
{
	struct ms912x_device *ms912x = to_ms912x(connector->dev);
	struct ms912x_edid_read_context context = {
		.ms912x = ms912x,
	};
	const struct drm_edid *edid;
	bool malformed;

	if (fake_edid_mode == MS912X_FAKE_EDID_ON &&
	    ms912x_port_supports_edid(ms912x->port_type)) {
		edid = ms912x_edid_create_synthetic(ms912x);
	} else {
		edid = drm_edid_read_custom(connector, ms912x_edid_read_block,
					   &context);
		if (edid && drm_edid_valid(edid)) {
			WRITE_ONCE(ms912x->fake_edid_active, false);
			return edid;
		}

		malformed = context.malformed_header ||
			(edid && !drm_edid_valid(edid));
		if (edid) {
			drm_edid_free(edid);
			edid = NULL;
		}
		if (!context.read_error && malformed &&
		    fake_edid_mode == MS912X_FAKE_EDID_AUTO &&
		    READ_ONCE(ms912x->detect_status_valid) &&
		    READ_ONCE(ms912x->detect_status) ==
							connector_status_connected &&
		    ms912x_port_supports_edid(ms912x->port_type))
			edid = ms912x_edid_create_synthetic(ms912x);
	}

	if (!edid)
		WRITE_ONCE(ms912x->fake_edid_active, false);
	return edid;
}

static int ms912x_connector_publish_edid_modes(
				struct drm_connector *connector,
				const struct drm_edid *edid)
{
	struct ms912x_device *ms912x = to_ms912x(connector->dev);
	int count = 0;

	if (READ_ONCE(ms912x->monitor_log_pending)) {
		char monitor_name[32] = {};
		bool log_connection = false;

		drm_edid_get_monitor_name(drm_edid_raw(edid), monitor_name,
					  sizeof(monitor_name));
		if (!monitor_name[0])
			strscpy(monitor_name, "Generic PNP Monitor",
				sizeof(monitor_name));
		mutex_lock(&ms912x->detect_lock);
		if (ms912x->monitor_log_pending) {
			ms912x->monitor_log_pending = false;
			log_connection = true;
		}
		mutex_unlock(&ms912x->detect_lock);
		if (log_connection)
			drm_info(&ms912x->drm, "monitor EDID name: %s\n",
				 monitor_name);
	}

	if (drm_edid_connector_update(connector, edid) >= 0)
		count = drm_edid_connector_add_modes(connector);
	drm_edid_free(edid);
	return count;
}

static int ms912x_connector_add_custom_timing(struct drm_connector *connector)
{
	struct ms912x_device *ms912x = to_ms912x(connector->dev);
	struct drm_display_mode *mode;
	unsigned int i;
	int count = 0;

	for (i = 0; i < ms912x->num_custom_modes; i++) {
		mode = drm_mode_duplicate(
			connector->dev,
			&ms912x->custom_modes[i].display_mode);
		if (!mode)
			continue;

		drm_mode_probed_add(connector, mode);
		count++;
	}

	return count;
}

static int ms912x_connector_modes_get(struct drm_connector *connector)
{
	struct ms912x_device *ms912x = to_ms912x(connector->dev);
	enum ms912x_fake_edid_mode fake_edid_mode =
		READ_ONCE(ms912x_parameters.fake_edid);
	const struct drm_edid *edid;
	bool force_edid = fake_edid_mode == MS912X_FAKE_EDID_ON &&
		ms912x_port_supports_edid(ms912x->port_type);
	int ret = ms912x_connector_add_custom_timing(connector);

	if (READ_ONCE(ms912x->sdram_type) == MS912X_SDRAM_UNKNOWN)
		ms912x_sdram_detect(ms912x);

	if (!force_edid &&
	    (ms912x->port_type == MS912X_VIDEO_PORT_CVBS ||
	    ms912x->port_type == MS912X_VIDEO_PORT_SVIDEO ||
	    ms912x->port_type == MS912X_VIDEO_PORT_CVBS_SVIDEO ||
	    ms912x->port_type == MS912X_VIDEO_PORT_UNKNOWN))
		return ret + ms912x_connector_add_sd_modes(connector);
	else if (!force_edid && ms912x->port_type == MS912X_VIDEO_PORT_YPBPR)
		return ret + ms912x_connector_add_component_modes(connector);
	else
		edid = ms912x_connector_read_edid(connector, fake_edid_mode);
	if (!edid) {
		WRITE_ONCE(ms912x->fake_edid_active, false);
		return ret + ms912x_connector_add_default_modes(connector);
	}
	return ret + ms912x_connector_publish_edid_modes(connector, edid);
}

static bool ms912x_edid_probe_has_content(const u8 *header, size_t len)
{
	bool all_zero = true, all_ones = true;
	unsigned int i;

	for (i = 0; i < len; i++) {
		if (header[i] != 0)
			all_zero = false;
		if (header[i] != 0xff)
			all_ones = false;
	}

	return !all_zero && !all_ones;
}

static enum drm_connector_status ms912x_connector_detect(struct drm_connector *connector,
					       bool force)
{
	struct ms912x_device *ms912x = to_ms912x(connector->dev);
	struct ms912x_edid_read_context edid_context = {
		.ms912x = ms912x,
	};
	enum drm_connector_status status;
	enum ms912x_force_connected_mode force_mode;
	unsigned long cache_time = msecs_to_jiffies(250);
	u8 edid_header[8] = {};
	bool edid_connected = false, forced_connected = false;
	bool status_changed, log_connected = false, log_disconnected = false;
	bool log_forced_connected = false;
	int value, confirm, edid_probe;

	mutex_lock(&ms912x->detect_lock);
	if (ms912x->detect_reset_running) {
		status = ms912x->detect_status_valid ? ms912x->detect_status :
			 connector_status_unknown;
		goto unlock;
	}
	if (!force && ms912x->detect_status_valid &&
	    time_before(jiffies, ms912x->detect_status_updated + cache_time)) {
		status = ms912x->detect_status;
		goto unlock;
	}

	force_mode = READ_ONCE(ms912x_parameters.force_connected);
	if (force_mode == MS912X_FORCE_CONNECTED_ON) {
		/* Explicit force mode skips all hardware presence probes. */
		value = -EOPNOTSUPP;
		status = connector_status_connected;
		forced_connected = true;
		goto update_status;
	}

	value = ms912x_register_read_u8(ms912x, MS912X_REG_DISPLAY_STATUS);
	if (value < 0)
		ms912x_schedule_detect_reset(ms912x, value);
	if (value == 1) {
		status = connector_status_connected;
	} else if (value == 0) {
		/* A confirmed no-sink status must not be overridden by stale EDID. */
		status = connector_status_disconnected;
	} else if (force_mode == MS912X_FORCE_CONNECTED_AUTO &&
		   ms912x_port_supports_edid(ms912x->port_type)) {
		edid_probe = ms912x_edid_read_block(&edid_context, edid_header, 0,
					     sizeof(edid_header));
		/* A zero status is an unplugged sink, not an adapter failure. */
		if (value != 0 && edid_context.read_error)
			ms912x_schedule_detect_reset(ms912x,
						    edid_context.read_error);
		edid_connected = !edid_context.read_error &&
			(!edid_probe ||
			 (edid_context.malformed_header &&
			  ms912x_edid_probe_has_content(edid_header,
						       sizeof(edid_header))));
		if (edid_connected)
			status = connector_status_connected;
		else if (value < 0) {
			status = ms912x->detect_status_valid ?
				 ms912x->detect_status : connector_status_unknown;
			ms912x->detect_status_updated = jiffies;
			goto unlock;
		} else {
			status = connector_status_disconnected;
		}
	} else if (value < 0) {
		status = ms912x->detect_status_valid ? ms912x->detect_status :
			 connector_status_unknown;
		ms912x->detect_status_updated = jiffies;
		goto unlock;
	} else {
		status = connector_status_disconnected;
	}

	if (!edid_connected && ms912x->detect_status_valid &&
	    status != ms912x->detect_status) {
		/* Avoid reporting a transient status-register glitch as a hotplug. */
		usleep_range(10000, 15000);
		confirm = ms912x_register_read_u8(ms912x, MS912X_REG_DISPLAY_STATUS);
		if (confirm < 0 || (confirm == 1) !=
		    (status == connector_status_connected))
			status = ms912x->detect_status;
	}

update_status:
	if ((edid_connected || forced_connected) &&
	    (!ms912x->detect_status_valid ||
	     ms912x->detect_status != connector_status_connected))
		drm_warn(&ms912x->drm,
			 "force_connected=%s reported a sink without positive hardware status (status=%d, EDID probe=%d); modesetting or video may fail\n",
			 forced_connected ? "on" : "auto", value,
			 edid_connected ? edid_probe : -ENODATA);
	status_changed = ms912x->detect_status_valid &&
		status != ms912x->detect_status;
	if (status == connector_status_connected &&
	    (!ms912x->detect_status_valid ||
	     ms912x->detect_status != connector_status_connected)) {
		ms912x->monitor_log_pending = true;
		log_connected = !forced_connected;
		log_forced_connected = forced_connected;
	} else if (status == connector_status_disconnected) {
		log_disconnected = ms912x->detect_status_valid &&
			ms912x->detect_status == connector_status_connected;
		ms912x->monitor_log_pending = false;
	}
	ms912x->detect_status = status;
	ms912x->detect_status_valid = true;
	ms912x->detect_status_updated = jiffies;
	if (status_changed)
		ms912x_edid_cache_invalidate(ms912x);
unlock:
	mutex_unlock(&ms912x->detect_lock);
	if (log_connected)
		drm_info(&ms912x->drm, "monitor connected\n");
	if (log_forced_connected)
		drm_info(&ms912x->drm, "monitor connection forced\n");
	if (log_disconnected)
		drm_info(&ms912x->drm, "monitor disconnected\n");
	return status;
}

static const struct drm_encoder_funcs ms912x_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static const struct drm_connector_helper_funcs ms912x_connector_helper_funcs = {
	.get_modes = ms912x_connector_modes_get,
};

static const struct drm_connector_funcs ms912x_connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.detect = ms912x_connector_detect,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

int ms912x_connector_register(struct ms912x_device *ms912x)
{
	int ret;
	int connector_type;
	int encoder_type;

	connector_type = ms912x_connector_detect_type(ms912x);
	encoder_type = ms912x_encoder_type_for_port(ms912x->port_type);

	ret = drm_encoder_init(&ms912x->drm, &ms912x->encoder,
			       &ms912x_encoder_funcs, encoder_type, NULL);
	if (ret)
		return ret;
	ms912x->encoder.possible_crtcs = drm_crtc_mask(&ms912x->crtc);

	drm_connector_helper_add(&ms912x->connector,
				 &ms912x_connector_helper_funcs);
	ret = drm_connector_init(&ms912x->drm, &ms912x->connector,
				 &ms912x_connector_funcs, connector_type);
	if (ret)
		return ret;

	if (READ_ONCE(ms912x_parameters.vrr)) {
		ret = drm_connector_attach_vrr_capable_property(
			&ms912x->connector);
		if (ret)
			return ret;
		drm_connector_set_vrr_capable_property(&ms912x->connector, true);
	}

	ms912x->connector.polled =
		DRM_CONNECTOR_POLL_CONNECT | DRM_CONNECTOR_POLL_DISCONNECT;

	return drm_connector_attach_encoder(&ms912x->connector,
					    &ms912x->encoder);
}
