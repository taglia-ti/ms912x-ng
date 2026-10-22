/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MS912X_TEST_SCAN_CONFIG_COMPAT_H
#define MS912X_TEST_SCAN_CONFIG_COMPAT_H

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

typedef uint32_t u32;

#define READ_ONCE(value) (value)
#define WRITE_ONCE(value, next) ((value) = (next))
#define clamp_t(type, value, low, high) \
	((type)((value) < (low) ? (low) : (value) > (high) ? (high) : (value)))
#define MS912X_SCAN_MASKED_BLOCK_PIXELS 16
#define MS912X_SCAN_MEMCMP_BLOCK_BYTES 128

enum ms912x_damage_scan_method {
	MS912X_DAMAGE_SCAN_SCALAR,
	MS912X_DAMAGE_SCAN_MEMCMP,
	MS912X_DAMAGE_SCAN_AVX,
	MS912X_DAMAGE_SCAN_SSE2,
	MS912X_DAMAGE_SCAN_NEON,
	MS912X_DAMAGE_SCAN_METHOD_AUTO,
};

struct ms912x_parameters_test {
	int damage_scan_config_generation;
	enum ms912x_damage_scan_method damage_scan_method;
	bool damage_scan_stride_override;
	unsigned int damage_scan_stride;
};

struct ms912x_device {
	int damage_scan_lock;
	int damage_scan_config_generation;
	int damage_scan_probe_config_generation;
	u32 damage_scan_device_config_generation;
	u32 damage_scan_local_config_generation;
	u32 damage_scan_local_probe_config_generation;
	u32 damage_scan_probe_generation;
	bool damage_scan_x_probe_done;
	bool damage_scan_x_stable;
	bool damage_scan_stride_is_overridden;
	unsigned int damage_scan_stride_override_value;
	unsigned int damage_scan_stride_default;
	unsigned int effective_tolerance;
	bool video_in_rgb565;
	bool damage_scan_valid;
	unsigned int damage_scan_width;
	unsigned int damage_scan_height;
	int damage_scan_invalid;
	unsigned int damage_scan_backend;
	uint64_t damage_scan_invalidation_epoch;
};

struct ms912x_scan_config {
	unsigned int generation;
	u32 local_generation;
	unsigned int tolerance;
	unsigned int stride;
	unsigned int run_block_pixels;
	unsigned int run_tolerance;
	u32 pixel_mask;
	enum ms912x_damage_scan_method method;
	bool rgb565;
	bool full_update;
	bool use_memcmp;
	bool use_simd;
	bool use_relaxed;
};

struct ms912x_damage_scan_result {
	uint64_t invalidation_epoch;
	unsigned int config_generation;
	u32 local_config_generation;
};

extern struct ms912x_parameters_test ms912x_parameters;

static inline int atomic_read(const int *value)
{
	return *value;
}

static inline void atomic_set(int *value, int next)
{
	*value = next;
}

#define lockdep_assert_held(lock) assert(*(lock))

static inline u32 ms912x_scan_rgb565_tolerance_mask(unsigned int tolerance)
{
	return 0x56560000U | tolerance;
}

static inline u32 ms912x_scan_tolerance_mask(unsigned int tolerance)
{
	return 0x88880000U | tolerance;
}

static inline unsigned int
ms912x_damage_scan_effective_tolerance(struct ms912x_device *ms912x)
{
	return ms912x->effective_tolerance;
}

#endif /* MS912X_TEST_SCAN_CONFIG_COMPAT_H */
