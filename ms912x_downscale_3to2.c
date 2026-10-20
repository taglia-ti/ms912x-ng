// SPDX-License-Identifier: GPL-2.0-only

#ifdef CONFIG_X86_64
#include <asm/cpufeature.h>
#include <asm/simd.h>
#endif

#include "ms912x.h"
#include "ms912x_downscale_3to2.h"
#include "ms912x_scan_methods.h"

void ms912x_downscale_xrgb8888_3to2_region(const u32 *src, u32 *dst,
					 unsigned int src_width,
					 unsigned int src_height,
					 unsigned int dst_width,
					 unsigned int dst_height,
					 unsigned int src_stride,
					 unsigned int dst_stride,
					 unsigned int offset_x, unsigned int offset_y)
{
#ifdef CONFIG_X86_64
	if (may_use_simd() && boot_cpu_has(X86_FEATURE_AVX) &&
	    boot_cpu_has(X86_FEATURE_AVX2)) {
		ms912x_downscale_xrgb8888_3to2_avx2_region(
			src, dst, src_width, src_height, dst_width, dst_height,
			src_stride, dst_stride, offset_x, offset_y);
		return;
	}
	if (may_use_simd() && boot_cpu_has(X86_FEATURE_XMM2)) {
		ms912x_downscale_xrgb8888_3to2_sse2_region(
			src, dst, src_width, src_height, dst_width, dst_height,
			src_stride, dst_stride, offset_x, offset_y);
		return;
	}
#elif MS912X_HAS_NEON
	if (ms912x_scan_neon_available()) {
		ms912x_downscale_xrgb8888_3to2_neon_region(
			src, dst, src_width, src_height, dst_width, dst_height,
			src_stride, dst_stride, offset_x, offset_y);
		return;
	}
#endif
	ms912x_downscale_xrgb8888_3to2_scalar_region(
		src, dst, src_width, src_height, dst_width, dst_height,
		src_stride, dst_stride, offset_x, offset_y);
}

void ms912x_downscale_xrgb8888_3to2(const u32 *src, u32 *dst,
		unsigned int sw, unsigned int sh, unsigned int dw, unsigned int dh,
		unsigned int ss, unsigned int ds)
{
	ms912x_downscale_xrgb8888_3to2_region(src, dst, sw, sh, dw, dh, ss, ds, 0, 0);
}
