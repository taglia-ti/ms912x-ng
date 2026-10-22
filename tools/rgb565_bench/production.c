// SPDX-License-Identifier: GPL-2.0-only
/* ABI adapters and platform controls only; all pixel/framing code is production. */
#include "bench.h"
#include <asm/cpufeature.h>
#include "ms912x_color.h"
#include "ms912x_rgb565.h"
#include "ms912x_scan_methods.h"

struct ms912x_device;
static struct {
	enum ms912x_fast_chroma_backend fast_chroma;
} ms912x_parameters;

#include "production-functions.h"

void bench_configure(bool simd, enum ms912x_fast_chroma_backend chroma)
{
	bench_simd_allowed = simd;
	ms912x_parameters.fast_chroma = chroma;
}

int bench_encode_rgb565(void *dst, const void *src, unsigned int width,
			unsigned int height, const struct drm_rect *rect,
			bool final)
{
	return ms912x_convert_rgb565_rect(dst, src, width, height, rect, final);
}

int bench_encode_uyvy(void *dst, const void *src, unsigned int width,
		      unsigned int height, const struct drm_rect *rect,
		      bool final)
{
	return ms912x_convert_xrgb8888_to_uyvy_rect(dst, src, NULL, width,
						    height, rect, final);
}

int bench_encode_batch(bool rgb565, void *dst, const void *src,
		       unsigned int width, unsigned int height,
		       const struct drm_rect *rects, unsigned int count)
{
	if (rgb565)
		return ms912x_convert_rgb565_batch(dst, src, width, height,
						   rects, count);
	return ms912x_convert_xrgb8888_to_uyvy_batch(dst, src, NULL, width,
						     height, rects, count);
}
