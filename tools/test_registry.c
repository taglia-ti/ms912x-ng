// SPDX-License-Identifier: GPL-2.0-only
#include <stdio.h>

#include "test_registry_compat.h"

struct test_mutex ms912x_devices_lock;
LIST_HEAD(ms912x_devices);
unsigned int test_warning_count;
bool test_allocation_failure;
struct ms912x_device *test_remove_device_on_refresh;

void ms912x_device_registry_remove(struct ms912x_device *ms912x);

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

static void ms912x_transfer_force_full_update(struct ms912x_device *ms912x)
{
	if (test_remove_device_on_refresh) {
		struct ms912x_device *removed = test_remove_device_on_refresh;

		test_remove_device_on_refresh = NULL;
		removed->drm.unplugged = true;
		ms912x_device_registry_remove(removed);
	}
	assert(!ms912x_devices_lock.held);
	if (!ms912x->drm.unplugged)
		ms912x->drm.full_refreshes++;
}

#include "registry.h"
#include "edid-invalidate.h"

static void init_device(struct ms912x_device *ms912x)
{
	memset(ms912x, 0, sizeof(*ms912x));
	ms912x->drm.refs = 1;
	INIT_LIST_HEAD(&ms912x->device_list);
}

static void registry_membership_and_refresh_fanout(void)
{
	struct ms912x_device first, second;

	init_device(&first);
	init_device(&second);
	ms912x_device_registry_add(&first);
	ms912x_device_registry_add(&second);

	ms912x_refresh_devices_after_draws_off();
	assert(first.drm.full_refreshes == 1);
	assert(second.drm.full_refreshes == 1);
	assert(first.drm.refs == 1 && second.drm.refs == 1);

	second.drm.unplugged = true;
	ms912x_refresh_devices_after_draws_off();
	assert(first.drm.full_refreshes == 2);
	assert(second.drm.full_refreshes == 1);
	assert(first.drm.refs == 1 && second.drm.refs == 1);

	ms912x_device_registry_remove(&first);
	ms912x_device_registry_remove(&second);
	ms912x_refresh_devices_after_draws_off();
	assert(first.drm.full_refreshes == 2);
	assert(second.drm.full_refreshes == 1);
}

static void snapshot_allocation_failure_releases_cleanly(void)
{
	struct ms912x_device device;

	init_device(&device);
	ms912x_device_registry_add(&device);
	test_allocation_failure = true;
	ms912x_refresh_devices_after_draws_off();
	test_allocation_failure = false;
	assert(test_warning_count == 1);
	assert(device.drm.refs == 1 && device.drm.full_refreshes == 0);
	ms912x_device_registry_remove(&device);
}

static void removal_after_snapshot_keeps_fanout_safe(void)
{
	struct ms912x_device first, second;

	init_device(&first);
	init_device(&second);
	ms912x_device_registry_add(&first);
	ms912x_device_registry_add(&second);
	test_remove_device_on_refresh = &second;
	ms912x_refresh_devices_after_draws_off();
	assert(first.drm.full_refreshes == 1);
	assert(second.drm.full_refreshes == 0);
	assert(first.drm.refs == 1 && second.drm.refs == 1);
	assert(second.device_list.next == &second.device_list &&
	       second.device_list.prev == &second.device_list);
	ms912x_device_registry_remove(&first);
}

static void edid_cache_invalidation_is_per_device(void)
{
	struct ms912x_device first, second;
	unsigned int i;

	init_device(&first);
	init_device(&second);
	for (i = 0; i < MS912X_EDID_CACHE_BLOCKS; i++) {
		first.edid_cache_valid[i] = true;
		second.edid_cache_valid[i] = true;
	}
	ms912x_edid_cache_invalidate(&first);
	for (i = 0; i < MS912X_EDID_CACHE_BLOCKS; i++) {
		assert(!first.edid_cache_valid[i]);
		assert(second.edid_cache_valid[i]);
	}
	assert(!first.edid_lock.held && !second.edid_lock.held);
}

int main(void)
{
	registry_membership_and_refresh_fanout();
	snapshot_allocation_failure_releases_cleanly();
	removal_after_snapshot_keeps_fanout_safe();
	edid_cache_invalidation_is_per_device();
	puts("PASS adapter state: registry fanout/removal, snapshot failure and EDID cache isolation");
	return 0;
}
