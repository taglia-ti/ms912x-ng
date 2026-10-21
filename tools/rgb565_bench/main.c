// SPDX-License-Identifier: GPL-2.0-only
#include "bench.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sched.h>
#include <sys/mman.h>
#include <sys/utsname.h>
#if defined(__arm__)
#include <sys/auxv.h>
#include <asm/hwcap.h>
#endif

enum { HEIGHT = 1080, SAMPLES = 7, SLOTS = 8 };
struct backend {
	const char *name;
	rgb565_encode_fn run;
	bool copy_control, uyvy;
};
static const struct backend backends[] = {
	{ "scalar-copy", rgb565_nomask, true, false },
	{ "scalar-mask", rgb565_scalar, false, false },
	{ "scalar-row", rgb565_scalar_row, false, false },
	{ "UYVY-scalar", uyvy_scalar, false, true },
#if defined(__x86_64__)
	{ "SSE2", rgb565_sse2, false, false },
	{ "AVX2", rgb565_avx2, false, false },
	{ "UYVY-SSE2", uyvy_sse2, false, true },
	{ "UYVY-AVX2", uyvy_avx2, false, true },
#else
	{ "NEON", rgb565_neon, false, false },
	{ "UYVY-NEON", uyvy_neon, false, true },
#endif
};
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
static unsigned int random_state = 0x912565;
static volatile unsigned long checksum;
bool bench_simd_allowed = true, bench_neon_present = true;
bool ms912x_scan_neon_available(void)
{
	return bench_simd_allowed && bench_neon_present;
}

static u32 random32(void)
{
	random_state ^= random_state << 13;
	random_state ^= random_state >> 17;
	random_state ^= random_state << 5;
	return random_state;
}

static bool available(const struct backend *b)
{
#if defined(__x86_64__)
	if (!strcmp(b->name, "SSE2") || !strcmp(b->name, "UYVY-SSE2"))
		return __builtin_cpu_supports("sse2");
	if (!strcmp(b->name, "AVX2") || !strcmp(b->name, "UYVY-AVX2"))
		return __builtin_cpu_supports("avx2");
#elif defined(__arm__)
	if (strstr(b->name, "NEON"))
		return getauxval(AT_HWCAP) & HWCAP_NEON;
#else
	(void)b;
#endif
	return true;
}

static size_t encoded_bytes(const struct drm_rect *r)
{
	return (size_t)(r->x2 - r->x1) * (r->y2 - r->y1) * 2 + 16;
}

/* Independent byte oracle: no production mask, framing or packing helpers. */
static void oracle(u8 *out, const void *pixels, const struct drm_rect *r,
		   bool mask, bool uyvy, bool final)
{
	const u32 *src = pixels;
	const u16 *native = pixels;
	u32 xy = ((u32)r->x1 << 12) | r->y1;
	u32 wh = ((u32)(r->x2 - r->x1) << 12) | (r->y2 - r->y1);

	out[0] = 0xff;
	out[1] = 0;
	out[2] = xy >> 16;
	out[3] = xy >> 8;
	out[4] = xy;
	out[5] = wh >> 16;
	out[6] = wh >> 8;
	out[7] = wh;
	out += 8;
	for (int y = r->y1; y < r->y2; y++) {
		if (uyvy) {
			for (int x = r->x1; x < r->x2; x += 2) {
				u32 a = src[y * MS912X_MAX_WIDTH + x];
				u32 b = src[y * MS912X_MAX_WIDTH + x + 1];
				int r0 = (a >> 16) & 255, r1 = (b >> 16) & 255;
				int g0 = (a >> 8) & 255, g1 = (b >> 8) & 255;
				int b0 = a & 255, b1 = b & 255;

				*out++ = (128 * 131072 - 9676 * (r0 + r1) -
					  18996 * (g0 + g1) +
					  28672 * (b0 + b1)) /
					 131072;
				*out++ = 16 +
					 (16763 * r0 + 32904 * g0 + 6391 * b0) /
						 65536;
				*out++ =
					(128 * 131072 + 28672 * (r0 + r1) -
					 24009 * (g0 + g1) - 4663 * (b0 + b1)) /
					131072;
				*out++ = 16 +
					 (16763 * r1 + 32904 * g1 + 6391 * b1) /
						 65536;
			}
			continue;
		}
		for (int x = r->x1; x < r->x2; x++) {
			u32 pixel = native[y * MS912X_MAX_WIDTH + x];

			*out++ = pixel & (mask ? 0xfe : 0xff);
			*out++ = (pixel >> 8) & (mask ? 0xf7 : 0xff);
		}
	}
	out[0] = 0xff;
	out[1] = final ? 0xc0 : 0x80;
	for (unsigned int i = 2; i < 8; i++)
		out[i] = 0;
}

static void check_rect(const struct backend *b, const void *pixels,
		       const struct drm_rect *r, unsigned int offset,
		       bool final)
{
	size_t n = encoded_bytes(r);
	u8 *expected = malloc(n + 64), *actual = malloc(n + 64);

	assert(expected && actual);
	memset(expected, 0xa5, n + 64);
	memset(actual, 0xa5, n + 64);
	oracle(expected + offset, pixels, r, !b->copy_control, b->uyvy, final);
	assert(!b->run(actual + offset, pixels, MS912X_MAX_WIDTH, HEIGHT, r,
		       final));
	if (memcmp(expected, actual, n + 64)) {
		fprintf(stderr, "FAIL %s %dx%d at %d,%d offset=%u\n", b->name,
			r->x2 - r->x1, r->y2 - r->y1, r->x1, r->y1, offset);
		abort();
	}
	free(expected);
	free(actual);
}

static void correctness(void)
{
	const unsigned int widths[] = { 2,   4,	  6,   8,   10,	  14,  16,  18,
					30,  32,  34,  62,  64,	  66,  126, 128,
					130, 638, 640, 642, 1280, 1920 };
	u32 *src = malloc((size_t)MS912X_MAX_WIDTH * HEIGHT * sizeof(*src));
	u16 *native = malloc((size_t)MS912X_MAX_WIDTH * HEIGHT * 2);
	unsigned int cases = 0;
	const u8 trailer[8] = { 0xff, 0xc0 };

	assert(src && !memcmp(trailer, ms912x_end_of_buffer, sizeof(trailer)));
	for (unsigned int i = 0; i < MS912X_MAX_WIDTH * HEIGHT; i++)
		src[i] = random32();
	/* Cover every native RGB565 value before random crops and guard-page tails. */
	for (unsigned int y = 0; y < 64; y++)
		for (unsigned int x = 0; x < 1024; x++)
			src[y * MS912X_MAX_WIDTH + x] =
				(random32() & 0xffff0000U) | (y * 1024 + x);
	for (unsigned i = 0; i < MS912X_MAX_WIDTH * HEIGHT; i++)
		native[i] = src[i];
	for (unsigned int k = 0; k < COUNT(backends); k++) {
		const struct backend *b = &backends[k];

		if (!available(b))
			continue;
		check_rect(b, b->uyvy ? (void *)src : (void *)native,
			   &(struct drm_rect){ 0, 0, 1024, 64 }, 0, true);
		for (unsigned int w = 0; w < COUNT(widths); w++) {
			for (unsigned int offset = 0; offset < 32; offset++) {
				unsigned int x = widths[w] < 1918 ? 2 : 0;
				struct drm_rect r = { x, 2, x + widths[w],
						      2 + 1 + offset % 7 };

				check_rect(b,
					   b->uyvy ? (void *)src :
						     (void *)native,
					   &r, offset, offset & 1);
				cases++;
			}
		}
		/* A single row ending exactly at an inaccessible source/destination page. */
		size_t page = sysconf(_SC_PAGESIZE);
		u8 *source = mmap(NULL, 2 * page, PROT_READ | PROT_WRITE,
				  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		u8 *output = mmap(NULL, 2 * page, PROT_READ | PROT_WRITE,
				  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

		assert(source != MAP_FAILED && output != MAP_FAILED);
		assert(!mprotect(source + page, page, PROT_NONE));
		assert(!mprotect(output + page, page, PROT_NONE));
		for (unsigned int width = 2; width <= 130; width += 2) {
			struct drm_rect r = { 0, 0, width, 1 };
			unsigned bytes = b->uyvy ? 4 : 2;
			u32 *row = (u32 *)(source + page - width * bytes);
			u8 *out = output + page - encoded_bytes(&r);
			u8 expected[130 * 2 + 16];

			for (unsigned int x = 0; x < width * bytes / 4; x++)
				row[x] = random32();
			oracle(expected, row, &r, !b->copy_control, b->uyvy,
			       true);
			assert(!b->run(out, row, MS912X_MAX_WIDTH, 1, &r,
				       true));
			assert(!memcmp(out, expected, encoded_bytes(&r)));
			cases++;
		}
		assert(!munmap(source, 2 * page) && !munmap(output, 2 * page));
	}
	free(src);
	free(native);
	printf("PASS RGB565: %u rectangles, all 65536 values/backend, native 16-bit storage, "
	       "unaligned output, tails, independent framing oracle and guard pages\n",
	       cases);
}

static uint64_t now_ns(void)
{
	struct timespec t;
	assert(!clock_gettime(CLOCK_MONOTONIC_RAW, &t));
	return (uint64_t)t.tv_sec * 1000000000ULL + t.tv_nsec;
}

static void pin_cpu(void)
{
	cpu_set_t allowed, chosen;
	assert(!sched_getaffinity(0, sizeof(allowed), &allowed));
	for (unsigned int cpu = 0; cpu < CPU_SETSIZE; cpu++) {
		if (!CPU_ISSET(cpu, &allowed))
			continue;
		CPU_ZERO(&chosen);
		CPU_SET(cpu, &chosen);
		assert(!sched_setaffinity(0, sizeof(chosen), &chosen));
		printf("Pinned CPU: %u\n", cpu);
		return;
	}
	abort();
}

static u32 *sources[SLOTS];
static u8 *outputs[SLOTS];

static uint64_t measure(const struct backend *b, const struct drm_rect *r,
			size_t iterations, unsigned int slots)
{
	uint64_t start = now_ns();
	int error = 0;

	for (size_t i = 0; i < iterations; i++) {
		unsigned int slot = i & (slots - 1);
		error |= b->run(outputs[slot], sources[slot], MS912X_MAX_WIDTH,
				HEIGHT, r, true);
	}
	uint64_t elapsed = now_ns() - start;
	assert(!error);
	checksum += outputs[0][8];
	return elapsed;
}

static int compare_u64(const void *a, const void *b)
{
	uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
	return (x > y) - (x < y);
}

static void benchmark(const char *csv_path)
{
	const struct {
		const char *name;
		struct drm_rect rect;
	} cases[] = {
		{ "1920x1080", { 0, 0, 1920, 1080 } },
		{ "1280x720", { 0, 0, 1280, 720 } },
		{ "640x128", { 2, 2, 642, 130 } },
		{ "64x64", { 2, 2, 66, 66 } },
		{ "32x32", { 2, 2, 34, 34 } },
		{ "8x8", { 2, 2, 10, 10 } },
		{ "6x64", { 2, 2, 8, 66 } },
		{ "1920x1", { 0, 0, 1920, 1 } },
		{ "16x1", { 2, 2, 18, 3 } },
	};
	FILE *csv = fopen(csv_path, "w");
	const uint64_t target_ns = 30000000;

	assert(csv);
	fprintf(csv,
		"cache,case,backend,sample,iterations,pixels_per_call,duration_ns\n");
	for (unsigned int slot = 0; slot < SLOTS; slot++) {
		assert(!posix_memalign((void **)&sources[slot], 64,
				       (size_t)MS912X_MAX_WIDTH * HEIGHT * 4));
		assert(!posix_memalign((void **)&outputs[slot], 64,
				       (size_t)MS912X_MAX_WIDTH * HEIGHT * 2 +
					       64));
		for (unsigned int x = 0; x < MS912X_MAX_WIDTH * HEIGHT; x++)
			sources[slot][x] = random32();
		memset(outputs[slot], 0,
		       (size_t)MS912X_MAX_WIDTH * HEIGHT * 2 + 64);
	}
	for (unsigned int cache = 0; cache < 2; cache++) {
		unsigned int slots = cache ? SLOTS : 1;

		for (unsigned int c = 0; c < (cache ? 2 : COUNT(cases)); c++) {
			const struct drm_rect *r = &cases[c].rect;
			size_t iterations[COUNT(backends)] = { 0 };
			uint64_t durations[COUNT(backends)][SAMPLES] = { { 0 } };
			unsigned int pixels = (r->x2 - r->x1) * (r->y2 - r->y1);

			for (unsigned int b = 0; b < COUNT(backends); b++) {
				if (!available(&backends[b]))
					continue;
				size_t n = 1;
				uint64_t elapsed;

				do {
					elapsed = measure(&backends[b], r, n,
							  slots);
					n *= 2;
				} while (elapsed < 5000000);
				n /= 2;
				iterations[b] = (size_t)((double)n * target_ns /
							 elapsed) +
						1;
			}
			for (unsigned int s = 0; s < SAMPLES; s++) {
				/* Rotate backend order so drift does not always favor the same ISA. */
				for (unsigned int offset = 0;
				     offset < COUNT(backends); offset++) {
					unsigned int b =
						(s + offset) % COUNT(backends);
					if (!iterations[b])
						continue;
					durations[b][s] =
						measure(&backends[b], r,
							iterations[b], slots);
					fprintf(csv,
						"%s,%s,%s,%u,%zu,%u,%llu\n",
						cache ? "rotating-8" :
							"reused-1",
						cases[c].name, backends[b].name,
						s, iterations[b], pixels,
						(unsigned long long)
							durations[b][s]);
				}
			}
			for (unsigned int b = 0; b < COUNT(backends); b++) {
				if (!iterations[b])
					continue;
				qsort(durations[b], SAMPLES, sizeof(uint64_t),
				      compare_u64);
				double ns = (double)durations[b][SAMPLES / 2] /
					    iterations[b];
				printf("%-10s %-10s %-11s %10.3f us %10.1f Mpixel/s\n",
				       cache ? "rotating-8" : "reused-1",
				       cases[c].name, backends[b].name,
				       ns / 1000, pixels * 1000.0 / ns);
			}
			fflush(csv);
			fflush(stdout);
		}
	}
	assert(!fclose(csv));
	for (unsigned int slot = 0; slot < SLOTS; slot++) {
		free(sources[slot]);
		free(outputs[slot]);
	}
}

void pipeline_benchmark(const char *path);
void pipeline_checks(void);

int main(int argc, char **argv)
{
	bool bench = false;
	const char *csv_path = "samples.csv";
	struct utsname system;

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--bench"))
			bench = true;
		else if (!strcmp(argv[i], "--csv") && i + 1 < argc)
			csv_path = argv[++i];
		else {
			fprintf(stderr, "Unknown argument: %s\n", argv[i]);
			return 2;
		}
	}
	assert(!uname(&system));
	printf("Native target: %s %s\n", system.machine, system.release);
	correctness();
	pipeline_checks();
	if (bench) {
		pin_cpu();
		benchmark(csv_path);
		char path[4096];
		snprintf(path, sizeof(path), "%s.pipeline.csv", csv_path);
		pipeline_benchmark(path);
	}
	return 0;
}
