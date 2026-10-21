// SPDX-License-Identifier: GPL-2.0-only
/* Matched native framebuffer -> capture -> scan/baseline -> staging -> wire path. */
#include "pipeline.h"
#include <time.h>
#include <stdio.h>
#include "ms912x_pixels.h"
#include "ms912x_scan_methods.h"
#include "ms912x_scan_rects.h"
#include "pipeline-functions.h"

struct drm_rect *ms912x_rect_vector_grow(struct drm_rect *rects,
					 unsigned *capacity, unsigned required,
					 unsigned limit)
{
	if (required <= *capacity)
		return rects;
	*capacity = min(limit, max(required, *capacity * 2));
	rects = realloc(rects, *capacity * sizeof(*rects));
	assert(rects);
	return rects;
}

void pipeline_init(struct pipeline *p, unsigned width, unsigned height,
		   unsigned stride, bool rgb565, bool scan,
		   const struct drm_rect *changed, rgb565_encode_fn encode)
{
	*p = (struct pipeline){ .width = width,
				.height = height,
				.stride = stride,
				.rgb565 = rgb565,
				.scan = scan,
				.encode = encode,
				.capacity = 1 };
	size_t bytes = (size_t)MS912X_MAX_WIDTH * height * (rgb565 ? 2 : 4);
	p->sources[0] = calloc(1, bytes);
	p->sources[1] = calloc(1, bytes);
	p->capture = calloc(1, bytes);
	p->baseline = calloc(1, bytes);
	p->next = calloc(1, bytes);
	p->staging = calloc(1, bytes);
	p->output = calloc(1, (size_t)width * height * 2 +
				      MS912X_MAX_DAMAGE_RECTS * 16);
	p->rects = calloc(1, sizeof(*p->rects));
	p->index = ms912x_scan_rect_index_alloc();
	assert(p->sources[0] && p->sources[1] && p->capture && p->baseline &&
	       p->next && p->staging && p->output && p->rects && p->index);
	for (unsigned y = 0; y < height; y++)
		for (unsigned x = 0; x < width; x++) {
			unsigned v =
				((x * 73 + y * 19) ^ (y * 59 + x * 13)) & 255;
			u32 color = (v << 16) | ((255 - v) << 8) | (v ^ 0x55);
			bool hit = x >= (unsigned)changed->x1 &&
				   x < (unsigned)changed->x2 &&
				   y >= (unsigned)changed->y1 &&
				   y < (unsigned)changed->y2;
			for (unsigned f = 0; f < 2; f++) {
				u32 c = color ^ ((f && hit) ? 0x00f0f0f0U : 0);
				if (rgb565)
					((u16 *)p->sources[f])
						[y * MS912X_MAX_WIDTH + x] =
							((c >> 8) & 0xf800) |
							((c >> 5) & 0x07e0) |
							((c >> 3) & 31);
				else
					p->sources[f][y * MS912X_MAX_WIDTH + x] =
						c;
			}
		}
	memcpy(p->baseline, p->sources[0], bytes);
	memcpy(p->next, p->sources[0], bytes);
}

size_t pipeline_frame(struct pipeline *p)
{
	bool rgb = p->rgb565;
	unsigned frame = ++p->sequence & 1;
	struct drm_format_info format = { .format =
						  rgb ? DRM_FORMAT_RGB565 : 0 };
	struct drm_framebuffer fb = { .format = &format,
				      .pitches = { MS912X_MAX_WIDTH *
						   (rgb ? 2 : 4) } };
	struct iosys_map map = { .data = p->sources[frame] };
	ms912x_capture_pixel_rows(p->capture, MS912X_MAX_WIDTH, 0, 0, &fb, &map,
				  0, 0, p->width, p->height);
	p->count = 0;
	if (p->scan) {
		struct ms912x_scan_work work = {
			.index = p->index,
			.frame = p->capture,
			.baseline = p->baseline,
			.next_snapshot = p->next,
			.rects = &p->rects,
			.rect_capacity = &p->capacity,
			.rect_count = &p->count,
			.width = p->width,
			.height = p->height,
			.word_width = p->width >> rgb,
			.row_words = MS912X_MAX_WIDTH >> rgb,
			.pixel_shift = rgb
		};
		struct ms912x_scan_config config = {
			.stride = p->stride,
			.run_block_pixels = MS912X_SCAN_MASKED_BLOCK_PIXELS,
			.pixel_mask =
				rgb ? ms912x_scan_rgb565_tolerance_mask(0) :
				      ms912x_scan_tolerance_mask(3),
			.use_relaxed = true,
			.method =
#if defined(__x86_64__)
				__builtin_cpu_supports("avx") ?
					MS912X_DAMAGE_SCAN_AVX :
					MS912X_DAMAGE_SCAN_SSE2
#else
				PIPELINE_SCAN_METHOD
#endif
		};
		unsigned rows = 0;
		ms912x_scan_rect_index_reset(p->index, p->height);
		ms912x_damage_scan_prepare_baseline(&work, &config);
		ms912x_damage_scan_run_rows(&work, &config, p->sequence, &rows);
		ms912x_damage_scan_extend_bottom(&work);
		u32 *swap = p->baseline;
		p->baseline = p->next;
		p->next = swap;
	} else {
		p->rects[0] = (struct drm_rect){ 0, 0, p->width, p->height };
		p->count = 1;
	}
	size_t offset = 0;
	for (unsigned i = 0; i < p->count; i++) {
		const struct drm_rect *r = &p->rects[i];
		ms912x_pixels_copy_rect(p->staging, p->capture, r, rgb);
		int ret = p->encode(p->output + offset, p->staging, p->width,
				    p->height, r, i + 1 == p->count);
		assert(!ret);
		offset +=
			(size_t)drm_rect_width(r) * drm_rect_height(r) * 2 + 16;
	}
	return offset;
}

void pipeline_destroy(struct pipeline *p)
{
	free(p->sources[0]);
	free(p->sources[1]);
	free(p->capture);
	free(p->baseline);
	free(p->next);
	free(p->staging);
	free(p->output);
	free(p->rects);
	ms912x_scan_rect_index_free(p->index);
}

void pipeline_checks(void)
{
	unsigned cases = 0;
	const unsigned widths[] = { 2, 6, 62, 64, 66, 126, 128, 130 };
	/* Both halves of every u32 comparison word use the same wire-ignore mask. */
	for (unsigned k = 0; k < ARRAY_SIZE(widths); k++)
		for (unsigned stride = 1; stride <= 4; stride++)
			for (unsigned phase = 0; phase < stride; phase++)
				for (unsigned edge = 0; edge < 2; edge++)
					for (unsigned ignored = 0; ignored < 2;
					     ignored++) {
						unsigned w = widths[k], h = 9,
							 x = edge ? w - 1 : 0,
							 y = phase;
						struct pipeline p;
						struct drm_rect empty = { 0 };
						pipeline_init(&p, w, h, stride,
							      true, true,
							      &empty,
							      rgb565_scalar);
						p.sequence = phase + stride - 1;
						for (unsigned f = 0; f < 2; f++)
							((u16 *)p.sources[f])
								[y * MS912X_MAX_WIDTH +
								 x] ^=
								ignored ?
									0x0801 :
									0x0402;
						pipeline_frame(&p);
						assert(p.count ==
						       (ignored ? 0U : 1U));
						if (!ignored) {
							const struct drm_rect *
								r = &p.rects[0];
							assert(r->x1 ==
							       (int)(x / 64 *
								     64));
							assert(r->x2 ==
							       (int)min(
								       w,
								       x / 64 * 64 +
									       64));
							assert(r->y1 ==
							       (int)(y >= stride - 1 ?
									     y - stride +
										     1 :
									     0));
							assert(r->y2 ==
							       (int)min(
								       h,
								       y + stride));
							for (unsigned yy = 0;
							     yy < h; yy++)
								for (unsigned xx =
									     0;
								     xx < w;
								     xx++) {
									u16 b = ((u16 *)p.baseline)
										[yy * MS912X_MAX_WIDTH +
										 xx];
									u16 f = ((u16 *)p.sources
											 [0])
										[yy * MS912X_MAX_WIDTH +
										 xx];
									assert(b ==
									       f);
								}
						}
						pipeline_destroy(&p);
						cases++;
					}
	/* Cropped framebuffer capture honors byte pitch and native destination stride. */
	for (unsigned rgb = 0; rgb < 2; rgb++) {
		u8 source[6 * 80], dest[6 * 80];
		for (unsigned i = 0; i < sizeof(source); i++)
			source[i] = i * 37;
		memset(dest, 0xa5, sizeof(dest));
		struct drm_format_info format = {
			.format = rgb ? DRM_FORMAT_RGB565 : 0
		};
		struct drm_framebuffer fb = { .format = &format,
					      .pitches = { 80 } };
		struct iosys_map map = { .data = source };
		unsigned bytes = rgb ? 2 : 4;
		ms912x_capture_pixel_rows((u32 *)dest, 16, 2, 1, &fb, &map, 3,
					  2, 6, 3);
		for (unsigned i = 0; i < sizeof(dest); i++) {
			unsigned y = i / (16 * bytes), x = i % (16 * bytes);
			u8 expected = 0xa5;
			if (y >= 1 && y < 4 && x >= 2 * bytes && x < 8 * bytes)
				expected = source[(y + 1) * 80 + 3 * bytes + x -
						  2 * bytes];
			assert(dest[i] == expected);
		}
		cases++;
	}
	printf("PASS packed pipeline: %u mask halves, word tails, pixel bounds, stride margins, baseline and pitched capture cases\n",
	       cases);
}
