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
#include "ms912x_capabilities.h"
#include "ms912x_profile_policy.h"
#include "ms912x_downscale.h"
#include "ms912x_damage_policy.h"
#include "ms912x_scale.h"
#include "ms912x_cursor.h"
#include "ms912x_stats.h"
#include "ms912x_neon.h"

#define MS912X_DRIVER_NAME "ms912x"
#define MS912X_DRIVER_DESC "MacroSilicon USB to VGA/HDMI"

#define MS912X_DRIVER_MAJOR 1
#define MS912X_DRIVER_MINOR 0

#define MS912X_BULK_OUT_ENDPOINT	4
#define MS912X_USB_INITIAL_RATE_BYTES_PER_SEC	(30ULL * 1000 * 1000)
#define MS912X_INITIAL_DAMAGE_RECTS	512
#define MS912X_DAMAGE_RECTS_GROWTH	256
#define MS912X_MAX_DAMAGE_RECTS		2048
#define MS912X_MAX_BATCH_RECTS \
	(MS912X_MAX_DAMAGE_RECTS * 2 + 1)
#define MS912X_SCAN_FRAME_SLOTS		2
#define MS912X_SMALL_DAMAGE_THRESHOLD_BYTES	(256U * 1024U)
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
	/* Geometry is private until source staging begins. */
	MS912X_REQUEST_PREPARING,
	MS912X_REQUEST_CONVERTING,
	MS912X_REQUEST_USB_QUEUED,
	MS912X_REQUEST_USB_ACTIVE,
};

struct ms912x_scan_rect_index;

struct ms912x_usb_request {
	void *transfer_buffer;
	struct ms912x_device *ms912x;
	struct drm_rect *rects;
	struct drm_rect *current_rects;
	unsigned int rect_capacity;
	unsigned int current_rect_capacity;
	size_t transfer_len;
	struct sg_table transfer_sgt;
	struct usb_sg_request sgr;
	struct work_struct work;
	struct timer_list timer;
	/* Source revision captured in this payload and latest covered revision. */
	u64 source_generation;
	u64 content_generation;
	u32 generation;
	enum ms912x_request_state state;
	unsigned int width;
	unsigned int height;
	unsigned int rect_count;
	unsigned int current_rect_count;
	bool replay_included;
	u32 cursor_pixels[MS912X_CURSOR_MAX_WIDTH * MS912X_CURSOR_MAX_HEIGHT];
	int cursor_x;
	int cursor_y;
	unsigned int cursor_alpha_x;
	unsigned int cursor_alpha_y;
	unsigned int cursor_source_height;
	unsigned int cursor_width;
	unsigned int cursor_height;
	bool cursor_enabled;
	bool cursor_premultiplied;
	bool cursor_included;
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
	bool filling;
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
	MS912X_DAMAGE_SCAN_AVX2,
	MS912X_DAMAGE_SCAN_SSE2,
	MS912X_DAMAGE_SCAN_NEON,
	MS912X_DAMAGE_SCAN_METHOD_AUTO,
	MS912X_DAMAGE_SCAN_SSE41,
};

enum ms912x_fast_chroma_backend {
	MS912X_FAST_CHROMA_SCALAR,
	MS912X_FAST_CHROMA_AUTO,
	MS912X_FAST_CHROMA_AVX2,
	MS912X_FAST_CHROMA_NEON,
	MS912X_FAST_CHROMA_SSE2,
	MS912X_FAST_CHROMA_SSSE3,
};

struct ms912x_parameters {
	enum ms912x_profile profile;
	enum ms912x_profile effective_profile;
	enum ms912x_damage_scan_mode damage_scan;
	unsigned int scale;
	enum ms912x_damage_scan_method damage_scan_method;
	unsigned int damage_scan_stride;
	unsigned int damage_scan_tolerance;
	unsigned int damage_scan_tolerance_max;
	bool damage_scan_override;
	bool damage_scan_stride_override;
	bool damage_scan_tolerance_override;
	enum ms912x_fast_chroma_backend fast_chroma;
	bool cursor_plane;
	enum ms912x_fake_edid_mode fake_edid;
	enum ms912x_force_connected_mode force_connected;
	bool debugfs_enabled;
	bool dbg_show_draws;
	bool vrr;
	atomic_t damage_scan_generation;
	atomic_t damage_scan_config_generation;
	atomic_t scale_generation;
};

extern struct ms912x_parameters ms912x_parameters;
void ms912x_scale_snapshot(unsigned int *scale, int *generation);
const char *ms912x_param_damage_scan_method_name(
	enum ms912x_damage_scan_method method);
const char *ms912x_param_damage_scan_mode_name(
	enum ms912x_damage_scan_mode mode);
const char *ms912x_profile_name(enum ms912x_profile profile);
void ms912x_profile_configure_device(struct ms912x_device *ms912x,
				     enum usb_device_speed speed);
const char *ms912x_damage_scan_selected_backend_name(
	struct ms912x_device *ms912x);
const char *ms912x_fast_chroma_selected_backend_name(void);

struct ms912x_mode {
	int width;
	int height;
	int hz;
	u8 vic;
	u32 clock_khz;
	u16 htotal;
	u16 vtotal;
	u16 hoffset;
	u16 voffset;
	u16 hsync_width;
	u16 vsync_width;
	u8 polarity;
};

struct ms912x_custom_mode {
	struct ms912x_mode mode;
	struct drm_display_mode display_mode;
};

#define MS912X_MODE_TIMED(w, h, z, v, clk, ht, vt, ho, vo, hsw, vsw, pol) \
	{                                                                      \
		.width = w, .height = h, .hz = z, .vic = v,                  \
		.clock_khz = clk, .htotal = ht, .vtotal = vt,                 \
		.hoffset = ho, .voffset = vo, .hsync_width = hsw,             \
		.vsync_width = vsw, .polarity = pol                           \
	}

struct ms912x_device {
	struct drm_device drm;
	struct usb_interface *intf;
	unsigned int bulk_pipe;
	enum ms912x_video_port port_type;
	enum ms912x_sdram_type sdram_type;
	struct workqueue_struct *workqueue;
	struct workqueue_struct *conversion_workqueue;
	struct work_struct scan_work;
	struct work_struct detect_reset_work;
	struct ms912x_scan_frame scan_frames[MS912X_SCAN_FRAME_SLOTS];
	struct mutex scan_queue_lock;
	wait_queue_head_t scan_frame_wait;
	u32 scan_epoch;
	u64 frame_sequence;
	bool scan_work_scheduled;
	struct delayed_work conversion_work;
	/* Serializes HID control request/response sequences */
	struct mutex ctrl_lock;
	struct mutex sdram_lock;
	struct mutex damage_scan_lock;
	struct mutex transfer_lock;
	/* Protect latest_xrgb writes/swaps during staging; acquire before transfer_lock. */
	struct mutex snapshot_lock;
	/* Serializes framebuffer producers across CPU-access callbacks. */
	struct mutex submit_lock;
	wait_queue_head_t conversion_wait;
	wait_queue_head_t frame_present_wait;
	/* Protected by transfer_lock; sample count includes the initial estimate. */
	u64 usb_speed_mean_bytes_per_sec;
	u64 usb_speed_samples;
	u64 usb_successful_transfers;

	struct drm_connector connector;
	struct drm_encoder encoder;
	struct drm_crtc crtc;
	struct drm_plane plane;
	struct drm_plane cursor_plane;
	bool cursor_plane_enabled;
	u32 *cursor_source_pixels;
	u32 *cursor_staging_pixels;
	u32 cursor_pixels[MS912X_CURSOR_MAX_WIDTH * MS912X_CURSOR_MAX_HEIGHT];
	struct mutex cursor_prepare_lock;
	u64 cursor_commit_sequence;
	unsigned int cursor_width;
	unsigned int cursor_height;
	int cursor_x;
	int cursor_y;
	unsigned int cursor_alpha_x;
	unsigned int cursor_alpha_y;
	unsigned int cursor_plane_width;
	unsigned int cursor_plane_height;
	enum ms912x_scale_mode cursor_scale_mode;
	unsigned int cursor_source_height;
	int cursor_plane_x;
	int cursor_plane_y;
	u32 cursor_fb_id;
	bool cursor_enabled;
	bool cursor_plane_active;
	bool cursor_premultiplied;
	bool cursor_commit_pending;
	struct list_head device_list;
	struct mutex detect_lock;
	struct mutex edid_lock;
	enum drm_connector_status detect_status;
	unsigned long detect_status_updated;
	u8 edid_cache[MS912X_EDID_CACHE_BLOCKS]
		   [MS912X_EDID_CACHE_BLOCK_SIZE];
	bool edid_cache_valid[MS912X_EDID_CACHE_BLOCKS];
	bool edid_block_read_unsupported;
	bool detect_status_valid;
	bool monitor_log_pending;
	bool fake_edid_active;
	bool detect_reset_stopping;
	bool detect_reset_running;
	atomic_t detect_reset_pending;
	unsigned long detect_reset_last;

	struct ms912x_custom_mode custom_modes[2];
	unsigned int num_custom_modes;
	unsigned int damage_scan_stride_default;
	struct ms912x_scan_rect_index *scan_rect_index;
	enum ms912x_damage_scan_method damage_scan_backend;

	unsigned int frame_snapshot_width;
	unsigned int frame_snapshot_height;
	unsigned int framebuffer_width;
	unsigned int framebuffer_height;
	unsigned int video_in_width;
	unsigned int video_in_height;
	int scale_config_generation;
	enum ms912x_scale_mode scale_mode;
	struct drm_rect failed_rect;
	unsigned int failed_width;
	unsigned int failed_height;
	u32 failed_generation;

	struct ms912x_usb_request requests[2];
	struct ms912x_stats stats;
	u32 *damage_scan_snapshot;
	u32 *damage_scan_snapshot_back;
	u32 *latest_xrgb;
	u32 *conversion_xrgb;
	/* Reusable coverage snapshot, owned by snapshot_lock. */
	struct drm_rect *payload_compare_rects;
	struct drm_rect *scan_damage_rects;
	unsigned int scan_damage_rect_capacity;
	struct drm_rect *submit_damage_rects;
	unsigned int submit_damage_rect_capacity;
	struct drm_rect *pending_rects;
	unsigned int pending_rect_capacity;
	unsigned int pending_rect_count;
	struct drm_rect *previous_damage_rects;
	unsigned int previous_damage_rect_capacity;
	unsigned int previous_damage_rect_count;
	unsigned int previous_damage_width;
	unsigned int previous_damage_height;
	u32 previous_damage_generation;
	unsigned long pending_deadline;
	unsigned int pending_width;
	unsigned int pending_height;
	/* Changes only when a modeset/frame-size change invalidates scan work. */
	u32 frame_generation;
	u64 published_frame_sequence;
	u64 drm_released_sequence;
	u64 small_damage_sequence;
	u64 failed_present_sequence;
	u64 present_queue_pressure_ns;
	u64 present_queue_low_watermark_ns;
	u64 present_queue_high_watermark_ns;
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
	bool failed_rect_valid;
	bool present_backpressured;
	bool conversion_busy;
	atomic_t damage_scan_invalid;
};

#define MS912X_MAX_WIDTH 1920
#define MS912X_MAX_HEIGHT 1200
#define MS912X_MAX_TRANSFER_LEN \
	(MS912X_MAX_WIDTH * MS912X_MAX_HEIGHT * 2 + \
	 MS912X_FRAME_OVERHEAD)
#define to_ms912x(x) container_of(x, struct ms912x_device, drm)

void ms912x_transfer_work_init(struct ms912x_device *ms912x);
void ms912x_schedule_detect_reset(struct ms912x_device *ms912x, int error);
void ms912x_transfer_start(struct ms912x_device *ms912x);
void ms912x_transfer_force_full_update(struct ms912x_device *ms912x);
int ms912x_usb_request_refresh(struct ms912x_usb_request *request);
struct drm_rect *ms912x_rect_vector_grow(struct drm_rect *rects,
					 unsigned int *capacity,
					 unsigned int required,
					 unsigned int limit);
#ifdef CONFIG_X86_64
bool ms912x_convert_xrgb8888_to_uyvy_sse2(void *dst, const __le32 *src,
					    const struct drm_rect *rect);
bool ms912x_convert_xrgb8888_to_uyvy_ssse3(void *dst, const __le32 *src,
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
#if MS912X_HAS_NEON
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
unsigned int ms912x_damage_scan_effective_tolerance(
					 struct ms912x_device *ms912x);
void ms912x_device_registry_add(struct ms912x_device *ms912x);
void ms912x_device_registry_remove(struct ms912x_device *ms912x);
void ms912x_debugfs_register(struct drm_device *drm);
void ms912x_kms_mode_config_init(struct drm_device *drm);
const struct ms912x_mode *ms912x_default_mode(void);
int ms912x_kms_init(struct ms912x_device *ms912x);
int ms912x_convert_xrgb8888_to_uyvy_batch(
				    void *dst, const __le32 *src,
				    struct ms912x_device *ms912x,
				    unsigned int fb_width,
				    unsigned int fb_height,
				    const struct drm_rect *rects,
				    unsigned int rect_count);
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
				   struct drm_rect **rects,
				   unsigned int *rect_capacity,
				   unsigned int *rect_count);
void ms912x_damage_scan_update_snapshot(struct ms912x_device *ms912x,
				  const struct drm_rect *rect);
void ms912x_transfer_stop(struct ms912x_device *ms912x);
void ms912x_scan_wait_captures(struct ms912x_device *ms912x);
void ms912x_transfer_free_requests(struct ms912x_device *ms912x);
/* Caller must hold transfer_lock. */
void ms912x_schedule_conversion_locked(struct ms912x_device *ms912x);
/* Caller holds transfer_lock; updates the USB-pressure page-flip gate. */
void ms912x_transfer_update_pacing_locked(struct ms912x_device *ms912x);


int ms912x_register_read_u8(struct ms912x_device *ms912x, u16 address);
int ms912x_register_read_block(struct ms912x_device *ms912x, u16 address,
			       void *data, size_t len);
int ms912x_timing_load_custom(struct ms912x_device *ms912x);
int ms912x_connector_register(struct ms912x_device *ms912x);
int ms912x_display_set_mode(struct ms912x_device *ms912x,
			  const struct ms912x_mode *mode);
int ms912x_sdram_detect(struct ms912x_device *ms912x);
unsigned int ms912x_supported_mode_count(struct ms912x_device *ms912x);
const struct ms912x_mode *
ms912x_supported_mode_at(struct ms912x_device *ms912x, unsigned int index);
int ms912x_mode_to_display_mode(struct ms912x_device *ms912x,
				const struct ms912x_mode *mode,
				struct drm_display_mode *display_mode);

int ms912x_display_enable(struct ms912x_device *ms912x);
int ms912x_display_disable(struct ms912x_device *ms912x);

int ms912x_framebuffer_submit(struct drm_framebuffer *fb, const struct iosys_map *map,
			const struct drm_rect *damage,
			const struct drm_mode_rect *clips,
			unsigned int clip_count,
			bool software_scan, bool force_full_scan,
			bool new_epoch, bool cursor_commit);
bool ms912x_cursor_commit_begin(struct ms912x_device *ms912x,
				const struct drm_plane_state *new_state);
void ms912x_cursor_plane_submit(struct ms912x_device *ms912x,
				struct drm_plane_state *new_state);
void ms912x_cursor_reconfigure(struct ms912x_device *ms912x,
			       enum ms912x_scale_mode mode);
void ms912x_transfer_requeue_rect_locked(struct ms912x_device *ms912x,
					 const struct drm_rect *rect,
					 unsigned int width,
					 unsigned int height);
u64 ms912x_cursor_composite_uyvy(void *transfer_buffer,
				 const struct drm_rect *rect,
				 const u32 *cursor_pixels, int cursor_x, int cursor_y,
				 unsigned int cursor_width,
				 unsigned int cursor_height,
				 unsigned int cursor_alpha_x,
				 unsigned int cursor_alpha_y,
				 bool premultiplied);
void ms912x_debug_draw_uyvy_border(void *transfer_buffer,
				   const struct drm_rect *transfer_rect,
				   const struct drm_rect *new_damage_rect);

void ms912x_usb_request_release(struct ms912x_usb_request *request);
int ms912x_usb_request_init(struct ms912x_device *ms912x,
			struct ms912x_usb_request *request, size_t len);
#endif
