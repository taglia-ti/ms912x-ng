// SPDX-License-Identifier: GPL-2.0-only

#include <asm/neon.h>
#include <asm/simd.h>

#include <linux/kernel.h>

#include <arm_neon.h>

#include "ms912x_scan_methods.h"

static __always_inline uint32x4_t
ms912x_scan_neon_difference(const u32 *old, const u32 *new)
{
	return veorq_u32(vld1q_u32(old), vld1q_u32(new));
}

/* Reduce vector differences once per damage block, preserving its granularity. */
static __always_inline void
ms912x_scan_row_neon_blocks(const u32 *old, const u32 *new, unsigned int width,
			    struct ms912x_scan_row_result *result)
{
	unsigned int x = 0;

	ms912x_scan_row_result_init(result, width);
	for (; x + 16 <= width; x += 16) {
		uint32x4_t d0 = ms912x_scan_neon_difference(old + x, new + x);
		uint32x4_t d1 = ms912x_scan_neon_difference(old + x + 4, new + x + 4);
		uint32x4_t d2 = ms912x_scan_neon_difference(old + x + 8, new + x + 8);
		uint32x4_t d3 = ms912x_scan_neon_difference(old + x + 12, new + x + 12);
		uint32x4_t diff;

		diff = vorrq_u32(vorrq_u32(d0, d1), vorrq_u32(d2, d3));
		ms912x_scan_record_block(result, width, x, x + 16, vmaxvq_u32(diff) != 0);
	}
	if (x < width) {
		uint32x4_t diff = vdupq_n_u32(0);
		unsigned int vx = x, changed = 0;

		for (; vx + 4 <= width; vx += 4)
			diff = vorrq_u32(diff, ms912x_scan_neon_difference(old + vx, new + vx));
		for (; vx < width; vx++)
			changed |= old[vx] != new[vx];
		changed |= vmaxvq_u32(diff) != 0;
		ms912x_scan_record_block(result, width, x, width, changed);
	}
	result->equal_row = !result->changed_pixels;
}

/* Keep compiler-generated SIMD inside the FPU-protected batch call. */
static noinline void
ms912x_scan_rows_neon_blocks_fpu(const u32 *old, const u32 *new, unsigned int width,
				 unsigned int row_stride, unsigned int row_count,
				 struct ms912x_scan_row_result *results)
{
	unsigned int row;

	for (row = 0; row < row_count; row++)
		ms912x_scan_row_neon_blocks(old + row * row_stride, new + row * row_stride, width,
					    &results[row]);
}

void
ms912x_scan_rows_neon_blocks(const u32 *old, const u32 *new, unsigned int width,
			     unsigned int row_stride, unsigned int row_count,
			     struct ms912x_scan_row_result *results)
{
	kernel_neon_begin();
	ms912x_scan_rows_neon_blocks_fpu(old, new, width, row_stride, row_count, results);
	kernel_neon_end();
}

static __always_inline void
ms912x_scan_row_neon_relaxed(const u32 *old, const u32 *new, u32 *next, unsigned int width,
			     unsigned int tolerance, struct ms912x_scan_row_result *result)
{
	const uint8x16_t threshold = vdupq_n_u8(tolerance);
	const uint32x4_t rgb_mask = vdupq_n_u32(0x00ffffff);
	const uint32x4_t zero = vdupq_n_u32(0);
	uint32x4_t counts = zero, first = vdupq_n_u32(width), last = zero;
	uint32x4_t positions = {0, 1, 2, 3};
	unsigned int x = 0;

	ms912x_scan_row_result_init(result, width);
	for (; x + 4 <= width; x += 4) {
		uint8x16_t a = vld1q_u8((const u8 *)&old[x]);
		uint8x16_t b = vld1q_u8((const u8 *)&new[x]);
		uint8x16_t over =
		    vandq_u8(vcgtq_u8(vabdq_u8(a, b), threshold), vreinterpretq_u8_u32(rgb_mask));
		uint32x4_t equal = vceqq_u32(vreinterpretq_u32_u8(over), zero);
		uint32x4_t changed = vmvnq_u32(equal);

		vst1q_u8((u8 *)&next[x], vbslq_u8(vreinterpretq_u8_u32(equal), a, b));
		counts = vsubq_u32(counts, changed);
		first = vminq_u32(first, vorrq_u32(positions, equal));
		last = vmaxq_u32(last, vandq_u32(positions, changed));
		positions = vaddq_u32(positions, vdupq_n_u32(4));
	}
	result->changed_pixels = vaddvq_u32(counts);
	result->min_x = vminvq_u32(first);
	result->max_x = vmaxvq_u32(last);
	for (; x < width; x++) {
		unsigned int changed = ms912x_scan_pixel_relaxed_changed(old[x], new[x], tolerance);
		unsigned int mask = -changed;

		result->changed_pixels += changed;
		result->min_x = min(result->min_x, (x & mask) | (width & ~mask));
		result->max_x = max(result->max_x, x & mask);
		next[x] = (old[x] & ~mask) | (new[x] & mask);
	}
	result->equal_row = !result->changed_pixels;
}

static noinline void
ms912x_scan_rows_neon_relaxed_fpu(const u32 *old, const u32 *new, u32 *next, unsigned int width,
				  unsigned int row_stride, unsigned int row_count,
				  unsigned int tolerance, struct ms912x_scan_row_result *results)
{
	unsigned int row;

	for (row = 0; row < row_count; row++)
		ms912x_scan_row_neon_relaxed(old + row * row_stride, new + row * row_stride,
					     next + row * row_stride, width, tolerance,
					     &results[row]);
}

void
ms912x_scan_rows_neon_relaxed(const u32 *old, const u32 *new, u32 *next, unsigned int width,
			      unsigned int row_stride, unsigned int row_count,
			      unsigned int tolerance, struct ms912x_scan_row_result *results)
{
	kernel_neon_begin();
	ms912x_scan_rows_neon_relaxed_fpu(old, new, next, width, row_stride, row_count, tolerance,
					  results);
	kernel_neon_end();
}
