// SPDX-License-Identifier: GPL-2.0-only
#include <stdio.h>

#include "test_scan_config_compat.h"

struct ms912x_parameters_test ms912x_parameters;

#include "scan-config.h"

static void init_device(struct ms912x_device *ms912x,
			unsigned int tolerance, unsigned int stride)
{
	memset(ms912x, 0, sizeof(*ms912x));
	ms912x->damage_scan_lock = 1;
	ms912x->damage_scan_config_generation =
		ms912x_parameters.damage_scan_config_generation;
	ms912x->damage_scan_probe_config_generation =
		ms912x_parameters.damage_scan_config_generation;
	ms912x->damage_scan_stride_default = stride;
	ms912x->effective_tolerance = tolerance;
	ms912x->damage_scan_valid = true;
	ms912x->damage_scan_width = 800;
	ms912x->damage_scan_height = 600;
}

static void resolve(struct ms912x_device *device,
		    struct ms912x_scan_config *config,
		    unsigned int frame_generation)
{
	memset(config, 0, sizeof(*config));
	ms912x_damage_scan_resolve_config_locked(device, frame_generation,
						 800, 600, false, config);
}

static void adapters_keep_scan_configuration_and_epochs_separate(void)
{
	struct ms912x_device usb2, usb3;
	struct ms912x_scan_config usb2_config, usb3_config;

	memset(&ms912x_parameters, 0, sizeof(ms912x_parameters));
	ms912x_parameters.damage_scan_config_generation = 4;
	ms912x_parameters.damage_scan_method = MS912X_DAMAGE_SCAN_SCALAR;
	ms912x_parameters.damage_scan_stride = 8;
	init_device(&usb2, 3, 4);
	init_device(&usb3, 11, 7);
	usb2.damage_scan_device_config_generation = 2;
	usb2.damage_scan_stride_is_overridden = true;
	usb2.damage_scan_stride_override_value = 2;
	usb3.damage_scan_device_config_generation = 5;

	resolve(&usb2, &usb2_config, 10);
	resolve(&usb3, &usb3_config, 20);
	assert(usb2_config.stride == 2 && usb3_config.stride == 7);
	assert(usb2_config.tolerance == 3 && usb3_config.tolerance == 11);
	assert(usb2.damage_scan_invalidation_epoch == 1);
	assert(usb3.damage_scan_invalidation_epoch == 1);
	assert(usb2.damage_scan_probe_generation == 10);
	assert(usb3.damage_scan_probe_generation == 20);

	resolve(&usb2, &usb2_config, 10);
	assert(usb2.damage_scan_invalidation_epoch == 1);
	assert(usb3.damage_scan_invalidation_epoch == 1);

	ms912x_parameters.damage_scan_config_generation++;
	resolve(&usb2, &usb2_config, 10);
	assert(usb2.damage_scan_invalidation_epoch == 2);
	assert(usb3.damage_scan_invalidation_epoch == 1);
	resolve(&usb3, &usb3_config, 20);
	assert(usb3.damage_scan_invalidation_epoch == 2);

	usb2.damage_scan_device_config_generation++;
	resolve(&usb2, &usb2_config, 10);
	assert(usb2.damage_scan_invalidation_epoch == 3);
	assert(usb3.damage_scan_invalidation_epoch == 2);
}

int main(void)
{
	adapters_keep_scan_configuration_and_epochs_separate();
	puts("PASS scan config: per-adapter stride, tolerance, generations and invalidation");
	return 0;
}
