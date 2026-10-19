/* SPDX-License-Identifier: GPL-2.0-only */
/* Included after the actual driver selectors by run.py. */
void dispatch_tests(void) {
    unsigned checks = 0;
    const char *names[] = {"scalar", "auto", "avx2", "neon", "sse2", "ssse3"};

    for (unsigned flags = 1; flags < 32; flags += 2) {
        bench_cpu_features = flags;
        for (unsigned usable = 0; usable < 2; usable++) {
            bench_simd_allowed = usable;
            for (unsigned mode = 0; mode <= MS912X_FAST_CHROMA_SSSE3; mode++) {
                enum ms912x_fast_chroma_backend expected = MS912X_FAST_CHROMA_SCALAR;

#ifdef CONFIG_X86_64
                bool avx2 = (flags & 24) == 24;
                bool ssse3 = flags & 2;
                if (usable) {
                    if (mode == MS912X_FAST_CHROMA_AUTO)
                        expected = avx2    ? MS912X_FAST_CHROMA_AVX2
                                   : ssse3 ? MS912X_FAST_CHROMA_SSSE3
                                           : MS912X_FAST_CHROMA_SSE2;
                    else if ((mode == MS912X_FAST_CHROMA_AVX2 && avx2) ||
                             (mode == MS912X_FAST_CHROMA_SSSE3 && ssse3) ||
                             mode == MS912X_FAST_CHROMA_SSE2)
                        expected = mode;
                }
#else
                if (usable && (mode == MS912X_FAST_CHROMA_AUTO || mode == MS912X_FAST_CHROMA_NEON))
                    expected = MS912X_FAST_CHROMA_NEON;
#endif
                ms912x_parameters.fast_chroma = mode;
                assert(ms912x_fast_chroma_select() == expected);
                assert(!strcmp(ms912x_fast_chroma_selected_backend_name(), names[expected]));
                checks++;
            }
            for (unsigned tolerance = 0; tolerance < 2; tolerance++) {
                for (unsigned stable = 0; stable < 2; stable++) {
                    enum ms912x_damage_scan_method expected =
                        stable ? MS912X_DAMAGE_SCAN_MEMCMP : MS912X_DAMAGE_SCAN_SCALAR;

                    if (usable && (tolerance || stable)) {
#ifdef CONFIG_X86_64
                        expected = (flags & 24) == 24           ? MS912X_DAMAGE_SCAN_AVX2
                                   : (tolerance && (flags & 4)) ? MS912X_DAMAGE_SCAN_SSE41
                                                                : MS912X_DAMAGE_SCAN_SSE2;
#else
                        expected = MS912X_DAMAGE_SCAN_NEON;
#endif
                    }
                    assert(ms912x_damage_scan_select_auto(tolerance, stable) == expected);
                    if (expected != MS912X_DAMAGE_SCAN_SCALAR &&
                        expected != MS912X_DAMAGE_SCAN_MEMCMP)
                        assert(ms912x_damage_scan_simd_method_available(expected));
                    assert(ms912x_damage_scan_simd_method_available(MS912X_DAMAGE_SCAN_SSE41) ==
#ifdef CONFIG_X86_64
                           (usable && !!(flags & 4))
#else
                           false
#endif
                    );
                    checks++;
                }
            }
        }
    }
    bench_simd_allowed = true;
    bench_cpu_features = 31;
    printf("Dispatch matrix: %u cases, feature masks, forced modes, SIMD guards, X stability\n",
           checks);
}
