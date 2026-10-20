// SPDX-License-Identifier: GPL-2.0-only
/* Exercise production damage construction across both sides of sampled rows. */
#include <assert.h>
#include <stdio.h>

#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#define max3(a, b, c) max(max(a, b), c)
#include "ms912x_scan_methods.h"
#include "ms912x_scan_rects.h"

struct ms912x_scan_frame_stats {
    unsigned int rect_add_calls;
    unsigned int rect_overflows;
};
#include "scan-rows.h"

struct drm_rect *ms912x_rect_vector_grow(struct drm_rect *rects, unsigned int *capacity,
                                       unsigned int required, unsigned int limit)
{
    unsigned int grown = min(limit, max(required, *capacity * 2));

    if (required <= *capacity)
        return rects;
    assert(required <= limit);
    rects = realloc(rects, grown * sizeof(*rects));
    assert(rects);
    *capacity = grown;
    return rects;
}

static unsigned int cases;

static void check_rows(unsigned int height, unsigned int stride, unsigned int phase,
                       unsigned int first_hit, unsigned int last_hit, bool relaxed)
{
    const unsigned int width = 64;
    size_t pixels = (size_t)height * MS912X_MAX_WIDTH;
    struct ms912x_scan_rect_index *index = ms912x_scan_rect_index_alloc();
    struct ms912x_scan_frame_stats stats = {};
    unsigned int capacity = 1, count = 0, hits = 0;
    struct drm_rect *rects = calloc(capacity, sizeof(*rects));
    u32 *baseline = calloc(pixels, sizeof(*baseline));
    u32 *frame = calloc(pixels, sizeof(*frame));
    u32 *allocation = calloc(pixels + 2, sizeof(*allocation));
    u32 *next = allocation + 1;
    bool *expected = calloc((size_t)height * width, sizeof(*expected));
    u64 changed_pixels = 0;

    assert(index && rects && baseline && frame && allocation && expected);
    allocation[0] = allocation[pixels + 1] = 0xabcdef01;
    for (unsigned int y = 0; y < height; y++) {
        bool sampled = y >= phase && (y - phase) % stride == 0;

        /* Independent changes on skipped rows must accompany a nearby hit. */
        if (!sampled)
            for (unsigned int x = 0; x < width; x++)
                frame[y * MS912X_MAX_WIDTH + x] = 0x00112233;
        if (sampled && y >= first_hit && y <= last_hit) {
            hits++;
            for (unsigned int x = 0; x < width; x++) {
                if (!((x >= 10 && x < 13) || (x >= 38 && x < 41)))
                    continue;
                frame[y * MS912X_MAX_WIDTH + x] = 0x00f07020;
                for (unsigned int row = 0; row < height; row++)
                    if (abs((int)row - (int)y) < (int)stride)
                        expected[row * width + x] = true;
            }
        }
        if (sampled && relaxed)
            frame[y * MS912X_MAX_WIDTH + 50] = 0x00020202;
        for (unsigned int x = width; x < MS912X_MAX_WIDTH; x++)
            frame[y * MS912X_MAX_WIDTH + x] = 0xdeadbeef;
    }

    ms912x_scan_rect_index_reset(index, height);
    for (unsigned int y = phase; y < height; y += stride) {
        struct ms912x_scan_row_result result;

        if (relaxed)
            ms912x_scan_row_scalar_relaxed(baseline + y * MS912X_MAX_WIDTH,
                frame + y * MS912X_MAX_WIDTH, next + y * MS912X_MAX_WIDTH,
                width, 4, &result);
        else
            ms912x_scan_row_scalar(baseline + y * MS912X_MAX_WIDTH,
                frame + y * MS912X_MAX_WIDTH, width, &result);
        ms912x_damage_scan_process_row(index, frame, next, width, height,
            stride, y, &result, 1, relaxed ? 4 : 0, baseline,
            &rects, &capacity, &count, relaxed, &changed_pixels, &stats);
    }
    /* Consecutive hits still compact before adding overlapping lower margins. */
    assert(count == (hits ? 2U : 0U));
    ms912x_damage_scan_extend_bottom(rects, count, stride, height, frame, next,
                                     relaxed);
    assert(changed_pixels == hits * 6U && !stats.rect_overflows);

    for (unsigned int i = 0; i < count; i++)
        assert(rects[i].x1 >= 0 && rects[i].x2 <= (int)width &&
               rects[i].y1 >= 0 && rects[i].y2 <= (int)height);
    for (unsigned int y = 0; y < height; y++) {
        bool sampled = y >= phase && (y - phase) % stride == 0;

        for (unsigned int x = 0; x < width; x++) {
            bool covered = false;
            u32 wanted = 0;
            size_t offset = y * MS912X_MAX_WIDTH + x;

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
            if (relaxed && sampled && x != 50)
                wanted = frame[offset];
            assert(next[offset] == wanted);
        }
        for (unsigned int x = width; x < MS912X_MAX_WIDTH; x++)
            assert(next[y * MS912X_MAX_WIDTH + x] == 0);
    }
    assert(allocation[0] == 0xabcdef01 && allocation[pixels + 1] == 0xabcdef01);
    free(expected);
    free(allocation);
    free(frame);
    free(baseline);
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
           "clipping, vertical compaction and exact/relaxed snapshots\n", cases);
    return 0;
}
