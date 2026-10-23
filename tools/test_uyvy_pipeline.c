/* SPDX-License-Identifier: GPL-2.0-only */
#include "ms912x_batch.h"
#include "ms912x_color.h"
#include "ms912x_scan_methods.h"
#include "ms912x_scan_rects.h"
#include "scan-rows.h"
#include "shared/ms912x_rgbx_convert.h"

struct ms912x_device { enum ms912x_damage_scan_method damage_scan_backend; };
#include "scan-backends.h"

bool bench_simd_allowed = true, bench_neon_present = false;
unsigned int bench_cpu_features;
struct ms912x_parameters_fixture ms912x_parameters;
static unsigned int checks;
static u32 random_state = 0x912c5eedU;
static u32 random_word(void)
{
	random_state ^= random_state << 13;
	random_state ^= random_state >> 17;
	random_state ^= random_state << 5;
	return random_state;
}
struct drm_rect *ms912x_rect_vector_grow(struct drm_rect *p, unsigned *capacity, unsigned required,
					 unsigned limit)
{
	if (required <= *capacity)
		return p;
	unsigned grown = min(limit, max(required, *capacity * 2));
	if (required > limit)
		return NULL;
	p = realloc(p, grown * sizeof(*p));
	assert(p);
	*capacity = grown;
	return p;
}
static void scan_row_cases(void);

#ifdef CONFIG_X86_64
static bool force_sse2;
int ms912x_benchmark_uyvy(void);

static bool available(unsigned backend)
{
	switch (backend) {
	case MS912X_FAST_CHROMA_AVX2: return __builtin_cpu_supports("avx2");
	case MS912X_FAST_CHROMA_SSSE3: return __builtin_cpu_supports("ssse3");
	case MS912X_FAST_CHROMA_NEON: return false;
	default: return true;
	}
}

static void scan_row(const u32 *baseline, const u32 *candidate, unsigned width,
		     unsigned stride, u32 mask, struct ms912x_scan_row_result *result)
{
	if (__builtin_cpu_supports("avx") && !force_sse2)
		ms912x_scan_rows_avx_relaxed_masked(baseline, candidate, NULL, width,
			stride, 1, mask, result);
	else
		ms912x_scan_rows_sse2_relaxed_masked(baseline, candidate, NULL, width,
			stride, 1, mask, result);
}

static void init_cpu_features(void)
{
	bench_cpu_features = (1U << 0) | (__builtin_cpu_supports("ssse3") ? 1U << 1 : 0) |
		(__builtin_cpu_supports("avx") ? 1U << 3 : 0) |
		(__builtin_cpu_supports("avx2") ? 1U << 4 : 0);
}

static int maybe_run_benchmark(int argc, char **argv)
{
	return argc == 2 && !strcmp(argv[1], "--benchmark") ? ms912x_benchmark_uyvy() : -1;
}

static void other_scan_row_cases(void)
{
	if (__builtin_cpu_supports("avx")) {
		force_sse2 = true;
		scan_row_cases();
	}
}
#else
static bool available(unsigned backend)
{
	return backend == MS912X_FAST_CHROMA_AUTO || backend == MS912X_FAST_CHROMA_SCALAR ||
		backend == MS912X_FAST_CHROMA_NEON;
}

static void scan_row(const u32 *baseline, const u32 *candidate, unsigned width,
		     unsigned stride, u32 mask, struct ms912x_scan_row_result *result)
{
	ms912x_scan_rows_neon_relaxed_masked(baseline, candidate, NULL, width,
		stride, 1, mask, result);
}

static void init_cpu_features(void)
{
	bench_neon_present = true;
}

static int maybe_run_benchmark(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	return -1;
}

static void other_scan_row_cases(void)
{
}
#endif

static void backend_policy(void)
{
	struct ms912x_device device = {};
	assert(MS912X_SCAN_MASKED_BLOCK_BYTES == 128);
	for (unsigned method = MS912X_DAMAGE_SCAN_SCALAR;
	     method <= MS912X_DAMAGE_SCAN_METHOD_AUTO; method++)
		for (unsigned tolerance = 0; tolerance <= 3; tolerance += 3) {
			struct ms912x_scan_config config = {.method = method, .tolerance = tolerance,
				.pixel_mask = ms912x_scan_uyvy_tolerance_mask(tolerance)};
			ms912x_damage_scan_select_incremental_backend_locked(&device, &config);
			assert(config.use_memcmp == (method == MS912X_DAMAGE_SCAN_MEMCMP));
			assert(config.pixel_mask == (config.use_memcmp ? ~0U :
				ms912x_scan_uyvy_tolerance_mask(tolerance)));
			assert(config.use_relaxed == !config.use_memcmp);
			assert(config.run_block_pixels == (config.use_memcmp ? 32 : 8));
			if (method == MS912X_DAMAGE_SCAN_METHOD_AUTO)
				assert(config.method == ms912x_damage_scan_select_auto());
			checks++;
		}
	/* Loss of SIMD context preserves masked tolerance through scalar fallback. */
	bench_simd_allowed = false;
	assert(ms912x_damage_scan_select_auto() == MS912X_DAMAGE_SCAN_SCALAR);
	bench_simd_allowed = true;
}

static void conversion_and_rgbx(void)
{
	enum { ROWS = 24, RAW = MS912X_MAX_WIDTH * ROWS * 4, YUV = MS912X_MAX_WIDTH * ROWS * 2 };
	u8 *rgb = malloc(RAW), *rgbx = malloc(RAW), *out = malloc(YUV + 64),
	   *expected = malloc(YUV + 64);
	const unsigned widths[] = {2, 4, 6, 8, 14, 32, 66, 1366, 1920}, heights[] = {1, 3, 17};
	assert(rgb && rgbx && out && expected);
	for (unsigned i = 0; i < RAW; i++)
		rgb[i] = random_word();
	for (unsigned i = 0; i < RAW; i += 4) {
		rgbx[i] = rgb[i + 2];
		rgbx[i + 1] = rgb[i + 1];
		rgbx[i + 2] = rgb[i];
		rgbx[i + 3] = rgb[i + 3] ^ 255;
	}
	for (unsigned wi = 0; wi < ARRAY_SIZE(widths); wi++)
		for (unsigned hi = 0; hi < ARRAY_SIZE(heights); hi++)
			for (unsigned cropped = 0; cropped < 2; cropped++) {
				unsigned width = widths[wi], height = heights[hi],
					 x = cropped && width < 1920 ? 2 : 0, y = cropped ? 2 : 0;
				struct drm_rect rect = DRM_RECT_INIT(x, y, width, height);
				struct ms912x_rgbx_view view = {.data = rgb,
								.bytes = RAW,
								.row_stride = 1920 * 4,
								.pixel_stride = 4,
								.crop_x = x,
								.crop_y = y,
								.width = width,
								.height = height,
								.order = MS912X_BGRX};
				memset(expected, 0xa5, YUV + 64);
				size_t offset = (size_t)(y * 1920 + x) * 2;
				assert(!ms912x_rgbx_to_uyvy(expected + 32 + offset, YUV - offset,
							    1920 * 2, &view, MS912X_COLOR_SCALAR));
				for (unsigned backend = 0; backend <= MS912X_FAST_CHROMA_SSSE3;
				     backend++)
					if (available(backend)) {
						ms912x_parameters.fast_chroma = backend;
						memset(out, 0xa5, YUV + 64);
						assert(!ms912x_convert_to_uyvy(
						    (u32 *)(out + 32), (const __le32 *)rgb, &rect));
						assert(!memcmp(out, expected, YUV + 64));
						checks++;
					}
				view.data = rgbx;
				view.order = MS912X_RGBX;
				memset(out, 0xa5, YUV + 64);
				assert(!ms912x_rgbx_to_uyvy(out + 32 + offset, YUV - offset,
							    1920 * 2, &view, MS912X_COLOR_AUTO));
				assert(!memcmp(out, expected, YUV + 64));
				checks++;
			}
	struct drm_rect invalid[] = {DRM_RECT_INIT(-2, 0, 2, 1),   DRM_RECT_INIT(1, 0, 2, 1),
				     DRM_RECT_INIT(0, 0, 3, 1),	   DRM_RECT_INIT(1920, 0, 2, 1),
				     DRM_RECT_INIT(0, 1200, 2, 1), DRM_RECT_INIT(0, 0, 0, 1)};
	for (unsigned i = 0; i < ARRAY_SIZE(invalid); i++) {
		memset(out, 0xa5, YUV + 64);
		assert(ms912x_convert_to_uyvy((u32 *)(out + 32), (const __le32 *)rgb,
					      &invalid[i]) == -EINVAL);
		for (unsigned j = 0; j < YUV + 64; j++)
			assert(out[j] == 0xa5);
		checks++;
	}
	free(rgb);
	free(rgbx);
	free(out);
	free(expected);
}
static void packet_bytes(void)
{
	enum { HEIGHT = 9 };
	u32 *frame = malloc(1920 * HEIGHT * 2);
	u8 packet[512];
	assert(frame);
	for (unsigned i = 0; i < 1920 * HEIGHT / 2; i++)
		frame[i] = random_word();
	struct drm_rect rects[] = {DRM_RECT_INIT(2, 1, 6, 3), DRM_RECT_INIT(28, 7, 4, 2)};
	memset(packet, 0xa5, sizeof(packet));
	assert(!ms912x_pack_uyvy_batch(packet, frame, 64, HEIGHT, rects, 2));
	size_t offset = 0;
	for (unsigned i = 0; i < 2; i++) {
		const struct drm_rect *r = &rects[i];
		unsigned width = drm_rect_width(r);
		unsigned position =
		    (packet[offset + 2] << 16) | (packet[offset + 3] << 8) | packet[offset + 4];
		unsigned dimensions =
		    (packet[offset + 5] << 16) | (packet[offset + 6] << 8) | packet[offset + 7];
		assert(packet[offset] == 255 && packet[offset + 1] == 0);
		assert(position == ((unsigned)r->x1 << 12 | (unsigned)r->y1));
		assert(dimensions == (width << 12 | (unsigned)drm_rect_height(r)));
		offset += 8;
		for (int y = r->y1; y < r->y2; y++) {
			assert(!memcmp(packet + offset, (u8 *)frame + (y * 1920 + r->x1) * 2,
				       width * 2));
			offset += width * 2;
		}
		assert(packet[offset] == 255 && packet[offset + 1] == (i ? 0xc0 : 0x80));
		for (unsigned j = 2; j < 8; j++)
			assert(!packet[offset + j]);
		offset += 8;
	}
	for (unsigned i = offset; i < sizeof(packet); i++)
		assert(packet[i] == 0xa5);
	assert(!ms912x_pack_uyvy_batch(packet, frame, 64, HEIGHT, rects, 1));
	assert(packet[8 + 6 * 3 * 2 + 1] == 0xc0);
	checks++;
	assert(ms912x_pack_uyvy_batch(packet, frame, 64, HEIGHT, NULL, 0) == -EINVAL);
	free(frame);
}
static void tolerance_bytes(void)
{
	const unsigned tolerances[] = {0, 1, 3, 7, 15, 31};
	for (unsigned t = 0; t < ARRAY_SIZE(tolerances); t++)
		for (unsigned lane = 0; lane < 4; lane++)
			for (unsigned a = 0; a < 256; a++)
				for (unsigned b = 0; b < 256; b++) {
					u32 old = a << (lane * 8), new = b << (lane * 8), next = 0,
					    mask = ms912x_scan_uyvy_tolerance_mask(tolerances[t]);
					struct ms912x_scan_row_result r;
					ms912x_scan_row_scalar_masked(&old, &new, &next, 1, mask,
								      &r);
					bool hit = ((a ^ b) & (255U ^ tolerances[t])) != 0;
					assert((r.changed_pixels != 0) == hit &&
					       next == (hit ? new : old));
					checks++;
				}
}
/* NULL destinations must compare all lanes without advancing a suppressed baseline. */
static void comparison_only_lanes(void)
{
	const unsigned widths[] = {1, 15, 31, 32, 33, 63, 64, 65, 960};
	u32 baseline[960] = {}, candidate[960] = {}, retained[960];
	for (unsigned wi = 0; wi < ARRAY_SIZE(widths); wi++)
		for (unsigned lane = 0; lane < 4; lane++)
			for (unsigned tolerance = 0; tolerance <= 3; tolerance += 3) {
				unsigned width = widths[wi];
				struct ms912x_scan_row_result expected, actual;
				memset(candidate, 0, sizeof(candidate));
				candidate[width - 1] = 0x40U << (8 * lane);
				u32 mask = ms912x_scan_uyvy_tolerance_mask(tolerance);
				ms912x_scan_row_scalar_masked(baseline, candidate, retained,
							      width, mask, &expected);
				scan_row(baseline, candidate, width, width, mask, &actual);
				assert(!memcmp(&expected, &actual, sizeof(expected)));
				assert(actual.changed_pixels && !baseline[width - 1]);
				/* Low bits are suppressed, and cannot silently advance the baseline. */
				candidate[width - 1] = 1U << (8 * lane);
				scan_row(baseline, candidate, width, width, mask, &actual);
				assert(actual.equal_row == (tolerance != 0));
				assert(!baseline[width - 1]);
				checks++;
			}
}

/* A coarse SIMD hit must not advance suppressed neighbouring fine spans. */
static void damage_run_granularity(void)
{
	enum { WIDTH = 132, HEIGHT = 3, WORDS = MS912X_MAX_WIDTH / 2 };
	const unsigned positions[] = {0, 7, 8, 15, 16, 23, 24, 31, 32,
				      39, 40, 47, 48, 55, 56, 63, 64, 65};
	u32 *baseline = calloc(WORDS * HEIGHT, sizeof(*baseline));
	u32 *candidate = calloc(WORDS * HEIGHT, sizeof(*candidate));
	struct ms912x_scan_rect_index *index = ms912x_scan_rect_index_alloc();
	unsigned capacity = 8, count;
	struct drm_rect *rects = calloc(capacity, sizeof(*rects));

	assert(baseline && candidate && index && rects);
	for (unsigned p = 0; p <= ARRAY_SIZE(positions); p++)
		for (unsigned lane = 0; lane < 4; lane++)
			for (unsigned tolerance = 0; tolerance <= 3; tolerance += 3) {
				bool split = p == ARRAY_SIZE(positions);
				unsigned word = split ? 0 : positions[p];
				struct ms912x_scan_row_result result;
				struct ms912x_scan_work work = {
					.index = index, .frame = candidate, .baseline = baseline,
					.next_snapshot = baseline, .rects = &rects,
					.rect_capacity = &capacity, .rect_count = &count,
					.width = WIDTH, .height = HEIGHT, .stride = 1,
					.block_pixels = MS912X_SCAN_UYVY_RUN_WORDS,
					.word_width = WIDTH / 2, .row_words = WORDS, .pixel_shift = 1,
					.pixel_mask = ms912x_scan_uyvy_tolerance_mask(tolerance),
					.selective_baseline = true,
				};

				memset(baseline, 0, WORDS * HEIGHT * sizeof(*baseline));
				memset(candidate, 0, WORDS * HEIGHT * sizeof(*candidate));
				for (unsigned x = 0; x < WIDTH / 2; x++)
					candidate[WORDS + x] = tolerance ? 0x01010101U : 0;
				candidate[WORDS + word] |= 0x40U << (lane * 8);
				if (split)
					candidate[WORDS + 24] |= 0x40U << (lane * 8);
				count = 0;
				ms912x_scan_rect_index_reset(index, HEIGHT);
				scan_row(baseline + WORDS, candidate + WORDS, WIDTH / 2,
					 WORDS, work.pixel_mask, &result);
				ms912x_damage_scan_process_row(&work, 1, &result);
				assert(count == (split ? 2U : 1U));
				for (unsigned r = 0; r < count; r++) {
					unsigned x1 = split ? r * 48 : word / 8 * 16;
					struct drm_rect expected = DRM_RECT_INIT(
						x1, 1, min(16U, WIDTH - x1), 1);

					assert(drm_rect_equals(&rects[r], &expected));
				}
				for (unsigned y = 0; y < HEIGHT; y++)
					for (unsigned x = 0; x < WORDS; x++) {
						bool admitted = y == 1 && x < WIDTH / 2 &&
							(x / 8 == word / 8 || (split && x / 8 == 3));

						assert(baseline[y * WORDS + x] ==
						       (admitted ? candidate[y * WORDS + x] : 0));
					}
				checks++;
			}
	free(baseline);
	free(candidate);
	free(rects);
	ms912x_scan_rect_index_free(index);
}

static void scan_and_pack(unsigned stride, unsigned tolerance,
			  enum ms912x_damage_scan_method method)
{
	enum { WIDTH = 128, HEIGHT = 32, WORDS = 1920 / 2, BYTES = WORDS * HEIGHT * 4 };
	u32 *baseline = calloc(1, BYTES), *candidate = calloc(1, BYTES),
	    *display = calloc(1, BYTES);
	u32 *rgb = calloc(1920 * HEIGHT, 4);
	u8 *packet = malloc(WIDTH * HEIGHT * 2 + 4096);
	struct ms912x_scan_rect_index *index = ms912x_scan_rect_index_alloc();
	struct ms912x_device device = {};
	struct ms912x_scan_config config = {
		.method = method, .stride = stride, .tolerance = tolerance,
		.pixel_mask = ms912x_scan_uyvy_tolerance_mask(tolerance),
	};
	unsigned capacity = 2048, count = 0;
	struct drm_rect *rects = calloc(capacity, sizeof(*rects));
	assert(baseline && candidate && display && rgb && packet && index && rects);
	struct drm_rect full = DRM_RECT_INIT(0, 0, WIDTH, HEIGHT);
	ms912x_damage_scan_select_incremental_backend_locked(&device, &config);
	ms912x_parameters.fast_chroma = MS912X_FAST_CHROMA_AUTO;
	assert(!ms912x_convert_to_uyvy(candidate, rgb, &full));
	memcpy(baseline, candidate, BYTES);
	memcpy(display, candidate, BYTES);
	for (unsigned frame = 0; frame < 40; frame++) {
		/* Isolated odd-row changes exercise both sampling margins and the fourth UYVY byte.
		 */
		unsigned x = (frame * 14) % WIDTH, y = (frame * 3) % HEIGHT;
		rgb[y * 1920 + x] ^= 0x00808080U;
		assert(!ms912x_convert_to_uyvy(candidate, rgb, &full));
		for (unsigned phase = 0; phase < stride; phase++) {
			count = 0;
			ms912x_scan_rect_index_reset(index, HEIGHT);
			struct ms912x_scan_work work = {
			    .index = index,
			    .frame = candidate,
			    .baseline = baseline,
			    .next_snapshot = baseline,
			    .rects = &rects,
			    .rect_capacity = &capacity,
			    .rect_count = &count,
			    .width = WIDTH,
			    .height = HEIGHT,
			    .stride = stride,
			    .block_pixels = config.run_block_pixels,
			    .word_width = WIDTH / 2,
			    .row_words = WORDS,
			    .pixel_shift = 1,
			    .pixel_mask = config.pixel_mask,
			    .selective_baseline = config.use_relaxed};
			unsigned rows_scanned = 0;
			ms912x_damage_scan_run_rows(&work, &config, phase, &rows_scanned);
			assert(rows_scanned == DIV_ROUND_UP(HEIGHT - phase, stride));
			ms912x_damage_scan_extend_bottom(&work);
			ms912x_rects_compact(rects, &count, WIDTH * HEIGHT * 2 + 16);
			if (count) {
				assert(!ms912x_pack_uyvy_batch(packet, candidate, WIDTH, HEIGHT,
							       rects, count));
				size_t at = 0;
				for (unsigned i = 0; i < count; i++) {
					struct drm_rect r = rects[i];
					at += 8;
					for (int yy = r.y1; yy < r.y2; yy++) {
						memcpy((u8 *)display + (yy * 1920 + r.x1) * 2,
						       packet + at, drm_rect_width(&r) * 2);
						at += drm_rect_width(&r) * 2;
					}
					at += 8;
				}
			}
		}
		for (unsigned yy = 0; yy < HEIGHT; yy++)
			for (unsigned word = 0; word < WIDTH / 2; word++)
				assert(
				    !((display[yy * WORDS + word] ^ candidate[yy * WORDS + word]) & config.pixel_mask));
		checks++;
	}
	free(baseline);
	free(candidate);
	free(display);
	free(rgb);
	free(packet);
	free(rects);
	ms912x_scan_rect_index_free(index);
}

static void scan_row_cases(void)
{
	comparison_only_lanes();
	damage_run_granularity();
}

static void scan_cases(void)
{
	for (unsigned method = MS912X_DAMAGE_SCAN_SCALAR;
	     method <= MS912X_DAMAGE_SCAN_METHOD_AUTO; method++)
		for (unsigned stride = 1; stride <= 4; stride++)
			for (unsigned t = 0; t <= 3; t += 3)
				scan_and_pack(stride, t, method);
}

int main(int argc, char **argv)
{
	int result;

	init_cpu_features();
	result = maybe_run_benchmark(argc, argv);
	if (result >= 0)
		return result;
	backend_policy();
	conversion_and_rgbx();
	packet_bytes();
	tolerance_bytes();
	scan_row_cases();
	other_scan_row_cases();
	scan_cases();
	printf("PASS UYVY pipeline: %u checks; pitched scalar/SIMD conversion, RGBX/BGRX parity, "
	       "four-byte masks, sampled baselines and mode-3 packets\n",
	       checks);
}
