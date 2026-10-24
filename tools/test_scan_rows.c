// SPDX-License-Identifier: GPL-2.0-only
/* Exercise production damage construction across both sides of sampled rows. */
#include <assert.h>
#include <stdio.h>

#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#define max3(a, b, c) max(max(a, b), c)
#include "ms912x_scan_methods.h"
#include "ms912x_scan_runs.h"
#include "ms912x_scan_rects.h"

struct ms912x_scan_frame_stats {
    unsigned int rect_add_calls;
    unsigned int rect_overflows;
};
struct ms912x_scan_work {
    struct ms912x_scan_rect_index *index;
    const u32 *frame;
    const u32 *baseline;
    u32 *next_snapshot;
    struct drm_rect **rects;
    unsigned int *rect_capacity;
    unsigned int *rect_count;
    unsigned int width;
    unsigned int height;
    unsigned int stride;
    unsigned int block_pixels;
    unsigned int word_width, row_words, pixel_shift;
    unsigned int tolerance;
    u32 pixel_mask;
    bool selective_baseline;
    u64 changed_pixels;
    struct ms912x_scan_frame_stats *frame_stats;
};
#include "scan-rows.h"

#include "rect-grow.h"

static unsigned int cases;

static void check_rows(unsigned int height, unsigned int stride, unsigned int phase,
                       unsigned int first_hit, unsigned int last_hit, bool relaxed)
{
    const unsigned int width = 64;
    const unsigned int words = width / 2, row_words = MS912X_MAX_WIDTH / 2;
    size_t pixels = (size_t)height * row_words;
    struct ms912x_scan_rect_index *index = ms912x_scan_rect_index_alloc();
    struct ms912x_scan_frame_stats stats = {};
    unsigned int capacity = 1, count = 0, hits = 0;
    struct drm_rect *rects = calloc(capacity, sizeof(*rects));
    u32 *frame = calloc(pixels, sizeof(*frame));
    u32 *allocation = calloc(pixels + 2, sizeof(*allocation));
    u32 *baseline = allocation + 1, *next = baseline;
    bool *expected = calloc((size_t)height * width, sizeof(*expected));
    struct ms912x_scan_work work = {
        .index = index,
        .frame = frame,
        .baseline = baseline,
        .next_snapshot = next,
        .rects = &rects,
        .rect_capacity = &capacity,
        .rect_count = &count,
        .width = width,
        .word_width = words, .row_words = row_words, .pixel_shift = 1,
        .height = height,
        .stride = stride,
        .block_pixels = relaxed ? MS912X_SCAN_UYVY_RUN_WORDS : MS912X_SCAN_MEMCMP_BLOCK_BYTES / sizeof(u32),
        .tolerance = relaxed ? 3 : 0,
        .pixel_mask = ms912x_scan_uyvy_tolerance_mask(relaxed ? 3 : 0),
        .selective_baseline = relaxed,
        .frame_stats = &stats,
    };

    assert(index && rects && baseline && frame && allocation && expected);
    allocation[0] = allocation[pixels + 1] = 0xabcdef01;
    for (unsigned int y = 0; y < height; y++) {
        bool sampled = y >= phase && (y - phase) % stride == 0;

        /* Independent changes on skipped rows must accompany a nearby hit. */
        if (!sampled)
            for (unsigned int x = 0; x < words; x++)
                frame[y * row_words + x] = 0x00112233;
        if (sampled && y >= first_hit && y <= last_hit) {
            hits++;
            for (unsigned int x = 0; x < words; x++) {
                if (!((x * 2 >= 10 && x * 2 < 13) || (x * 2 >= 38 && x * 2 < 41)))
                    continue;
                frame[y * row_words + x] = 0x40f07020;
            }
            for (unsigned int row = 0; row < height; row++)
                if (abs((int)row - (int)y) < (int)stride)
                    for (unsigned int x = 0; x < width; x++)
                        if (!relaxed || x < 16 || (x >= 32 && x < 48))
                            expected[row * width + x] = true;
        }
        if (sampled && relaxed)
            frame[y * row_words + 25] = 0x02020202;
        for (unsigned int x = words; x < row_words; x++)
            frame[y * row_words + x] = 0xdeadbeef;
    }

    ms912x_scan_rect_index_reset(index, height);
    for (unsigned int y = phase; y < height; y += stride) {
        struct ms912x_scan_row_result result;

        if (relaxed)
            ms912x_scan_row_scalar_masked(baseline + y * row_words,
                frame + y * row_words, NULL, words, work.pixel_mask, &result);
        else
            ms912x_scan_row_memcmp(baseline + y * row_words,
                frame + y * row_words, words, &result);
        ms912x_damage_scan_process_row(&work, y, &result);
    }
    /* Consecutive hits still compact before adding overlapping lower margins. */
    assert(count == (hits ? (relaxed ? 2U : 1U) : 0U));
    ms912x_damage_scan_extend_bottom(&work);
    assert(work.changed_pixels == hits * width && !stats.rect_overflows);

    for (unsigned int i = 0; i < count; i++)
        assert(rects[i].x1 >= 0 && rects[i].x2 <= (int)width &&
               rects[i].y1 >= 0 && rects[i].y2 <= (int)height);
    for (unsigned int y = 0; y < height; y++) {
        bool sampled = y >= phase && (y - phase) % stride == 0;

        for (unsigned int x = 0; x < width; x++) {
            bool covered = false;
            u32 wanted = 0;
            size_t offset = y * row_words + x / 2;

            for (unsigned int i = 0; i < count; i++)
                covered |= x >= (unsigned int)rects[i].x1 &&
                           x < (unsigned int)rects[i].x2 &&
                           y >= (unsigned int)rects[i].y1 &&
                           y < (unsigned int)rects[i].y2;
            if (covered != expected[y * width + x]) {
                fprintf(stderr, "coverage: stride=%u phase=%u hits=%u..%u at (%u,%u)\n",
                        stride, phase, first_hit, last_hit, x, y);
                abort();
            }
            if (!relaxed && (sampled || covered))
                wanted = frame[offset];
            if (relaxed && covered)
                wanted = frame[offset];
            assert(next[offset] == wanted);
        }
        for (unsigned int x = words; x < row_words; x++)
            assert(next[y * row_words + x] == 0);
    }
    assert(allocation[0] == 0xabcdef01 && allocation[pixels + 1] == 0xabcdef01);
    free(expected);
    free(allocation);
    free(frame);
    free(rects);
    ms912x_scan_rect_index_free(index);
    cases++;
}

int main(void)
{
    const unsigned int heights[] = {1, 2, 3, 16, 17, 37};

    for (unsigned int h = 0; h < ARRAY_SIZE(heights); h++)
        for (unsigned int stride = 1; stride <= 16; stride++)
            for (unsigned int phase = 0; phase < stride; phase++)
                for (unsigned int relaxed = 0; relaxed < 2; relaxed++) {
                    unsigned int height = heights[h];

                    check_rows(height, stride, phase, height, height, relaxed);
                    for (unsigned int y = phase; y < height; y += stride)
                        check_rows(height, stride, phase, y, y, relaxed);
                    check_rows(height, stride, phase, 0, height - 1, relaxed);
                }
    printf("PASS scan row coverage: %u cases; strides 1..16, all phases, both margins, "
           "clipping, vertical compaction and UYVY MEMCMP/masked snapshots\n", cases);
    return 0;
}
