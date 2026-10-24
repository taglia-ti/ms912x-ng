/* SPDX-License-Identifier: GPL-2.0-only */
/* Cross the 16-row kernel FPU boundary with padded/sampled rows and tails. */
static void backend_batches(void)
{
    enum { ROWS = 35, WIDTH = 67, STRIDE = 79 };
    uint32_t old[ROWS * STRIDE], newest[ROWS * STRIDE];
    uint32_t expected[ROWS * STRIDE], actual[ROWS * STRIDE];
    struct ms912x_scan_row_result reference[ROWS], results[ROWS];
    for (unsigned int i = 0; i < ROWS * STRIDE; i++) {
        old[i] = 0x80808080U;
        newest[i] = old[i] ^ (i % 71 == 0 ? 0x01000000U : i % 19 == 0 ? 0x08040201U : 0);
    }
    for (int backend = 0; backend <= MS912X_SCAN_BACKEND_COUNT; backend++) {
        for (unsigned int tolerance = 0; tolerance <= 31; tolerance = tolerance * 2 + 1) {
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
