// SPDX-License-Identifier: GPL-2.0-only
#include "pipeline.h"
#include <stdio.h>
#include <time.h>
static u64 pipeline_now(void)
{
	struct timespec t;
	assert(!clock_gettime(CLOCK_MONOTONIC_RAW, &t));
	return (u64)t.tv_sec * 1000000000 + t.tv_nsec;
}
static int cmp_double(const void *a, const void *b)
{
	double x = *(const double *)a, y = *(const double *)b;
	return (x > y) - (x < y);
}
static volatile size_t consumed;
static u64 pipeline_measure(struct pipeline *p, size_t n)
{
	u64 start = pipeline_now();
	size_t sum = 0;
	for (size_t i = 0; i < n; i++)
		sum += pipeline_frame(p);
	u64 elapsed = pipeline_now() - start;
	consumed += sum;
	return elapsed;
}

void pipeline_benchmark(const char *path)
{
#if defined(__x86_64__)
	rgb565_encode_fn uyvy =
		__builtin_cpu_supports("avx2") ? uyvy_avx2 : uyvy_sse2;
	rgb565_encode_fn candidate =
		__builtin_cpu_supports("avx2") ? rgb565_avx2 : rgb565_sse2;
#else
	rgb565_encode_fn uyvy = uyvy_neon, candidate = rgb565_neon;
#endif
	const struct {
		const char *name;
		unsigned width, height, stride;
		bool scan;
		unsigned kind;
	} cases[] = {
		{ "1080p-full-scan-off", 1920, 1080, 1, false, 0 },
		{ "1080p-full-scan-1", 1920, 1080, 1, true, 0 },
		{ "1080p-video60-scan-2", 1920, 1080, 2, true, 1 },
		{ "1080p-cursor-scan-2", 1920, 1080, 2, true, 2 },
		{ "1080p-idle-scan-2", 1920, 1080, 2, true, 3 },
		{ "720p-full-scan-off", 1280, 720, 1, false, 0 },
		{ "720p-video60-scan-2", 1280, 720, 2, true, 1 },
	};
	FILE *csv = fopen(path, "w");
	assert(csv);
	fprintf(csv,
		"case,path,sample,iterations,duration_ns,wire_bytes_last_frame\n");
	for (unsigned c = 0; c < sizeof(cases) / sizeof(cases[0]); c++) {
		const unsigned w = cases[c].width, h = cases[c].height;
		struct drm_rect change = { 0, 0, w, h };
		if (cases[c].kind == 1)
			change.y2 = h * 3 / 5;
		if (cases[c].kind == 2)
			change = (struct drm_rect){ 64, 64, 128, 128 };
		if (cases[c].kind == 3)
			change = (struct drm_rect){ 0 };
		struct pipeline paths[3];
		const char *names[] = { "XRGB8888-UYVY-SIMD",
					"RGB565-RGB565-scalar64",
					"RGB565-RGB565-SIMD" };
		rgb565_encode_fn encode[] = { uyvy, rgb565_scalar, candidate };
		size_t n[3];
		double samples[3][7];
		for (unsigned b = 0; b < 3; b++) {
			pipeline_init(&paths[b], w, h, cases[c].stride, b != 0,
				      cases[c].scan, &change, encode[b]);
			size_t i = 1;
			u64 elapsed;
			do {
				elapsed = pipeline_measure(&paths[b], i);
				i *= 2;
			} while (elapsed < 5000000);
			n[b] = (size_t)((double)(i / 2) * 30000000 / elapsed) +
			       1;
			/* Balance both alternating frames and sampled-row phases in every sample. */
			n[b] = (n[b] + 1) & ~(size_t)1;
		}
		for (unsigned s = 0; s < 7; s++)
			for (unsigned k = 0; k < 3; k++) {
				unsigned b = (s + k) % 3;
				u64 ns = pipeline_measure(&paths[b], n[b]);
				size_t bytes = pipeline_frame(&paths[b]);
				samples[b][s] = (double)ns / n[b];
				fprintf(csv, "%s,%s,%u,%zu,%llu,%zu\n",
					cases[c].name, names[b], s, n[b],
					(unsigned long long)ns, bytes);
			}
		for (unsigned b = 0; b < 3; b++) {
			qsort(samples[b], 7, sizeof(double), cmp_double);
			printf("pipeline %-24s %-25s %9.3f us\n", cases[c].name,
			       names[b], samples[b][3] / 1000);
			pipeline_destroy(&paths[b]);
		}
		fflush(stdout);
		fflush(csv);
	}
	assert(!fclose(csv));
}
