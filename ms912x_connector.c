// SPDX-License-Identifier: GPL-2.0-only

#include <linux/array_size.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/jiffies.h>
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

/* Synthetic EDID with a 1920x1080@60 preferred mode and 1-75 Hz limits. */
static const u8 ms912x_fake_edid_data[] = {
	0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00,
	0x36, 0x78, 0x20, 0x91, 0x01, 0x00, 0x00, 0x00,
	0x01, 0x22, 0x01, 0x04, 0x80, 0x34, 0x1d, 0x78,
	0x03, 0xae, 0x50, 0xa3, 0x54, 0x4c, 0x99, 0x26,
	0x0f, 0x50, 0x54, 0x00, 0x00, 0x00, 0x61, 0x40,
	0x45, 0x40, 0x31, 0x40, 0x81, 0x80, 0xd1, 0xc0,
	0x01, 0x01, 0x59, 0xc0, 0x01, 0x01, 0x02, 0x3a,
	0x80, 0x18, 0x71, 0x38, 0x2d, 0x40, 0x58, 0x2c,
	0x45, 0x00, 0x08, 0x22, 0x21, 0x00, 0x00, 0x1e,
	0x00, 0x00, 0x00, 0xfc, 0x00, 0x4d, 0x53, 0x39,
	0x31, 0x32, 0x78, 0x0a, 0x20, 0x20, 0x20, 0x20,
	0x20, 0x20, 0x00, 0x00, 0x00, 0xfd, 0x00, 0x01,
	0x4b, 0x1e, 0x5a, 0x14, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff,
	0x00, 0x4d, 0x53, 0x39, 0x31, 0x32, 0x58, 0x30,
	0x30, 0x30, 0x30, 0x30, 0x31, 0x0a, 0x01, 0x0f,
};

static const u8 ms912x_fake_edid_cea[EDID_LENGTH] = {
	[0] = 0x02, /* CEA extension */
	[1] = 0x03, /* CEA-861 revision 3 */
	[2] = 0x08, /* Detailed timing offset */
	[4] = 0x43, /* Video data block, 3 SVDs */
	[5] = 0x10, /* 1920x1080p60 */
	[6] = 0x1f, /* 1920x1080p50 */
	[7] = 0x22, /* 1920x1080p30 */
	/* 1024x600 59.85Hz CVT detailed timing (49.00MHz). */
	[8] = 0x24, [9] = 0x13, [10] = 0x00, [11] = 0x20,
	[12] = 0x41, [13] = 0x58, [14] = 0x18, [15] = 0x20,
	[16] = 0x28, [17] = 0x68, [18] = 0x3a, [19] = 0x00,
	[20] = 0x00, [21] = 0x00, [22] = 0x00, [23] = 0x00,
	[24] = 0x00, [25] = 0x1c,
	[EDID_LENGTH - 1] = 0x51,
};

static bool ms912x_timing_has_1920x1200_60(
						struct ms912x_device *ms912x)
{
	unsigned int i;

	for (i = 0; i < ms912x->num_custom_modes; i++) {
		const struct ms912x_mode *mode = &ms912x->custom_modes[i].mode;

		if (mode->width == 1920 && mode->height == 1200 && mode->hz == 60)
			return true;
	}

	return false;
}

static const struct drm_edid *
ms912x_edid_create_synthetic(struct ms912x_device *ms912x)
{
	u8 fake_edid[2 * EDID_LENGTH];
	u8 checksum = 0;
	const struct drm_edid *edid;
	unsigned int i;

	memcpy(fake_edid, ms912x_fake_edid_data, EDID_LENGTH);
	memcpy(fake_edid + EDID_LENGTH, ms912x_fake_edid_cea,
	       EDID_LENGTH);
	if (ms912x_timing_has_1920x1200_60(ms912x)) {
		/* EDID standard timing: 1920x1200 at 60 Hz (16:10). */
		fake_edid[48] = 0xd1;
		fake_edid[49] = 0x00;
	}

	fake_edid[127] = 0;
	for (i = 0; i < EDID_LENGTH - 1; i++)
		checksum += fake_edid[i];
	fake_edid[127] = -checksum;

	edid = drm_edid_alloc(fake_edid, sizeof(fake_edid));
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
		ret = -EINVAL;
		malformed_header = true;
		goto unlock_edid;
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
	struct ms912x_edid_read_context edid_context = {
		.ms912x = ms912x,
	};
	const struct drm_edid *edid;
	enum ms912x_fake_edid_mode fake_edid_mode =
		READ_ONCE(ms912x_parameters.fake_edid);
	int ret = ms912x_connector_add_custom_timing(connector);

	if (fake_edid_mode == MS912X_FAKE_EDID_ON &&
	    (ms912x->port_type == MS912X_VIDEO_PORT_VGA ||
	     ms912x->port_type == MS912X_VIDEO_PORT_HDMI ||
	     ms912x->port_type == MS912X_VIDEO_PORT_DIGITAL ||
	     ms912x->port_type == MS912X_VIDEO_PORT_UNKNOWN)) {
		edid = ms912x_edid_create_synthetic(ms912x);
	} else if (ms912x->port_type == MS912X_VIDEO_PORT_CVBS ||
	    ms912x->port_type == MS912X_VIDEO_PORT_SVIDEO ||
	    ms912x->port_type == MS912X_VIDEO_PORT_CVBS_SVIDEO ||
	    ms912x->port_type == MS912X_VIDEO_PORT_UNKNOWN)
		return ret + ms912x_connector_add_sd_modes(connector);
	else if (ms912x->port_type == MS912X_VIDEO_PORT_YPBPR)
		return ret + ms912x_connector_add_component_modes(connector);
	else {
		edid = drm_edid_read_custom(connector, ms912x_edid_read_block,
					    &edid_context);
		if (edid && drm_edid_valid(edid)) {
			WRITE_ONCE(ms912x->fake_edid_active, false);
		} else {
			bool malformed = edid_context.malformed_header ||
				(edid && !drm_edid_valid(edid));

			if (edid) {
				drm_edid_free(edid);
				edid = NULL;
			}
			if (!edid_context.read_error && malformed &&
			    fake_edid_mode == MS912X_FAKE_EDID_AUTO &&
			    READ_ONCE(ms912x->detect_status_valid) &&
			    READ_ONCE(ms912x->detect_status) ==
							connector_status_connected &&
			    (ms912x->port_type == MS912X_VIDEO_PORT_VGA ||
			     ms912x->port_type == MS912X_VIDEO_PORT_HDMI ||
			     ms912x->port_type == MS912X_VIDEO_PORT_DIGITAL ||
			     ms912x->port_type == MS912X_VIDEO_PORT_UNKNOWN))
				edid = ms912x_edid_create_synthetic(ms912x);
		}
	}
	if (!edid) {
		WRITE_ONCE(ms912x->fake_edid_active, false);
		return ret + ms912x_connector_add_default_modes(connector);
	}

	if (drm_edid_connector_update(connector, edid) >= 0)
		ret += drm_edid_connector_add_modes(connector);
	drm_edid_free(edid);
	return ret;
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

static bool ms912x_port_supports_edid(enum ms912x_video_port port_type)
{
	return port_type == MS912X_VIDEO_PORT_VGA ||
		port_type == MS912X_VIDEO_PORT_HDMI ||
		port_type == MS912X_VIDEO_PORT_DIGITAL ||
		port_type == MS912X_VIDEO_PORT_UNKNOWN;
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
	bool status_changed;
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
	WRITE_ONCE(ms912x->detect_status_confirm, -ENODATA);
	WRITE_ONCE(ms912x->detect_edid_probe, -ENODATA);
	if (force_mode == MS912X_FORCE_CONNECTED_ON) {
		/* Explicit force mode skips all hardware presence probes. */
		WRITE_ONCE(ms912x->detect_status_raw, -EOPNOTSUPP);
		status = connector_status_connected;
		forced_connected = true;
		goto update_status;
	}

	value = ms912x_register_read_u8(ms912x, MS912X_REG_DISPLAY_STATUS);
	WRITE_ONCE(ms912x->detect_status_raw, value);
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
		WRITE_ONCE(ms912x->detect_edid_probe, edid_probe);
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
		WRITE_ONCE(ms912x->detect_status_confirm, confirm);
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
	ms912x->detect_status = status;
	ms912x->detect_status_valid = true;
	ms912x->detect_status_updated = jiffies;
	if (status_changed)
		ms912x_edid_cache_invalidate(ms912x);
unlock:
	mutex_unlock(&ms912x->detect_lock);
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
