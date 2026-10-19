// SPDX-License-Identifier: GPL-2.0-only

#ifdef CONFIG_X86_64
#include <asm/cpufeature.h>
#include <asm/simd.h>
#endif

#include "ms912x.h"
#include "ms912x_downscale.h"
#include "ms912x_scan_methods.h"

void ms912x_downscale_xrgb8888_2x(const u32 *src, u32 *dst,
					 unsigned int src_width,
					 unsigned int src_height,
					 unsigned int dst_width,
					 unsigned int dst_height,
					 unsigned int src_stride,
					 unsigned int dst_stride)
{
#ifdef CONFIG_X86_64
	if (may_use_simd() && boot_cpu_has(X86_FEATURE_AVX) &&
	    boot_cpu_has(X86_FEATURE_AVX2)) {
		ms912x_downscale_xrgb8888_2x_avx2(
			src, dst, src_width, src_height, dst_width, dst_height,
			src_stride, dst_stride);
		return;
	}
	if (may_use_simd() && boot_cpu_has(X86_FEATURE_XMM2)) {
		ms912x_downscale_xrgb8888_2x_sse2(
			src, dst, src_width, src_height, dst_width, dst_height,
			src_stride, dst_stride);
		return;
	}
#elif MS912X_HAS_NEON
	if (ms912x_scan_neon_available()) {
		ms912x_downscale_xrgb8888_2x_neon(
			src, dst, src_width, src_height, dst_width, dst_height,
			src_stride, dst_stride);
		return;
	}
#endif
	ms912x_downscale_xrgb8888_2x_scalar(
		src, dst, src_width, src_height, dst_width, dst_height,
		src_stride, dst_stride);
}