// SPDX-License-Identifier: GPL-2.0-only
#include <stdio.h>

#include "test_registry_compat.h"

struct test_mutex ms912x_devices_lock;
struct test_mutex ms912x_profile_lock;
LIST_HEAD(ms912x_devices);
unsigned int test_warning_count;
bool test_allocation_failure;

static void ms912x_profile_configure_device_locked(
	struct ms912x_device *ms912x)
{
	assert(ms912x_profile_lock.held && ms912x_devices_lock.held);
	ms912x->profile_configurations++;
}

#define pr_warn_ratelimited(...) (++test_warning_count)

static void *kcalloc(size_t count, size_t size, int flags)
{
	(void)flags;
	if (test_allocation_failure)
		return NULL;
	return calloc(count, size);
}

static void kfree(void *pointer)
{
	free(pointer);
}

static void drm_dev_get(struct drm_device *drm)
{
	assert(ms912x_devices_lock.held);
	drm->refs++;
}

static void drm_dev_put(struct drm_device *drm)
{
	assert(drm->refs > 1);
	drm->refs--;
}

static bool drm_dev_enter(struct drm_device *drm, int *idx)
{
	assert(!ms912x_devices_lock.held);
	*idx = 1;
	return !drm->unplugged;
}

static void drm_dev_exit(int idx)
{
	assert(idx == 1);
}

static void drm_kms_helper_hotplug_event(struct drm_device *drm)
{
	struct ms912x_device *ms912x =
		container_of(drm, struct ms912x_device, drm);

	assert(!ms912x_devices_lock.held && !ms912x_profile_lock.held);
	ms912x->drm.hotplug_events++;
}

#include "registry.h"

static void init_device(struct ms912x_device *ms912x)
{
	memset(ms912x, 0, sizeof(*ms912x));
	ms912x->drm.refs = 1;
	INIT_LIST_HEAD(&ms912x->device_list);
}

static void registry_membership_and_hotplug_fanout(void)
{
	struct ms912x_device first, second;

	init_device(&first);
	init_device(&second);
	first.fake_edid_active = true;
	second.fake_edid_active = true;
	ms912x_device_registry_add(&first);
	ms912x_device_registry_add(&second);
	assert(first.profile_configurations == 1);
	assert(second.profile_configurations == 1);

	ms912x_notify_devices_hotplug();
	assert(first.drm.hotplug_events == 1);
	assert(second.drm.hotplug_events == 1);
	assert(!first.fake_edid_active && !second.fake_edid_active);
	assert(first.drm.refs == 1 && second.drm.refs == 1);

	first.fake_edid_active = true;
	second.fake_edid_active = true;
	second.drm.unplugged = true;
	ms912x_notify_devices_hotplug();
	assert(first.drm.hotplug_events == 2);
	assert(second.drm.hotplug_events == 1);
	assert(!first.fake_edid_active && second.fake_edid_active);
	assert(first.drm.refs == 1 && second.drm.refs == 1);

	ms912x_device_registry_remove(&first);
	ms912x_device_registry_remove(&second);
	ms912x_notify_devices_hotplug();
	assert(first.drm.hotplug_events == 2);
	assert(second.drm.hotplug_events == 1);
}

static void snapshot_allocation_failure_releases_cleanly(void)
{
	struct ms912x_device device;

	init_device(&device);
	ms912x_device_registry_add(&device);
	test_allocation_failure = true;
	ms912x_notify_devices_hotplug();
	test_allocation_failure = false;
	assert(test_warning_count == 1);
	assert(device.drm.refs == 1 && device.drm.hotplug_events == 0);
	ms912x_device_registry_remove(&device);
}

int main(void)
{
	registry_membership_and_hotplug_fanout();
	snapshot_allocation_failure_releases_cleanly();
	puts("PASS registry: membership, unlocked fanout, unplug guard and allocation failure");
	return 0;
}
