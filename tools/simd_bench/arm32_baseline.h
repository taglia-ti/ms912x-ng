/* SPDX-License-Identifier: GPL-2.0-only */
/* Arithmetic backport of A64-only baseline intrinsics, for comparison only. */
#include <arm_neon.h>

static inline uint32_t vmaxvq_u32(uint32x4_t v)
{
    uint32x2_t p = vmax_u32(vget_low_u32(v), vget_high_u32(v));
    return vget_lane_u32(vpmax_u32(p, p), 0);
}
static inline uint32_t vminvq_u32(uint32x4_t v)
{
    uint32x2_t p = vmin_u32(vget_low_u32(v), vget_high_u32(v));
    return vget_lane_u32(vpmin_u32(p, p), 0);
}
static inline uint32_t vaddvq_u32(uint32x4_t v)
{
    uint32x2_t p = vadd_u32(vget_low_u32(v), vget_high_u32(v));
    return vget_lane_u32(vpadd_u32(p, p), 0);
}
static inline uint32x4_t vpaddq_u32(uint32x4_t a, uint32x4_t b)
{
    return vcombine_u32(vpadd_u32(vget_low_u32(a), vget_high_u32(a)),
                       vpadd_u32(vget_low_u32(b), vget_high_u32(b)));
}
