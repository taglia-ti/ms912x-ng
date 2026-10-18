// SPDX-License-Identifier: GPL-2.0-only

#include <asm/neon.h>
#include <asm/simd.h>

#include <linux/bitops.h>
#include <linux/kernel.h>

#include <arm_neon.h>

#include "ms912x_scan_methods.h"

static __always_inline void
ms912x_scan_row_neon_blocks(const u32 *old, const u32 *new,
			   unsigned int width,
			   struct ms912x_scan_row_result *result)
{
	unsigned int x = 0;

	ms912x_scan_row_result_init(result, width);
	while (x < width) {
		unsigned int end = min(width, x + 16), vx = x, bits = 0;
		unsigned int changed, mask;

		for (; vx + 4 <= end; vx += 4) {
			uint32x4_t eq = vceqq_u32(vld1q_u32(&old[vx]),
						  vld1q_u32(&new[vx]));
			u32 lanes[4];

			vst1q_u32(lanes, eq);
			bits |= (lanes[0] == 0) | ((lanes[1] == 0) << 1) |
				((lanes[2] == 0) << 2) | ((lanes[3] == 0) << 3);
		}
		for (; vx < end; vx++)
			bits |= old[vx] != new[vx];
		changed = !!bits;
		mask = -changed;
		result->avx2_blocks++;
		result->avx2_equal_blocks += !changed;
		result->changed_pixels += (end - x) * changed;
		result->min_x = min(result->min_x,
				    (x & mask) | (width & ~mask));
		result->max_x = max(result->max_x, (end - 1) & mask);
		x = end;
	}
	result->equal_row = !result->changed_pixels;
}

static __always_inline void
ms912x_scan_row_neon_relaxed(const u32 *old, const u32 *new, u32 *next,
			    unsigned int width, unsigned int tolerance,
			    struct ms912x_scan_row_result *result)
{
	const uint8x16_t threshold = vdupq_n_u8(tolerance);
	const uint32x4_t rgb_mask = vdupq_n_u32(0x00ffffff);
	unsigned int x = 0;

	ms912x_scan_row_result_init(result, width);
	for (; x + 4 <= width; x += 4) {
		uint8x16_t a = vld1q_u8((const u8 *)&old[x]);
		uint8x16_t b = vld1q_u8((const u8 *)&new[x]);
		uint8x16_t diff = vmaxq_u8(vqsubq_u8(a, b), vqsubq_u8(b, a));
		uint8x16_t over = vandq_u8(vqsubq_u8(diff, threshold),
					   vreinterpretq_u8_u32(rgb_mask));
		uint32x4_t changed_mask = vmvnq_u32(vceqq_u32(
			vreinterpretq_u32_u8(over), vdupq_n_u32(0)));
		u32 lanes[4];
		unsigned int bits, first, last, has_change;

		vst1q_u32(lanes, changed_mask);
		bits = !!lanes[0] | (!!lanes[1] << 1) |
			(!!lanes[2] << 2) | (!!lanes[3] << 3);
		first = __ffs(bits | 0x10) - 1;
		last = fls(bits);
		has_change = !!bits;
		vst1q_u32(&next[x], vbslq_u32(changed_mask,
			vld1q_u32(&new[x]), vld1q_u32(&old[x])));
		result->changed_pixels += hweight8(bits);
		result->min_x = min(result->min_x, x + first);
		result->max_x = max(result->max_x,
				    (x + last - 1) & -has_change);
	}
	for (; x < width; x++) {
		unsigned int changed = ms912x_scan_pixel_relaxed_changed(
			old[x], new[x], tolerance);
		unsigned int mask = -changed;

		result->changed_pixels += changed;
		result->min_x = min(result->min_x,
				    (x & mask) | (width & ~mask));
		result->max_x = max(result->max_x, x & mask);
		next[x] = (old[x] & ~mask) | (new[x] & mask);
	}
	result->equal_row = !result->changed_pixels;
}

void ms912x_scan_rows_neon_blocks(const u32 *old, const u32 *new,
				  unsigned int width, unsigned int row_stride,
				  unsigned int row_count,
				  struct ms912x_scan_row_result *results)
{
	unsigned int row;

	kernel_neon_begin();
	for (row = 0; row < row_count; row++)
		ms912x_scan_row_neon_blocks(old + row * row_stride,
					   new + row * row_stride, width,
					   &results[row]);
	kernel_neon_end();
}

void ms912x_scan_rows_neon_relaxed(const u32 *old, const u32 *new,
				   u32 *next, unsigned int width,
				   unsigned int row_stride, unsigned int row_count,
				   unsigned int tolerance,
				   struct ms912x_scan_row_result *results)
{
	unsigned int row;

	kernel_neon_begin();
	for (row = 0; row < row_count; row++)
		ms912x_scan_row_neon_relaxed(old + row * row_stride,
					    new + row * row_stride,
					    next + row * row_stride, width,
					    tolerance, &results[row]);
	kernel_neon_end();
}
