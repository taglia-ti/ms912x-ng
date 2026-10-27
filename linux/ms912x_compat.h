/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MS912X_COMPAT_H
#define MS912X_COMPAT_H

#include <linux/version.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
#include <linux/dma-buf.h>
#include <linux/kernel.h>

#include <drm/drm_device.h>
#include <drm/drm_connector.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_debugfs.h>
#include <drm/drm_edid.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_file.h>
#include <drm/drm_plane.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_gem_shmem_helper.h>

#ifndef DRM_PLANE_NO_SCALING
#define DRM_PLANE_NO_SCALING DRM_PLANE_HELPER_NO_SCALING
#endif

#ifndef DRM_FORMAT_MAX_PLANES
#define DRM_FORMAT_MAX_PLANES 4
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0)
static inline void
ms912x_timer_shutdown_sync_timer_shutdown_sync(struct timer_list *timer)
{
	timer_shutdown_sync(timer);
}
#define ms912x_timer_shutdown_sync \
	ms912x_timer_shutdown_sync_timer_shutdown_sync
#else
static inline void ms912x_timer_shutdown_sync_del_timer_sync(
	struct timer_list *timer)
{
	del_timer_sync(timer);
}
#define ms912x_timer_shutdown_sync ms912x_timer_shutdown_sync_del_timer_sync
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 12, 0)
static inline int ms912x_mutex_init_devm_mutex_init(struct device *dev,
						   struct mutex *mutex)
{
	return devm_mutex_init(dev, mutex);
}
#define ms912x_mutex_init ms912x_mutex_init_devm_mutex_init
#else
static inline int ms912x_mutex_init_mutex_init(struct device *dev,
					      struct mutex *mutex)
{
	(void)dev;
	mutex_init(mutex);
	return 0;
}
#define ms912x_mutex_init ms912x_mutex_init_mutex_init
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 11, 0)
#include <drm/drm_gem_atomic_helper.h>
#define ms912x_shadow_plane_state drm_shadow_plane_state
#define ms912x_to_shadow_plane_state to_drm_shadow_plane_state
#define MS912X_GEM_SHADOW_PLANE_FUNCS DRM_GEM_SHADOW_PLANE_FUNCS
#define MS912X_GEM_SHADOW_PLANE_HELPER_FUNCS \
	DRM_GEM_SHADOW_PLANE_HELPER_FUNCS
#else
struct ms912x_shadow_plane_state {
	struct drm_plane_state base;
	struct iosys_map map[DRM_FORMAT_MAX_PLANES];
	struct iosys_map data[DRM_FORMAT_MAX_PLANES];
	struct drm_plane_state *old_state;
	bool ignore_damage_clips;
};

#define ms912x_to_shadow_plane_state(state) \
	container_of(state, struct ms912x_shadow_plane_state, base)
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 11, 0)
static inline int
ms912x_drm_gem_fb_begin_cpu_access_drm_gem_fb_begin_cpu_access(
	struct drm_framebuffer *fb, enum dma_data_direction dir)
{
	return drm_gem_fb_begin_cpu_access(fb, dir);
}

static inline void
ms912x_drm_gem_fb_end_cpu_access_drm_gem_fb_end_cpu_access(
	struct drm_framebuffer *fb, enum dma_data_direction dir)
{
	drm_gem_fb_end_cpu_access(fb, dir);
}

static inline int
ms912x_drm_gem_fb_vmap_drm_gem_fb_vmap(struct drm_framebuffer *fb,
					      struct iosys_map *map,
					      struct iosys_map *data)
{
	return drm_gem_fb_vmap(fb, map, data);
}

static inline void
ms912x_drm_gem_fb_vunmap_drm_gem_fb_vunmap(struct drm_framebuffer *fb,
						struct iosys_map *map)
{
	drm_gem_fb_vunmap(fb, map);
}
#define ms912x_drm_gem_fb_begin_cpu_access \
	ms912x_drm_gem_fb_begin_cpu_access_drm_gem_fb_begin_cpu_access
#define ms912x_drm_gem_fb_end_cpu_access \
	ms912x_drm_gem_fb_end_cpu_access_drm_gem_fb_end_cpu_access
#define ms912x_drm_gem_fb_vmap ms912x_drm_gem_fb_vmap_drm_gem_fb_vmap
#define ms912x_drm_gem_fb_vunmap \
	ms912x_drm_gem_fb_vunmap_drm_gem_fb_vunmap
#else
static inline int
ms912x_drm_gem_fb_begin_cpu_access_dma_buf_begin_cpu_access(
	struct drm_framebuffer *fb, enum dma_data_direction dir)
{
	struct drm_gem_object *obj;
	unsigned int i;
	int ret;

	for (i = 0; i < ARRAY_SIZE(fb->obj); i++) {
		obj = drm_gem_fb_get_obj(fb, i);
		if (!obj || !obj->import_attach)
			continue;
		ret = dma_buf_begin_cpu_access(obj->import_attach->dmabuf, dir);
		if (ret)
			goto err_end_access;
	}
	return 0;

err_end_access:
	while (i--) {
		obj = drm_gem_fb_get_obj(fb, i);
		if (obj && obj->import_attach)
			dma_buf_end_cpu_access(obj->import_attach->dmabuf, dir);
	}
	return ret;
}

static inline void
ms912x_drm_gem_fb_end_cpu_access_dma_buf_end_cpu_access(
	struct drm_framebuffer *fb, enum dma_data_direction dir)
{
	struct drm_gem_object *obj;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(fb->obj); i++) {
		obj = drm_gem_fb_get_obj(fb, i);
		if (obj && obj->import_attach)
			dma_buf_end_cpu_access(obj->import_attach->dmabuf, dir);
	}
}

static inline int
ms912x_drm_gem_fb_vmap_drm_gem_shmem_vmap(
	struct drm_framebuffer *fb, struct iosys_map *map,
	struct iosys_map *data)
{
	struct drm_gem_object *obj = drm_gem_fb_get_obj(fb, 0);
	void *vaddr;

	if (!obj)
		return -EINVAL;
	vaddr = drm_gem_shmem_vmap(obj);
	if (IS_ERR_OR_NULL(vaddr))
		return vaddr ? PTR_ERR(vaddr) : -ENOMEM;
	map[0].vaddr = vaddr;
	data[0].vaddr = (u8 *)vaddr + fb->offsets[0];
	return 0;
}

static inline void
ms912x_drm_gem_fb_vunmap_drm_gem_shmem_vunmap(
	struct drm_framebuffer *fb, struct iosys_map *map)
{
	struct drm_gem_object *obj = drm_gem_fb_get_obj(fb, 0);

	if (obj && map[0].vaddr)
		drm_gem_shmem_vunmap(obj, map[0].vaddr);
}
#define ms912x_drm_gem_fb_begin_cpu_access \
	ms912x_drm_gem_fb_begin_cpu_access_dma_buf_begin_cpu_access
#define ms912x_drm_gem_fb_end_cpu_access \
	ms912x_drm_gem_fb_end_cpu_access_dma_buf_end_cpu_access
#define ms912x_drm_gem_fb_vmap ms912x_drm_gem_fb_vmap_drm_gem_shmem_vmap
#define ms912x_drm_gem_fb_vunmap \
	ms912x_drm_gem_fb_vunmap_drm_gem_shmem_vunmap
#endif

#include <linux/seq_file.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 19, 0)
#define ms912x_atomic_state drm_atomic_commit
#else
#define ms912x_atomic_state drm_atomic_state
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
static inline void
ms912x_plane_state_set_ignore_damage_clips_drm_plane_state_field(
	struct drm_plane_state *new_state, bool ignore)
{
	new_state->ignore_damage_clips = ignore;
}

static inline bool
ms912x_plane_state_ignore_damage_clips_drm_plane_state_field(
	const struct drm_plane_state *old_state,
	const struct drm_plane_state *new_state,
	const struct drm_crtc_state *new_crtc_state)
{
	(void)old_state;
	(void)new_crtc_state;
	return new_state->ignore_damage_clips;
}
#define ms912x_plane_state_set_ignore_damage_clips \
	ms912x_plane_state_set_ignore_damage_clips_drm_plane_state_field
#define ms912x_plane_state_ignore_damage_clips \
	ms912x_plane_state_ignore_damage_clips_drm_plane_state_field

static inline int
ms912x_crtc_helper_atomic_check_drm_crtc_helper_atomic_check(
	struct drm_crtc *crtc, struct ms912x_atomic_state *state)
{
	return drm_crtc_helper_atomic_check(crtc, state);
}
#define ms912x_crtc_helper_atomic_check \
	ms912x_crtc_helper_atomic_check_drm_crtc_helper_atomic_check
#else
static inline void
ms912x_plane_state_set_ignore_damage_clips_derived(
	struct drm_plane_state *new_state, bool ignore)
{
	(void)new_state;
	(void)ignore;
}

static inline bool
ms912x_plane_state_ignore_damage_clips_derived(
	const struct drm_plane_state *old_state,
	const struct drm_plane_state *new_state,
	const struct drm_crtc_state *new_crtc_state)
{
	bool framebuffer_changed = old_state->fb != new_state->fb;
	bool has_damage_clips = new_state->fb &&
		drm_plane_get_damage_clips_count(new_state);

	return (new_crtc_state &&
		drm_atomic_crtc_needs_modeset(new_crtc_state)) ||
		(old_state->fb && new_state->fb &&
		 old_state->fb->format->format != new_state->fb->format->format) ||
		(framebuffer_changed && !has_damage_clips);
}
#define ms912x_plane_state_set_ignore_damage_clips \
	ms912x_plane_state_set_ignore_damage_clips_derived
#define ms912x_plane_state_ignore_damage_clips \
	ms912x_plane_state_ignore_damage_clips_derived

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
static inline int
ms912x_crtc_helper_atomic_check_drm_atomic_helper_check_crtc_state(
	struct drm_crtc *crtc, struct ms912x_atomic_state *state)
{
	struct drm_crtc_state *new_crtc_state =
		drm_atomic_get_new_crtc_state(state, crtc);
	int ret;

	if (!new_crtc_state->enable)
		return 0;

	ret = drm_atomic_helper_check_crtc_state(new_crtc_state, false);
	if (ret)
		return ret;

	return drm_atomic_add_affected_planes(state, crtc);
}
#define ms912x_crtc_helper_atomic_check \
	ms912x_crtc_helper_atomic_check_drm_atomic_helper_check_crtc_state
#else
#define ms912x_crtc_helper_atomic_check NULL
#endif
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
#define ms912x_debugfs_info drm_debugfs_info

static inline struct drm_device *
ms912x_debugfs_device_from_seq_drm_debugfs_entry(struct seq_file *m)
{
	struct drm_debugfs_entry *entry = m->private;

	return entry->dev;
}

static inline void
ms912x_debugfs_add_files_drm_debugfs_add_files(
	struct drm_device *drm, const struct ms912x_debugfs_info *files,
	int count)
{
	drm_debugfs_add_files(drm, files, count);
}
#define ms912x_debugfs_device_from_seq \
	ms912x_debugfs_device_from_seq_drm_debugfs_entry
#define ms912x_debugfs_add_files ms912x_debugfs_add_files_drm_debugfs_add_files
#else
#define ms912x_debugfs_info drm_info_list

static inline struct drm_device *
ms912x_debugfs_device_from_seq_drm_info_node(struct seq_file *m)
{
	struct drm_info_node *entry = m->private;

	return entry->minor->dev;
}

static inline void
ms912x_debugfs_add_files_drm_debugfs_create_files(
	struct drm_device *drm, const struct ms912x_debugfs_info *files,
	int count)
{
	drm_debugfs_create_files(files, count, drm->primary->debugfs_root,
				 drm->primary);
}
#define ms912x_debugfs_device_from_seq \
	ms912x_debugfs_device_from_seq_drm_info_node
#define ms912x_debugfs_add_files \
	ms912x_debugfs_add_files_drm_debugfs_create_files
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
#define ms912x_edid drm_edid

static inline const struct ms912x_edid *
ms912x_edid_alloc_drm_edid_alloc(const void *data, size_t size)
{
	return drm_edid_alloc(data, size);
}

static inline const struct ms912x_edid *
ms912x_edid_read_custom_drm_edid_read_custom(
	struct drm_connector *connector,
	int (*read_block)(void *, u8 *, unsigned int, size_t), void *context)
{
	return drm_edid_read_custom(connector, read_block, context);
}

static inline const struct edid *
ms912x_edid_raw_drm_edid_raw(const struct ms912x_edid *edid)
{
	return drm_edid_raw(edid);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
static inline bool
ms912x_edid_valid_drm_edid_valid(const struct ms912x_edid *edid)
{
	return drm_edid_valid(edid);

}
#define ms912x_edid_valid ms912x_edid_valid_drm_edid_valid

static inline int
ms912x_edid_add_modes_drm_edid_connector_add_modes(
	struct drm_connector *connector, const struct ms912x_edid *edid)
{
	(void)edid;
	return drm_edid_connector_add_modes(connector);
}
#define ms912x_edid_add_modes \
	ms912x_edid_add_modes_drm_edid_connector_add_modes
#else
static inline bool
ms912x_edid_valid_drm_edid_is_valid(const struct ms912x_edid *edid)
{
	return drm_edid_is_valid((struct edid *)drm_edid_raw(edid));
}
#define ms912x_edid_valid ms912x_edid_valid_drm_edid_is_valid

static inline int
ms912x_edid_add_modes_drm_add_edid_modes(
	struct drm_connector *connector, const struct ms912x_edid *edid)
{
	return drm_add_edid_modes(connector,
				  (struct edid *)drm_edid_raw(edid));
}
#define ms912x_edid_add_modes ms912x_edid_add_modes_drm_add_edid_modes
#endif

static inline void
ms912x_edid_free_drm_edid_free(const struct ms912x_edid *edid)
{
	drm_edid_free(edid);
}

static inline int
ms912x_edid_connector_update_drm_edid_connector_update(
	struct drm_connector *connector, const struct ms912x_edid *edid)
{
	return drm_edid_connector_update(connector, edid);
}

static inline void
ms912x_edid_get_monitor_name_drm_edid_get_monitor_name(
	const struct ms912x_edid *edid, char *name, int size)
{
	drm_edid_get_monitor_name(drm_edid_raw(edid), name, size);
}
#define ms912x_edid_alloc ms912x_edid_alloc_drm_edid_alloc
#define ms912x_edid_read_custom \
	ms912x_edid_read_custom_drm_edid_read_custom
#define ms912x_edid_raw ms912x_edid_raw_drm_edid_raw
#define ms912x_edid_free ms912x_edid_free_drm_edid_free
#define ms912x_edid_connector_update \
	ms912x_edid_connector_update_drm_edid_connector_update
#define ms912x_edid_get_monitor_name \
	ms912x_edid_get_monitor_name_drm_edid_get_monitor_name
#else
#define ms912x_edid edid

static inline const struct ms912x_edid *
ms912x_edid_alloc_kmemdup(const void *data, size_t size)
{
	return kmemdup(data, size, GFP_KERNEL);
}

static inline const struct ms912x_edid *
ms912x_edid_read_custom_drm_do_get_edid(
	struct drm_connector *connector,
	int (*read_block)(void *, u8 *, unsigned int, size_t), void *context)
{
	return drm_do_get_edid(connector, read_block, context);
}

static inline const struct edid *
ms912x_edid_raw_legacy(const struct ms912x_edid *edid)
{
	return edid;
}

static inline bool
ms912x_edid_valid_drm_edid_is_valid(const struct ms912x_edid *edid)
{
	return drm_edid_is_valid((struct edid *)edid);
}

static inline void ms912x_edid_free_kfree(const struct ms912x_edid *edid)
{
	kfree(edid);
}

static inline int
ms912x_edid_connector_update_drm_connector_update_edid_property(
	struct drm_connector *connector, const struct ms912x_edid *edid)
{
	return drm_connector_update_edid_property(connector, edid);
}

static inline int
ms912x_edid_add_modes_drm_add_edid_modes(
	struct drm_connector *connector, const struct ms912x_edid *edid)
{
	return drm_add_edid_modes(connector, (struct edid *)edid);
}

static inline void
ms912x_edid_get_monitor_name_drm_edid_get_monitor_name(
	const struct ms912x_edid *edid, char *name, int size)
{
	drm_edid_get_monitor_name((struct edid *)edid, name, size);
}
#define ms912x_edid_alloc ms912x_edid_alloc_kmemdup
#define ms912x_edid_read_custom ms912x_edid_read_custom_drm_do_get_edid
#define ms912x_edid_raw ms912x_edid_raw_legacy
#define ms912x_edid_valid ms912x_edid_valid_drm_edid_is_valid
#define ms912x_edid_free ms912x_edid_free_kfree
#define ms912x_edid_connector_update \
	ms912x_edid_connector_update_drm_connector_update_edid_property
#define ms912x_edid_add_modes ms912x_edid_add_modes_drm_add_edid_modes
#define ms912x_edid_get_monitor_name \
	ms912x_edid_get_monitor_name_drm_edid_get_monitor_name
#endif

/*
 * DRM moved fbdev setup from the generic helper to DRM clients over time.
 * Keep the version-specific API at the boundary; callers use one stable name.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 18, 0)
#include <drm/clients/drm_client_setup.h>
#include <drm/drm_fbdev_shmem.h>

static inline void
ms912x_drm_client_setup_drm_client_setup(struct drm_device *drm)
{
	drm_client_setup(drm, drm_format_info(DRM_FORMAT_XRGB8888));
}
#define ms912x_drm_client_setup ms912x_drm_client_setup_drm_client_setup
#define MS912X_DRM_GEM_DRIVER_OPS \
	DRM_GEM_SHMEM_DRIVER_OPS, DRM_FBDEV_SHMEM_DRIVER_OPS
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
#include <drm/drm_client_setup.h>

static inline void
ms912x_drm_client_setup_drm_client_setup(struct drm_device *drm)
{
	drm_client_setup(drm, drm_format_info(DRM_FORMAT_XRGB8888));
}
#define ms912x_drm_client_setup ms912x_drm_client_setup_drm_client_setup
#define MS912X_DRM_GEM_DRIVER_OPS \
	DRM_GEM_SHMEM_DRIVER_OPS, .fbdev_probe = NULL
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
#include <drm/drm_fbdev_generic.h>

static inline void
ms912x_drm_client_setup_fbdev_generic_setup(struct drm_device *drm)
{
	drm_fbdev_generic_setup(drm, 32);
}
#define ms912x_drm_client_setup ms912x_drm_client_setup_fbdev_generic_setup
#define MS912X_DRM_GEM_DRIVER_OPS DRM_GEM_SHMEM_DRIVER_OPS
#else
#include <drm/drm_fb_helper.h>

static inline void
ms912x_drm_client_setup_fbdev_generic_setup(struct drm_device *drm)
{
	drm_fbdev_generic_setup(drm, 32);
}
#define ms912x_drm_client_setup ms912x_drm_client_setup_fbdev_generic_setup
#define MS912X_DRM_GEM_DRIVER_OPS DRM_GEM_SHMEM_DRIVER_OPS
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 18, 0)
static inline void
ms912x_drm_dev_set_dma_dev_drm_dev_set_dma_dev(struct drm_device *drm,
						       struct device *dma_dev)
{
	drm_dev_set_dma_dev(drm, dma_dev);
}
#define ms912x_drm_dev_set_dma_dev ms912x_drm_dev_set_dma_dev_drm_dev_set_dma_dev
#else
/* Older DRM cores use the device passed to drm_dev_alloc() for DMA. */
static inline void
ms912x_drm_dev_set_dma_dev_drm_dev_alloc_device(struct drm_device *drm,
							struct device *dma_dev)
{
	(void)drm;
	(void)dma_dev;
}
#define ms912x_drm_dev_set_dma_dev \
	ms912x_drm_dev_set_dma_dev_drm_dev_alloc_device
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 18, 0)
#include <drm/drm_managed.h>
#define ms912x_alloc_ordered_workqueue \
	ms912x_alloc_ordered_workqueue_drmm_alloc_ordered_workqueue
static inline struct workqueue_struct *
ms912x_alloc_ordered_workqueue_drmm_alloc_ordered_workqueue(
	struct drm_device *drm, const char *name, unsigned int flags)
{
	return drmm_alloc_ordered_workqueue(drm, "%s", flags, name);
}
#else
#include <drm/drm_managed.h>
#include <linux/workqueue.h>

static void ms912x_destroy_workqueue_managed(struct drm_device *drm,
					     void *data)
{
	(void)drm;
	destroy_workqueue(data);
}

static inline struct workqueue_struct *
ms912x_alloc_ordered_workqueue_alloc_ordered_workqueue(
	struct drm_device *drm, const char *name, unsigned int flags)
{
	struct workqueue_struct *workqueue;
	int ret;

	workqueue = alloc_ordered_workqueue("%s", flags, name);
	if (!workqueue)
		return ERR_PTR(-ENOMEM);

	ret = drmm_add_action_or_reset(drm, ms912x_destroy_workqueue_managed,
				      workqueue);
	if (ret)
		return ERR_PTR(ret);

	return workqueue;
}
#define ms912x_alloc_ordered_workqueue \
	ms912x_alloc_ordered_workqueue_alloc_ordered_workqueue
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 18, 0)
static inline int
ms912x_drm_kms_helper_poll_init_drmm_kms_helper_poll_init(
	struct drm_device *drm)
{
	drmm_kms_helper_poll_init(drm);
	return 0;
}
#define ms912x_drm_kms_helper_poll_init \
	ms912x_drm_kms_helper_poll_init_drmm_kms_helper_poll_init
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
static inline int
ms912x_drm_kms_helper_poll_init_drmm_kms_helper_poll_init(
	struct drm_device *drm)
{
	return drmm_kms_helper_poll_init(drm);
}
#define ms912x_drm_kms_helper_poll_init \
	ms912x_drm_kms_helper_poll_init_drmm_kms_helper_poll_init
#else
static void ms912x_drm_kms_helper_poll_fini_managed(
	struct drm_device *drm, void *data)
{
	(void)data;
	drm_kms_helper_poll_fini(drm);
}

static inline int
ms912x_drm_kms_helper_poll_init_drm_kms_helper_poll_init(
	struct drm_device *drm)
{
	drm_kms_helper_poll_init(drm);
	return drmm_add_action_or_reset(drm,
					ms912x_drm_kms_helper_poll_fini_managed,
					drm);
}
#define ms912x_drm_kms_helper_poll_init \
	ms912x_drm_kms_helper_poll_init_drm_kms_helper_poll_init
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
static inline void *
ms912x_kvrealloc_kvrealloc_newsize(const void *memory, size_t old_size,
				   size_t new_size, gfp_t flags)
{
	(void)old_size;
	return kvrealloc(memory, new_size, flags);
}
#define ms912x_kvrealloc(memory, old_size, new_size, flags) \
	ms912x_kvrealloc_kvrealloc_newsize(memory, old_size, new_size, flags)
#else
static inline void *
ms912x_kvrealloc_kvrealloc_oldsize(const void *memory, size_t old_size,
				   size_t new_size, gfp_t flags)
{
	return kvrealloc(memory, old_size, new_size, flags);
}
#define ms912x_kvrealloc(memory, old_size, new_size, flags) \
	ms912x_kvrealloc_kvrealloc_oldsize(memory, old_size, new_size, flags)
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
#define MS912X_USB_SHUTDOWN(shutdown_fn) .shutdown = shutdown_fn,
#else
#define MS912X_USB_SHUTDOWN(shutdown_fn)
#endif

#endif /* MS912X_COMPAT_H */
