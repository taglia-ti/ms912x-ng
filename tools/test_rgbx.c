/* SPDX-License-Identifier: GPL-2.0-only */
#define _GNU_SOURCE
#include "ms912x_rgbx_convert.h"
#include "ms912x_color_core.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

static uint32_t random_state = 0x4c912abcU;
static uint8_t random_byte(void)
{
	random_state ^= random_state << 13;
	random_state ^= random_state >> 17;
	random_state ^= random_state << 5;
	return (uint8_t)random_state;
}

static void reference(uint8_t *dst, size_t stride, const struct ms912x_rgbx_view *src)
{
	unsigned int r = src->order == MS912X_RGBX ? 0 : 2, b = 2U - r;
	for (uint32_t y = 0; y < src->height; ++y) {
		const uint8_t *row = src->data + src->offset +
			((size_t)y + src->crop_y) * src->row_stride +
			(size_t)src->crop_x * src->pixel_stride;
		for (uint32_t x = 0; x < src->width; x += 2U) {
			const uint8_t *a = row + (size_t)x * src->pixel_stride;
			const uint8_t *c = a + src->pixel_stride;
			uint32_t pair = ms912x_core_rgb_pair_to_uyvy(a[r], a[1], a[b], c[r], c[1], c[b]);
			uint8_t *out = dst + (size_t)y * stride + (size_t)x * 2U;
			out[0] = (uint8_t)pair; out[1] = (uint8_t)(pair >> 8);
			out[2] = (uint8_t)(pair >> 16); out[3] = (uint8_t)(pair >> 24);
		}
	}
}

static size_t test_differential(void)
{
	static const uint32_t widths[] = {2, 4, 6, 8, 10, 14, 16, 18, 30, 64, 126, 640, 1366, 1920};
	static const uint32_t heights[] = {1, 3, 8};
	static const size_t steps[] = {4, 8, 12}, alignments[] = {0, 1, 3, 15};
	size_t cases = 0;
	for (size_t wi = 0; wi < sizeof(widths) / sizeof(widths[0]); ++wi)
	for (size_t hi = 0; hi < sizeof(heights) / sizeof(heights[0]); ++hi)
	for (size_t si = 0; si < sizeof(steps) / sizeof(steps[0]); ++si)
	for (size_t ai = 0; ai < sizeof(alignments) / sizeof(alignments[0]); ++ai)
	for (unsigned int order = 0; order < 2; ++order) {
		size_t offset = 7 + alignments[ai];
		size_t stride = (widths[wi] + 3U) * steps[si] + 19;
		size_t bytes = offset + (heights[hi] + 1U) * stride + (widths[wi] + 2U) * steps[si];
		size_t out_stride = widths[wi] * 2U + 13, guard = 32 + alignments[ai];
		size_t out_bytes = guard + heights[hi] * out_stride + 32;
		uint8_t *input = malloc(bytes), *actual = malloc(out_bytes), *expected = malloc(out_bytes);
		struct ms912x_rgbx_view src = {
			.data = input, .bytes = bytes, .offset = offset,
			.row_stride = stride, .pixel_stride = steps[si], .crop_x = 2, .crop_y = 1,
			.width = widths[wi], .height = heights[hi], .order = (enum ms912x_rgbx_order)order,
		};
		assert(input && actual && expected);
		for (size_t i = 0; i < bytes; ++i) input[i] = random_byte();
		memset(expected, 0xa5, out_bytes);
		reference(expected + guard, out_stride, &src);
		for (unsigned int backend = 0; backend <= MS912X_COLOR_AVX2; ++backend) {
			struct ms912x_rgbx_plan plan;
			if (!ms912x_rgbx_backend_supported((enum ms912x_color_backend)backend)) {
				assert(ms912x_rgbx_prepare(&plan, &src,
					(enum ms912x_color_backend)backend) == -ENOTSUP);
				continue;
			}
			memset(actual, 0xa5, out_bytes);
			assert(ms912x_rgbx_prepare(&plan, &src, (enum ms912x_color_backend)backend) == 0);
			if (steps[si] != 4) assert(plan.backend == MS912X_COLOR_SCALAR);
			if (steps[si] == 4 && backend == MS912X_COLOR_AUTO)
				assert(plan.backend == ms912x_rgbx_default_backend());
			assert(ms912x_rgbx_execute(&plan, actual + guard, out_bytes - guard, out_stride) == 0);
			assert(memcmp(actual, expected, out_bytes) == 0);
			++cases;
		}
		for (uint32_t y = 0; y < src.height; ++y)
			for (uint32_t x = 0; x < src.width; ++x)
				input[offset + (y + src.crop_y) * stride + (x + src.crop_x) * steps[si] + 3] ^= 255;
		memset(actual, 0xa5, out_bytes);
		assert(ms912x_rgbx_to_uyvy(actual + guard, out_bytes - guard, out_stride,
			&src, MS912X_COLOR_AUTO) == 0);
		assert(memcmp(actual, expected, out_bytes) == 0);
		free(input); free(actual); free(expected);
	}
	return cases;
}

static void test_validation(void)
{
	uint8_t input[128] = {0}, dst[64];
	struct ms912x_rgbx_view valid = {
		.data = input, .bytes = sizeof(input), .row_stride = 16,
		.pixel_stride = 4, .width = 4, .height = 3, .order = MS912X_RGBX,
	}, invalid;
	memset(dst, 0xab, sizeof(dst));
#define REJECT(field, value) do { \
	invalid = valid; invalid.field = (value); \
	assert(ms912x_rgbx_to_uyvy(dst, sizeof(dst), 8, &invalid, MS912X_COLOR_AUTO) < 0); \
} while (0)
	REJECT(width, 3);
	REJECT(width, 0);
	REJECT(bytes, 47);
	REJECT(offset, SIZE_MAX);
	REJECT(pixel_stride, 3);
	REJECT(crop_x, 1);
	REJECT(order, (enum ms912x_rgbx_order)42);
	REJECT(row_stride, SIZE_MAX);
#undef REJECT
	assert(ms912x_rgbx_to_uyvy(dst, sizeof(dst), 8, &valid, (enum ms912x_color_backend)99) == -ENOTSUP);
	assert(ms912x_rgbx_to_uyvy(dst, 23, 8, &valid, MS912X_COLOR_AUTO) < 0);
	assert(ms912x_rgbx_to_uyvy(dst, sizeof(dst), 7, &valid, MS912X_COLOR_AUTO) < 0);
	assert(ms912x_rgbx_to_uyvy(dst, sizeof(dst), SIZE_MAX, &valid, MS912X_COLOR_AUTO) < 0);
	assert(ms912x_rgbx_to_uyvy(input + 4, 64, 8, &valid, MS912X_COLOR_AUTO) < 0);
	for (size_t i = 0; i < sizeof(dst); ++i) assert(dst[i] == 0xab);
}

static uint8_t *guarded_buffer(size_t bytes, void **mapping, size_t *length)
{
	size_t page = (size_t)sysconf(_SC_PAGESIZE), pages = (bytes + page - 1U) / page;
	uint8_t *base;
	*length = (pages + 1U) * page;
	base = mmap(NULL, *length, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	assert(base != MAP_FAILED);
	assert(mprotect(base + pages * page, page, PROT_NONE) == 0);
	*mapping = base;
	return base + pages * page - bytes;
}

static void test_guard_pages(void)
{
	static const uint32_t widths[] = {2, 4, 6, 8, 10, 14, 16, 18, 30, 1366, 1920};
	for (size_t i = 0; i < sizeof(widths) / sizeof(widths[0]); ++i) {
		void *source_map, *dest_map;
		size_t source_len, dest_len;
		uint32_t width = widths[i];
		uint8_t *input = guarded_buffer(width * 4U, &source_map, &source_len);
		uint8_t *output = guarded_buffer(width * 2U, &dest_map, &dest_len);
		struct ms912x_rgbx_view src = {
			.data = input, .bytes = width * 4U, .row_stride = width * 4U,
			.pixel_stride = 4, .width = width, .height = 1, .order = MS912X_RGBX,
		};
		for (size_t j = 0; j < width * 4U; ++j) input[j] = random_byte();
		uint8_t *expected = malloc(width * 2U);
		assert(expected);
		for (unsigned int order = 0; order < 2; ++order) {
			src.order = (enum ms912x_rgbx_order)order;
			reference(expected, width * 2U, &src);
			for (unsigned int backend = 0; backend <= MS912X_COLOR_AVX2; ++backend) {
				if (!ms912x_rgbx_backend_supported((enum ms912x_color_backend)backend)) continue;
				assert(ms912x_rgbx_to_uyvy(output, width * 2U, width * 2U,
					&src, (enum ms912x_color_backend)backend) == 0);
				assert(memcmp(output, expected, width * 2U) == 0);
			}
		}
		free(expected);
		munmap(source_map, source_len); munmap(dest_map, dest_len);
	}
}

static double seconds(void)
{
	struct timespec ts;
	assert(clock_gettime(CLOCK_MONOTONIC, &ts) == 0);
	return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static int compare_double(const void *a, const void *b)
{
	double x = *(const double *)a, y = *(const double *)b;
	return (x > y) - (x < y);
}

static void benchmark(void)
{
	static const uint32_t sizes[][2] = {{640, 480}, {1366, 768}, {1920, 1080}, {6, 64}};
	volatile unsigned int checksum = 0;
	puts("order,backend,width,height,median_ms,mpixels_per_s");
	for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
		uint32_t width = sizes[i][0], height = sizes[i][1];
		size_t pixels = (size_t)width * height;
		uint8_t *src = malloc(pixels * 4U), *dst = malloc(pixels * 2U);
		struct ms912x_rgbx_view view = {
			.data = src, .bytes = pixels * 4U, .row_stride = width * 4U,
			.pixel_stride = 4, .width = width, .height = height, .order = MS912X_RGBX,
		};
		size_t iterations = pixels < 10000 ? 20000 : 30;
		assert(src && dst);
		for (size_t j = 0; j < pixels * 4U; ++j) src[j] = random_byte();
		for (unsigned int order = 0; order < 2; ++order) {
			view.order = (enum ms912x_rgbx_order)order;
			for (unsigned int choice = MS912X_COLOR_SCALAR; choice <= MS912X_COLOR_AVX2; ++choice) {
				struct ms912x_rgbx_plan plan;
				double samples[7];
				if (!ms912x_rgbx_backend_supported((enum ms912x_color_backend)choice)) continue;
				assert(ms912x_rgbx_prepare(&plan, &view,
					(enum ms912x_color_backend)choice) == 0);
				for (size_t sample = 0; sample < 7; ++sample) {
					double start = seconds();
					for (size_t n = 0; n < iterations; ++n)
						assert(ms912x_rgbx_execute(&plan, dst, pixels * 2U, width * 2U) == 0);
					samples[sample] = (seconds() - start) / iterations;
					checksum += dst[0];
				}
				qsort(samples, 7, sizeof(samples[0]), compare_double);
				printf("%s,%s,%u,%u,%.6f,%.3f\n", order ? "bgrx" : "rgbx",
					ms912x_rgbx_backend_name(plan.backend),
					width, height, samples[3] * 1000, pixels / samples[3] / 1e6);
			}
			/* Keep the same colors while changing their byte positions. */
			for (size_t j = 0; j < pixels; ++j) {
				uint8_t red = src[j * 4U];
				src[j * 4U] = src[j * 4U + 2U];
				src[j * 4U + 2U] = red;
			}
		}
		free(src); free(dst);
	}
	(void)checksum;
}

int main(int argc, char **argv)
{
	if (argc > 1 && strcmp(argv[1], "--bench") == 0) {
		benchmark();
		return 0;
	}
	test_validation();
	test_guard_pages();
	printf("RGBX conversion: %zu differential cases passed; auto=%s\n",
		test_differential(), ms912x_rgbx_backend_name(ms912x_rgbx_default_backend()));
	return 0;
}
