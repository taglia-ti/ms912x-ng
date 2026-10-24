/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MS912X_SCAN_NEON_CORE_H
#define MS912X_SCAN_NEON_CORE_H
#ifdef __KERNEL__
#include <asm/neon-intrinsics.h>
#else
#include <arm_neon.h>
#endif
#include "ms912x_scan_methods.h"

/* AArch32 has pairwise reductions; AArch64 can reduce a whole register. */
#if defined(CONFIG_ARM64) || defined(__aarch64__)
static inline __attribute__((always_inline)) u32 ms912x_neon_max(uint32x4_t v)
{
	return vmaxvq_u32(v);
}
#else
static inline __attribute__((always_inline)) u32 ms912x_neon_max(uint32x4_t v)
{
	uint32x2_t p = vmax_u32(vget_low_u32(v), vget_high_u32(v));

	return vget_lane_u32(vpmax_u32(p, p), 0);
}
#endif

static inline __attribute__((always_inline)) uint32x4_t
ms912x_scan_neon_difference(const u32 *old, const u32 *new)
{
	return veorq_u32(vld1q_u32(old), vld1q_u32(new));
}

#define MS912X_MASKED_TYPE uint32x4_t
#define MS912X_MASKED_PIXELS 4
#define MS912X_MASKED_ZERO() vdupq_n_u32(0)
#define MS912X_MASKED_OR(a, b) vorrq_u32(a, b)
#define MS912X_MASKED_DIFF(a, b) ms912x_scan_neon_difference(a, b)
#define MS912X_MASKED_CHANGED(diff, mask) \
	(ms912x_neon_max(vandq_u32(diff, vdupq_n_u32(mask))) != 0)
#define MS912X_MASKED_COPY(dst, src) vst1q_u32(dst, vld1q_u32(src))
#define MS912X_MASKED_BLOCK ms912x_scan_neon_masked_block
#define MS912X_MASKED_ROW ms912x_scan_row_neon_masked
#define MS912X_MASKED_PREFIX ms912x_scan_neon_masked_prefix
#include "ms912x_scan_masked_template.h"

#endif
