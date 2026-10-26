/* SPDX-License-Identifier: GPL-2.0-only */
#include <sys/mman.h>
#include <unistd.h>

/* Validate the real shared dispatcher at SIMD tails and wide reduction bounds. */
static void backend_bounds(void)
{
    const unsigned widths[] = {32767, 32768, 32769, 65535};
    for (unsigned wi = 0; wi < sizeof(widths)/sizeof(widths[0]); wi++) {
        unsigned width = widths[wi], last_block = (width - 1) / 32 * 32;
        u32 *old = calloc(width, sizeof(*old)), *newest = calloc(width, sizeof(*newest));
        u32 *storage = calloc(width + 2, sizeof(*storage)), *actual = storage + 1;
        assert(old && newest && storage);
        for (unsigned lane = 0; lane < 4; lane++)
        for (unsigned tolerance = 0; tolerance <= 3; tolerance += 3) {
            memset(newest, 0, width * sizeof(*newest));
            newest[0] = newest[width - 1] = 0x40U << (lane * 8);
            newest[width / 2] = 1U << (lane * 8);
            u32 mask = ms912x_scan_uyvy_tolerance_mask(tolerance);
            for (int backend = 0; backend <= MS912X_SCAN_BACKEND_COUNT; backend++) {
                struct ms912x_scan_row_result result;
                unsigned changed = 0, first = width, last = 0;
                storage[0] = storage[width + 1] = 0xdeadbeef;
                memset(actual, 0, width * sizeof(*actual));
                ms912x_core_scan_rows(old, newest, actual, width, width, 1, mask, &result, backend);
                for (unsigned x = 0; x < width; x++) {
                    bool hit = x < 32 || x >= last_block ||
                        ((!tolerance || backend == MS912X_SCAN_MEMCMP) && x / 32 == (width / 2) / 32);
                    if (hit) { changed++; if (first == width) first = x; last = x; }
                    assert(actual[x] == (hit || backend == MS912X_SCAN_MEMCMP ? newest[x] : old[x]));
                }
                assert(result.changed_pixels == changed && result.min_x == first && result.max_x == last);
                assert(!result.equal_row && storage[0] == 0xdeadbeef && storage[width + 1] == 0xdeadbeef);
            }
        }
        free(storage); free(newest); free(old);
    }
    size_t page = (size_t)sysconf(_SC_PAGESIZE);
    u8 *maps[3];
    for (unsigned i = 0; i < 3; i++) {
        maps[i] = mmap(NULL, page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        assert(maps[i] != MAP_FAILED && !mprotect(maps[i] + page, page, PROT_NONE));
    }
    for (unsigned width = 1; width <= 129; width++) {
        u32 *old = (u32 *)(maps[0] + page - width * 4);
        u32 *newest = (u32 *)(maps[1] + page - width * 4);
        u32 *actual = (u32 *)(maps[2] + page - width * 4);
        memset(old, 0, width * 4); memset(newest, 0x40, width * 4);
        for (int backend = 0; backend <= MS912X_SCAN_BACKEND_COUNT; backend++) {
            struct ms912x_scan_row_result result;
            ms912x_core_scan_rows(old, newest, actual, width, width, 1, ~0U, &result, backend);
            assert(result.changed_pixels == width && !memcmp(actual, newest, width * 4));
            ms912x_core_scan_rows(old, newest, NULL, width, width, 1, ~0U, &result, backend);
            assert(result.changed_pixels == width);
        }
    }
    for (unsigned i = 0; i < 3; i++) assert(!munmap(maps[i], page * 2));
}

/* Cross the 16-row kernel FPU boundary with padded/sampled rows and tails. */
static void backend_batches(void)
{
    backend_bounds();
    enum { ROWS = 35, WIDTH = 67, STRIDE = 79 };
    uint32_t old[ROWS * STRIDE], newest[ROWS * STRIDE];
    uint32_t expected[ROWS * STRIDE], actual[ROWS * STRIDE];
    struct ms912x_scan_row_result reference[ROWS], results[ROWS];
    for (unsigned int i = 0; i < ROWS * STRIDE; i++) {
        old[i] = 0x80808080U;
        newest[i] = old[i] ^ (i % 71 == 0 ? 0x01000000U :
                             i % 53 == 0 ? 0x40000000U :
                             i % 47 == 0 ? 0x80000000U : i % 19 == 0 ? 0x08040201U : 0);
    }
    for (int backend = 0; backend <= MS912X_SCAN_BACKEND_COUNT; backend++) {
        for (unsigned int tolerance = 0; tolerance <= 127; tolerance = tolerance * 2 + 1) {
            uint32_t mask = ms912x_scan_uyvy_tolerance_mask(tolerance);
            memcpy(expected, old, sizeof(old));
            memcpy(actual, old, sizeof(old));
            for (unsigned int y = 0; y < ROWS; y++) {
                if (backend == MS912X_SCAN_MEMCMP) {
                    ms912x_scan_row_memcmp(old + y * STRIDE, newest + y * STRIDE,
                                          WIDTH, &reference[y]);
                    memcpy(expected + y * STRIDE, newest + y * STRIDE, WIDTH * sizeof(uint32_t));
                } else {
                    ms912x_scan_row_scalar_masked(old + y * STRIDE, newest + y * STRIDE,
                                                  expected + y * STRIDE, WIDTH, mask, &reference[y]);
                }
            }
            /* Also exercise unavailable/invalid backend fallback. */
            ms912x_core_scan_rows(old, newest, actual, WIDTH, STRIDE, ROWS, mask, results, backend);
            assert(!memcmp(results, reference, sizeof(results)));
            assert(!memcmp(actual, expected, sizeof(actual)));
            ms912x_core_scan_rows(old, newest, NULL, WIDTH, STRIDE, ROWS, mask, results, backend);
            assert(!memcmp(results, reference, sizeof(results)));
        }
    }
}
