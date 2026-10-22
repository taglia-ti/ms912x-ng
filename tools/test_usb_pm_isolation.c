// SPDX-License-Identifier: GPL-2.0-only

#include <assert.h>
#include <stddef.h>
#include <stdio.h>

struct drm_device { unsigned int adapter_id; };
struct ms912x_device { struct drm_device drm; };
struct usb_interface { struct ms912x_device *driver_data; };
typedef int pm_message_t;

#define to_usb_interface(device) ((struct usb_interface *)(device))

static struct drm_device *suspended_drm, *resumed_drm;
static int suspend_result, resume_result;

static struct ms912x_device *usb_get_intfdata(struct usb_interface *interface)
{
	return interface->driver_data;
}
static int drm_mode_config_helper_suspend(struct drm_device *drm)
{
	suspended_drm = drm;
	return suspend_result;
}
static int drm_mode_config_helper_resume(struct drm_device *drm)
{
	resumed_drm = drm;
	return resume_result;
}

#include "usb-pm.h"

int main(void)
{
	struct ms912x_device first = { .drm.adapter_id = 1 };
	struct ms912x_device second = { .drm.adapter_id = 2 };
	struct usb_interface first_interface = { .driver_data = &first };
	struct usb_interface second_interface = { .driver_data = &second };

	suspend_result = -5;
	assert(ms912x_usb_suspend(&first_interface, 0) == -5);
	assert(suspended_drm == &first.drm && !resumed_drm);

	resume_result = -11;
	assert(ms912x_usb_resume(&second_interface) == -11);
	assert(resumed_drm == &second.drm && suspended_drm == &first.drm);
	assert(first.drm.adapter_id == 1 && second.drm.adapter_id == 2);

	puts("PASS USB PM wrappers: suspend/resume dispatch to the selected adapter DRM device");
	return 0;
}
