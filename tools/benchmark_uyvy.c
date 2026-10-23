/* SPDX-License-Identifier: GPL-2.0-only */
/* CPU microbenchmark: capture, scan/baseline, compaction, conversion and packing. */
#include "ms912x_batch.h"
#include "ms912x_scan_methods.h"
#include "ms912x_scan_rects.h"
#include "scan-rows.h"
#include <inttypes.h>
#include <sched.h>
#include <time.h>

extern struct drm_rect *ms912x_rect_vector_grow(struct drm_rect *, unsigned *, unsigned, unsigned);
static volatile u64 sink;
static u64 nanoseconds(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC_RAW, &t);
	return (u64)t.tv_sec * 1000000000 + t.tv_nsec;
}
static int compare_time(const void *a, const void *b)
{
	return (*(const u64 *)a > *(const u64 *)b) - (*(const u64 *)a < *(const u64 *)b);
}
struct benchmark {
	u32 *source, *snapshot, *rgb_old, *rgb_next, *stage;
	u32 *yuv_old, *yuv_baseline, *candidate;
	u8 *packet;
	struct drm_rect *rects;
	struct ms912x_scan_rect_index *scan_index;
	struct ms912x_compact_index *compact_index;
	unsigned capacity, count, stride;
	u64 bytes;
};
enum {
	WIDTH = 1920,
	HEIGHT = 1080,
	RGB_BYTES = WIDTH * HEIGHT * 4,
	YUV_BYTES = WIDTH * HEIGHT * 2,
	SAMPLES = 7,
	REPEATS = 16
};

static void *allocate(size_t size)
{
	void *p;
	assert(!posix_memalign(&p, 64, size));
	memset(p, 0, size);
	return p;
}
static void patch(struct benchmark *b, unsigned x, unsigned y, unsigned w, unsigned h)
{
	for (unsigned row = y; row < min(y + h, HEIGHT); row++)
		for (unsigned column = x; column < min(x + w, WIDTH); column++)
			b->source[row * WIDTH + column] ^= 0x00808080;
}
static void prepare(struct benchmark *b, unsigned pattern)
{
	u32 state = 0x912c5eed;
	for (unsigned i = 0; i < WIDTH * HEIGHT; i++) {
		state ^= state << 13;
		state ^= state >> 17;
		state ^= state << 5;
		b->rgb_old[i] = state & 0xffffff;
	}
	memcpy(b->source, b->rgb_old, RGB_BYTES);
	if (pattern == 1)
		patch(b, 192, 200, 64, 64);
	if (pattern == 2)
		patch(b, 0, 0, WIDTH, HEIGHT / 10);
	if (pattern == 3)
		patch(b, 0, 0, WIDTH / 2, HEIGHT / 2);
	if (pattern == 4)
		patch(b, 0, 0, WIDTH, HEIGHT);
	if (pattern == 5)
		for (unsigned y = 0; y < HEIGHT; y += 32)
			for (unsigned x = 0; x < WIDTH; x += 64)
				patch(b, x, y, 4, 4);
	struct drm_rect full = DRM_RECT_INIT(0, 0, WIDTH, HEIGHT);
	assert(!ms912x_convert_to_uyvy(b->yuv_old, b->rgb_old, &full));
}
static void scan(struct benchmark *b, bool uyvy)
{
	unsigned words = uyvy ? WIDTH / 2 : WIDTH;
	struct ms912x_scan_work work = {
	    .index = b->scan_index,
	    .frame = uyvy ? b->candidate : b->snapshot,
	    .baseline = uyvy ? b->yuv_baseline : b->rgb_old,
	    .next_snapshot = uyvy ? b->yuv_baseline : b->rgb_next,
	    .rects = &b->rects,
	    .rect_capacity = &b->capacity,
	    .rect_count = &b->count,
	    .width = WIDTH,
	    .height = HEIGHT,
	    .stride = b->stride,
	    .word_width = words,
	    .row_words = words,
	    .pixel_shift = uyvy,
	    .block_pixels = uyvy ? MS912X_SCAN_UYVY_RUN_WORDS : MS912X_SCAN_MASKED_BLOCK_PIXELS,
	    .pixel_mask = uyvy ? ms912x_scan_uyvy_tolerance_mask(3) : ms912x_scan_tolerance_mask(3),
	    .selective_baseline = true,
	};
	b->count = 0;
	ms912x_scan_rect_index_reset(b->scan_index, HEIGHT);
	if (!uyvy && b->stride != 1)
		memcpy(b->rgb_next, b->rgb_old, RGB_BYTES);
	for (unsigned row = 0; row < HEIGHT;) {
		struct ms912x_scan_row_result results[16];
		unsigned n = min(16U, DIV_ROUND_UP(HEIGHT - row, b->stride));
		ms912x_scan_rows_avx_relaxed_masked(
		    work.baseline + row * words, work.frame + row * words,
		    uyvy ? NULL : work.next_snapshot + row * words, words, b->stride * words, n,
		    work.pixel_mask, results);
		for (unsigned r = 0; r < n; r++)
			ms912x_damage_scan_process_row(&work, row + r * b->stride, &results[r]);
		row += n * b->stride;
	}
	ms912x_damage_scan_extend_bottom(&work);
}
static void pack(struct benchmark *b, bool uyvy)
{
	for (unsigned i = 0; i < b->count; i++) {
		b->rects[i].x1 &= ~3;
		b->rects[i].x2 = min(WIDTH, (b->rects[i].x2 + 3) & ~3);
	}
	ms912x_rects_compact_exact(b->rects, &b->count);
	ms912x_rects_compact_indexed(b->rects, &b->count, YUV_BYTES + 16,
				     uyvy ? b->compact_index : NULL);
	b->bytes = ms912x_rects_payload_bytes(b->rects, b->count);
	if (uyvy) {
		if (b->count)
			assert(!ms912x_pack_uyvy_batch(b->packet, b->candidate, WIDTH, HEIGHT,
						       b->rects, b->count));
	} else {
		size_t offset = 0;
		for (unsigned i = 0; i < b->count; i++) {
			const struct drm_rect *r = &b->rects[i];
			for (int y = r->y1; y < r->y2; y++)
				memcpy(b->stage + y * WIDTH + r->x1,
				       b->snapshot + y * WIDTH + r->x1, drm_rect_width(r) * 4);
			/* Original converter wrote directly into the packed payload. */
			ms912x_convert_xrgb8888_to_uyvy_avx2(b->packet + offset + 8, b->stage, r);
			offset += (size_t)drm_rect_width(r) * drm_rect_height(r) * 2 + 16;
		}
	}
	sink += b->bytes + b->count;
	if (b->bytes)
		sink += b->packet[8];
}
static u64 run(struct benchmark *b, bool uyvy)
{
	/* Restore fixed transitions outside the timer; stream correctness is tested separately. */
	memcpy(b->yuv_baseline, b->yuv_old, YUV_BYTES);
	u64 start = nanoseconds();
	memcpy(b->snapshot, b->source, RGB_BYTES);
	if (uyvy) {
		struct drm_rect full = DRM_RECT_INIT(0, 0, WIDTH, HEIGHT);
		assert(!ms912x_convert_to_uyvy(b->candidate, b->snapshot, &full));
	}
	scan(b, uyvy);
	pack(b, uyvy);
	return nanoseconds() - start;
}
static void compactor_benchmark(struct benchmark *b)
{
	struct drm_rect original[4097], rects[4097];
	const char *names[] = {"fragments256", "replay32", "sparse1020"};
	for (unsigned pattern = 0; pattern < 3; pattern++) {
		unsigned count = 0;
		if (!pattern)
			for (unsigned y = 0; y < 16; y++)
				for (unsigned x = 0; x < 16; x++)
					original[count++] = DRM_RECT_INIT(x * 64, y * 16, 16, 16);
		if (pattern == 1)
			for (unsigned y = 0; y < 2; y++)
				for (unsigned x = 0; x < 16; x++)
					original[count++] =
					    DRM_RECT_INIT(x * 64, y * 256, 16, 1024 - 256);
		if (pattern == 2)
			for (unsigned y = 0; y < 34; y++)
				for (unsigned x = 0; x < 30; x++)
					original[count++] = DRM_RECT_INIT(x * 64, y * 32, 4, 4);
		for (unsigned indexed = 0; indexed < 2; indexed++) {
			u64 times[SAMPLES], bytes = 0;
			unsigned output = 0;
			for (unsigned sample = 0; sample < SAMPLES; sample++) {
				u64 total = 0;
				for (unsigned rep = 0; rep < 256; rep++) {
					memcpy(rects, original, count * sizeof(*rects));
					output = count;
					u64 start = nanoseconds();
					ms912x_rects_compact_indexed(rects, &output, YUV_BYTES + 16,
								     indexed ? b->compact_index
									     : NULL);
					total += nanoseconds() - start;
					bytes = ms912x_rects_payload_bytes(rects, output);
					sink += output;
				}
				times[sample] = total / 256;
			}
			qsort(times, SAMPLES, sizeof(*times), compare_time);
			printf("compactor,%s,0,%s,%.3f,%u,%" PRIu64 "\n", names[pattern],
			       indexed ? "indexed" : "local8", times[SAMPLES / 2] / 1000., output,
			       bytes);
		}
	}
}
int ms912x_benchmark_uyvy(void)
{
	if (!__builtin_cpu_supports("avx2")) {
		fputs("The comparison benchmark requires AVX2.\n", stderr);
		return 1;
	}
	cpu_set_t cpus;
	assert(!sched_getaffinity(0, sizeof(cpus), &cpus));
	for (unsigned cpu = 0; cpu < CPU_SETSIZE; cpu++)
		if (CPU_ISSET(cpu, &cpus)) {
			CPU_ZERO(&cpus);
			CPU_SET(cpu, &cpus);
			assert(!sched_setaffinity(0, sizeof(cpus), &cpus));
			fprintf(stderr,
				"Pinned CPU %u; AVX scan, AVX2 conversion; 1920x1080; tolerance 3; "
				"median of %u x %u transitions\n",
				cpu, SAMPLES, REPEATS);
			break;
		}
	struct benchmark b = {.capacity = 2048};
	b.source = allocate(RGB_BYTES);
	b.snapshot = allocate(RGB_BYTES);
	b.rgb_old = allocate(RGB_BYTES);
	b.rgb_next = allocate(RGB_BYTES);
	b.stage = allocate(RGB_BYTES);
	b.yuv_old = allocate(YUV_BYTES);
	b.yuv_baseline = allocate(YUV_BYTES);
	b.candidate = allocate(YUV_BYTES);
	b.packet = allocate(YUV_BYTES + 65536);
	b.rects = allocate(b.capacity * sizeof(*b.rects));
	b.scan_index = ms912x_scan_rect_index_alloc();
	b.compact_index = allocate(sizeof(*b.compact_index));
	assert(b.scan_index);
	ms912x_parameters.fast_chroma = MS912X_FAST_CHROMA_AUTO;
	puts("kind,pattern,stride,route,median_us,rectangles,payload_bytes");
	const char *names[] = {"static", "patch64", "window10", "window25", "full", "sparsegrid"};
	for (b.stride = 1; b.stride <= 2; b.stride++)
		for (unsigned pattern = 0; pattern < 6; pattern++) {
			prepare(&b, pattern);
			u64 samples[2][SAMPLES], bytes[2];
			unsigned rects[2];
			for (unsigned warm = 0; warm < 4; warm++) {
				run(&b, false);
				run(&b, true);
			}
			for (unsigned sample = 0; sample < SAMPLES; sample++)
				for (unsigned turn = 0; turn < 2; turn++) {
					unsigned route = (turn + sample) & 1;
					u64 total = 0;
					for (unsigned rep = 0; rep < REPEATS; rep++)
						total += run(&b, route);
					samples[route][sample] = total / REPEATS;
					bytes[route] = b.bytes;
					rects[route] = b.count;
				}
			for (unsigned route = 0; route < 2; route++) {
				qsort(samples[route], SAMPLES, sizeof(u64), compare_time);
				printf("pipeline,%s,%u,%s,%.3f,%u,%" PRIu64 "\n", names[pattern],
				       b.stride, route ? "uyvy-first" : "rgb-first",
				       samples[route][SAMPLES / 2] / 1000., rects[route],
				       bytes[route]);
			}
		}
	compactor_benchmark(&b);
	free(b.source);
	free(b.snapshot);
	free(b.rgb_old);
	free(b.rgb_next);
	free(b.stage);
	free(b.yuv_old);
	free(b.yuv_baseline);
	free(b.candidate);
	free(b.packet);
	free(b.rects);
	ms912x_scan_rect_index_free(b.scan_index);
	free(b.compact_index);
	return 0;
}
