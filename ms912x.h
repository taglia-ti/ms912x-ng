/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MS912X_H
#define MS912X_H

#include <linux/atomic.h>
#include <linux/bits.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/scatterlist.h>
#include <linux/timer_types.h>
#include <linux/types.h>
#include <linux/usb.h>
#include <linux/wait.h>
#include <linux/workqueue.h>

#include <drm/drm_connector.h>
#include <drm/drm_crtc.h>
#include <drm/drm_device.h>
#include <drm/drm_encoder.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_format_helper.h>
#include <drm/drm_gem.h>
#include <drm/drm_plane.h>
#include <drm/drm_rect.h>

#include "ms912x_protocol.h"
#include "ms912x_stats.h"

#define MS912X_DRIVER_NAME "ms912x"
#define MS912X_DRIVER_DESC "MacroSilicon USB to VGA/HDMI"

#define MS912X_DRIVER_MAJOR 1
#define MS912X_DRIVER_MINOR 0

#define MS912X_BULK_OUT_ENDPOINT	4
#define MS912X_USB_INITIAL_RATE_BPS	(30ULL * 1000 * 1000)
#define MS912X_IDLE_REFRESH_MS		500
#define MS912X_IDLE_REPLAY_COLUMNS	4
#define MS912X_IDLE_REPLAY_TILES	8
#define MS912X_MAX_DAMAGE_RECTS		64
#define MS912X_SCAN_FRAME_SLOTS		2
#define MS912X_FULL_UPDATE_THRESHOLD_PERCENT 75
#define MS912X_DEFAULT_STALE_AREA_PERCENT	50
#define MS912X_USB_COALESCE_MIN_BYTES	(256U * 1024U)

enum ms912x_video_port {
	MS912X_VIDEO_PORT_CVBS = 0,
	MS912X_VIDEO_PORT_SVIDEO = 1,
	MS912X_VIDEO_PORT_VGA = 2,
	MS912X_VIDEO_PORT_YPBPR = 3,
	MS912X_VIDEO_PORT_CVBS_SVIDEO = 4,
	MS912X_VIDEO_PORT_HDMI = 5,
	MS912X_VIDEO_PORT_DIGITAL = 6,
	MS912X_VIDEO_PORT_UNKNOWN = 0xff,
};

enum ms912x_request_state {
	MS912X_REQUEST_FREE,
	MS912X_REQUEST_CONVERTING,
	MS912X_REQUEST_USB_QUEUED,
	MS912X_REQUEST_USB_ACTIVE,
};

struct ms912x_usb_request {
	void *transfer_buffer;
	struct ms912x_device *ms912x;
	struct drm_rect rect;
	struct drm_rect base_rect;
	size_t transfer_len;
	struct sg_table transfer_sgt;
	struct usb_sg_request sgr;
	struct work_struct work;
	struct timer_list timer;
	u64 queued_at_ns;
	u64 usb_started_ns;
	u64 source_sequence;
	u64 source_generation;
	u32 generation;
	u32 idle_replay_generation;
	unsigned int idle_replay_tile;
	bool idle_replay;
	enum ms912x_request_state state;
	unsigned int width;
	unsigned int height;
};

struct ms912x_pending_rect {
	struct drm_rect rect;
	u32 idle_replay_generation;
	unsigned int idle_replay_tile;
	bool idle_replay;
};

struct ms912x_scan_frame {
	u32 *pixels;
	u64 sequence;
	u32 generation;
	unsigned int width;
	unsigned int height;
	bool force_full;
	bool queued;
	bool running;
};

enum ms912x_damage_scan_mode {
	MS912X_DAMAGE_SCAN_AUTO,
	MS912X_DAMAGE_SCAN_OFF,
	MS912X_DAMAGE_SCAN_ON,
};

enum ms912x_fake_edid_mode {
	MS912X_FAKE_EDID_AUTO,
	MS912X_FAKE_EDID_OFF,
	MS912X_FAKE_EDID_ON,
};

enum ms912x_force_connected_mode {
	MS912X_FORCE_CONNECTED_AUTO,
	MS912X_FORCE_CONNECTED_OFF,
	MS912X_FORCE_CONNECTED_ON,
};

enum ms912x_damage_scan_method {
	MS912X_DAMAGE_SCAN_SCALAR,
	MS912X_DAMAGE_SCAN_MEMCMP,
};

struct ms912x_parameters {
	enum ms912x_damage_scan_mode damage_scan;
	enum ms912x_damage_scan_method damage_scan_method;
	unsigned int damage_scan_stride;
	unsigned int fast_chroma;
	enum ms912x_fake_edid_mode fake_edid;
	enum ms912x_force_connected_mode force_connected;
	bool debugfs_enabled;
	bool vrr;
	atomic_t damage_scan_generation;
	atomic_t damage_scan_config_generation;
};

extern struct ms912x_parameters ms912x_parameters;

struct ms912x_mode {
	int width;
	int height;
	int hz;
	int mode;
};

struct ms912x_custom_mode {
	struct ms912x_mode mode;
	struct drm_display_mode display_mode;
};

#define MS912X_MODE(w, h, z, m)                                               \
	{                                                                      \
		.width = w, .height = h, .hz = z, .mode = m                    \
	}

struct ms912x_device {
	struct drm_device drm;
	struct usb_interface *intf;
	unsigned int bulk_pipe;
	enum ms912x_video_port port_type;
	struct workqueue_struct *workqueue;
	struct workqueue_struct *conversion_workqueue;
	struct work_struct scan_work;
	struct work_struct detect_reset_work;
	struct ms912x_scan_frame scan_frames[MS912X_SCAN_FRAME_SLOTS];
	struct mutex scan_queue_lock;
	u64 frame_sequence;
	bool scan_work_scheduled;
	struct delayed_work conversion_work;
	struct delayed_work idle_work;
	/* Serializes HID control request/response sequences */
	struct mutex ctrl_lock;
	struct mutex damage_scan_lock;
	struct mutex transfer_lock;
	wait_queue_head_t conversion_wait;
	/* Protected by transfer_lock; sample count includes the initial estimate. */
	u64 usb_speed_mean_bps;
	u64 usb_speed_samples;
	u64 usb_successful_transfers;
	u64 usb_first_success_ns;
	u64 usb_last_success_ns;

	struct drm_connector connector;
	struct drm_encoder encoder;
	struct drm_crtc crtc;
	struct drm_plane plane;
	struct list_head device_list;
	struct mutex detect_lock;
	struct mutex edid_lock;
	enum drm_connector_status detect_status;
	unsigned long detect_status_updated;
	int detect_status_raw;
	int detect_status_confirm;
	int detect_edid_probe;
	u8 edid_cache[MS912X_EDID_CACHE_BLOCKS]
		   [MS912X_EDID_CACHE_BLOCK_SIZE];
	bool edid_cache_valid[MS912X_EDID_CACHE_BLOCKS];
	bool edid_block_read_unsupported;
	bool detect_status_valid;
	bool fake_edid_active;
	bool detect_reset_stopping;
	bool detect_reset_running;
	atomic_t detect_reset_pending;
	unsigned long detect_reset_last;

	struct ms912x_custom_mode custom_modes[2];
	unsigned int num_custom_modes;

	unsigned long last_frame_activity;
	unsigned int frame_snapshot_width;
	unsigned int frame_snapshot_height;
	struct drm_rect last_queued_rect;
	struct drm_rect last_wire_rect;
	struct drm_rect failed_rect;
	unsigned int last_queued_width;
	unsigned int last_queued_height;
	u32 last_queued_generation;
	unsigned int failed_width;
	unsigned int failed_height;
	u32 failed_generation;

	struct ms912x_usb_request requests[2];
	struct ms912x_stats stats;
	u32 *damage_scan_snapshot;
	u32 *damage_scan_snapshot_back;
	u32 *latest_xrgb;
	u32 *conversion_xrgb;
	struct ms912x_pending_rect pending_rects[MS912X_MAX_DAMAGE_RECTS];
	unsigned int pending_rect_count;
	unsigned long pending_deadline;
	unsigned int pending_width;
	unsigned int pending_height;
	/* Changes only when a modeset/frame-size change invalidates scan work. */
	u32 frame_generation;
	u64 published_frame_sequence;
	u64 scratch_generation;
	u32 idle_replay_generation;
	unsigned int idle_replay_tile;
	unsigned int pending_idle_replay_tile;
	unsigned int damage_scan_width;
	unsigned int damage_scan_height;
	bool damage_scan_x_probe_done;
	int damage_scan_mode_generation;
	int damage_scan_config_generation;
	u32 damage_scan_probe_generation;
	int damage_scan_probe_config_generation;
	unsigned int display_refresh_hz;
	bool damage_scan_x_stable;
	bool damage_scan_valid;
	bool transfer_stopping;
	bool scan_stopping;
	bool pending_valid;
	bool pending_force_full;
	bool pending_idle_replay;
	bool frame_snapshot_valid;
	bool screen_muted;
	bool last_queued_rect_valid;
	bool last_wire_rect_valid;
	bool failed_rect_valid;
	bool conversion_busy;
	atomic_t damage_scan_invalid;
};

#define MS912X_MAX_WIDTH 1920
#define MS912X_MAX_HEIGHT 1200
#define MS912X_MAX_TRANSFER_LEN \
	(MS912X_MAX_WIDTH * MS912X_MAX_HEIGHT * 2 + MS912X_FRAME_OVERHEAD)
#define to_ms912x(x) container_of(x, struct ms912x_device, drm)

void ms912x_transfer_work_init(struct ms912x_device *ms912x);
void ms912x_schedule_detect_reset(struct ms912x_device *ms912x, int error);
void ms912x_transfer_start(struct ms912x_device *ms912x);
int ms912x_usb_request_refresh(struct ms912x_usb_request *request);
void ms912x_idle_work(struct work_struct *work);
#ifdef CONFIG_X86_64
bool ms912x_convert_xrgb8888_to_uyvy_sse2(void *dst, const __le32 *src,
					    const struct drm_rect *rect);
void ms912x_convert_xrgb8888_to_uyvy_avx2(void *dst, const __le32 *src,
					   const struct drm_rect *rect);
#else
static inline bool
ms912x_convert_xrgb8888_to_uyvy_sse2(void *dst, const __le32 *src,
			       const struct drm_rect *rect)
{
	(void)dst;
	(void)src;
	(void)rect;
	return false;
}
static inline void
ms912x_convert_xrgb8888_to_uyvy_avx2(void *dst, const __le32 *src,
			      const struct drm_rect *rect)
{
	(void)dst;
	(void)src;
	(void)rect;
}
#endif
#ifdef CONFIG_ARM64
bool ms912x_convert_xrgb8888_to_uyvy_neon(void *dst, const __le32 *src,
					    const struct drm_rect *rect);
#else
static inline bool
ms912x_convert_xrgb8888_to_uyvy_neon(void *dst, const __le32 *src,
			       const struct drm_rect *rect)
{
	(void)dst;
	(void)src;
	(void)rect;
	return false;
}
#endif
const char *ms912x_param_damage_scan_mode_name(
					 enum ms912x_damage_scan_mode mode);
void ms912x_device_registry_add(struct ms912x_device *ms912x);
void ms912x_device_registry_remove(struct ms912x_device *ms912x);
void ms912x_debugfs_register(struct drm_device *drm);
void ms912x_kms_mode_config_init(struct drm_device *drm);
const struct ms912x_mode *ms912x_default_mode(void);
int ms912x_kms_init(struct ms912x_device *ms912x);
int ms912x_convert_xrgb8888_to_uyvy(void *dst, const __le32 *src,
				    struct ms912x_device *ms912x,
				    unsigned int fb_width,
				    unsigned int fb_height,
				    const struct drm_rect *rect);
void ms912x_damage_scan_invalidate(struct ms912x_device *ms912x);
int ms912x_damage_scan_find_snapshot_changes(
				   struct ms912x_device *ms912x,
				   const u32 *frame,
				   u64 frame_sequence,
				   u32 frame_generation,
				   const u32 *baseline,
				   u32 *next_snapshot,
				   unsigned int width,
				   unsigned int height,
				   bool force_full,
				   struct drm_rect *rects,
				   unsigned int *rect_count);
void ms912x_damage_scan_update_snapshot(struct ms912x_device *ms912x,
				  struct drm_framebuffer *fb,
				  const struct iosys_map *map,
				  const struct drm_rect *rect);
void ms912x_transfer_stop(struct ms912x_device *ms912x);
void ms912x_transfer_free_requests(struct ms912x_device *ms912x);
/* Caller must hold transfer_lock. */
void ms912x_schedule_conversion_locked(struct ms912x_device *ms912x);


int ms912x_register_read_u8(struct ms912x_device *ms912x, u16 address);
int ms912x_register_read_block(struct ms912x_device *ms912x, u16 address,
			       void *data, size_t len);
int ms912x_timing_load_custom(struct ms912x_device *ms912x);
int ms912x_connector_register(struct ms912x_device *ms912x);
int ms912x_display_set_mode(struct ms912x_device *ms912x,
			  const struct ms912x_mode *mode);

int ms912x_display_enable(struct ms912x_device *ms912x);
int ms912x_display_disable(struct ms912x_device *ms912x);
int ms912x_screen_mute(struct ms912x_device *ms912x);
int ms912x_screen_unmute(struct ms912x_device *ms912x);

int ms912x_framebuffer_submit(struct drm_framebuffer *fb, const struct iosys_map *map,
			struct drm_rect *rects, unsigned int *rect_count,
			bool software_scan, bool force_full_scan,
			bool new_epoch);

void ms912x_usb_request_release(struct ms912x_usb_request *request);
int ms912x_usb_request_init(struct ms912x_device *ms912x,
			struct ms912x_usb_request *request, size_t len);
#endif
