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
	ms912x->damage_scan_stride_is_overridden = true;
	ms912x->damage_scan_stride_override_value = stride;
	ms912x->effective_tolerance = tolerance;
	ms912x->damage_scan_valid = true;
	ms912x->damage_scan_width = 800;
	ms912x->damage_scan_height = 600;
}

static void resolve(struct ms912x_device *device,
		    struct ms912x_scan_config *config,
		    unsigned int frame_generation,
		    unsigned int width, unsigned int height)
{
	memset(config, 0, sizeof(*config));
	ms912x_damage_scan_resolve_config_locked(device, frame_generation,
							 width, height, false, config);
}

static struct ms912x_damage_scan_result result_for(
						struct ms912x_device *device,
						const struct ms912x_scan_config *config)
{
	return (struct ms912x_damage_scan_result) {
		.invalidation_epoch = device->damage_scan_invalidation_epoch,
		.config_generation = config->generation,
		.local_config_generation = config->local_generation,
	};
}

static void adapters_keep_scan_configuration_and_epochs_separate(void)
{
	struct ms912x_device usb2, usb3;
	struct ms912x_scan_config usb2_config, usb3_config;

	memset(&ms912x_parameters, 0, sizeof(ms912x_parameters));
	ms912x_parameters.damage_scan_config_generation = 4;
	ms912x_parameters.damage_scan_method = MS912X_DAMAGE_SCAN_SCALAR;
	init_device(&usb2, 3, 4);
	init_device(&usb3, 11, 7);
	usb2.damage_scan_device_config_generation = 2;
	usb2.damage_scan_stride_is_overridden = true;
	usb2.damage_scan_stride_override_value = 2;
	usb3.damage_scan_device_config_generation = 5;

	resolve(&usb2, &usb2_config, 10, 800, 600);
	resolve(&usb3, &usb3_config, 20, 800, 600);
	assert(usb2_config.stride == 2 && usb3_config.stride == 7);
	assert(usb2_config.tolerance == 3 && usb3_config.tolerance == 11);
	assert(usb2_config.pixel_mask == 0xfcfcfcfcU && usb3_config.pixel_mask == 0xf8f8f8f8U);
	assert(usb2.damage_scan_invalidation_epoch == 1);
	assert(usb3.damage_scan_invalidation_epoch == 1);

	resolve(&usb2, &usb2_config, 10, 800, 600);
	assert(usb2.damage_scan_invalidation_epoch == 1);
	assert(usb3.damage_scan_invalidation_epoch == 1);

	ms912x_parameters.damage_scan_config_generation++;
	resolve(&usb2, &usb2_config, 10, 800, 600);
	assert(usb2.damage_scan_invalidation_epoch == 2);
	assert(usb3.damage_scan_invalidation_epoch == 1);
	resolve(&usb3, &usb3_config, 20, 800, 600);
	assert(usb3.damage_scan_invalidation_epoch == 2);

	usb2.damage_scan_device_config_generation++;
	resolve(&usb2, &usb2_config, 10, 800, 600);
	assert(usb2.damage_scan_invalidation_epoch == 3);
	assert(usb3.damage_scan_invalidation_epoch == 2);
}

static void stale_results_after_global_update_and_modeset_are_rejected(void)
{
	struct ms912x_device first, second;
	struct ms912x_scan_config first_config, second_config;
	struct ms912x_damage_scan_result first_result, second_result;

	memset(&ms912x_parameters, 0, sizeof(ms912x_parameters));
	ms912x_parameters.damage_scan_config_generation = 20;
	ms912x_parameters.damage_scan_method = MS912X_DAMAGE_SCAN_SCALAR;
	init_device(&first, 0, 1);
	init_device(&second, 0, 1);
	first.damage_scan_lock = second.damage_scan_lock = 1;
	first.damage_scan_config_generation = 20;
	second.damage_scan_config_generation = 20;
	first.damage_scan_valid = second.damage_scan_valid = false;
	first.damage_scan_invalid = second.damage_scan_invalid = 1;
	resolve(&first, &first_config, 1, 800, 600);
	resolve(&second, &second_config, 1, 800, 600);
	first_result = result_for(&first, &first_config);
	second_result = result_for(&second, &second_config);

	/* A module-wide update invalidates a result captured under the old config. */
	ms912x_parameters.damage_scan_config_generation++;
	assert(!ms912x_damage_scan_commit_locked(&first, &first_result, 800, 600));
	assert(!first.damage_scan_valid && first.damage_scan_width == 800);
	assert(!ms912x_damage_scan_commit_locked(&second, &second_result, 800, 600));
	assert(!second.damage_scan_valid && second.damage_scan_width == 800);
	resolve(&second, &second_config, 1, 800, 600);
	second_result = result_for(&second, &second_config);
	assert(ms912x_damage_scan_commit_locked(&second, &second_result, 800, 600));
	assert(second.damage_scan_valid && !second.damage_scan_invalid);

	/* A modeset dimensions change makes old geometry ineligible for commit. */
	resolve(&first, &first_config, 2, 1024, 768);
	assert(first_config.full_update);
	first_result = result_for(&first, &first_config);
	assert(ms912x_damage_scan_commit_locked(&first, &first_result, 1024, 768));
	assert(first.damage_scan_width == 1024 && first.damage_scan_height == 768);
	assert(second.damage_scan_width == 800 && second.damage_scan_height == 600);
	assert(second.damage_scan_valid);
}

int main(void)
{
	adapters_keep_scan_configuration_and_epochs_separate();
	stale_results_after_global_update_and_modeset_are_rejected();
	puts("PASS scan config: per-adapter settings, global-update staleness and modeset dimensions");
	return 0;
}
