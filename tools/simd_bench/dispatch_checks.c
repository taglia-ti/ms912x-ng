/* SPDX-License-Identifier: GPL-2.0-only */
/* Included after the actual driver selectors by run.py. */
struct dispatch_expectation {
  enum ms912x_fast_chroma_backend conversion;
  enum ms912x_damage_scan_method scan;
  bool avx_available;
};

#ifdef CONFIG_X86_64
static struct dispatch_expectation expected_dispatch(unsigned mode, unsigned flags, bool usable) {
  struct dispatch_expectation expected = {
      .conversion = MS912X_FAST_CHROMA_SCALAR,
      .scan = MS912X_DAMAGE_SCAN_SCALAR,
      .avx_available = usable && !!(flags & 8),
  };
  bool avx2 = (flags & 24) == 24;
  bool ssse3 = flags & 2;

  if (!usable)
    return expected;
  expected.scan = flags & 8 ? MS912X_DAMAGE_SCAN_AVX : MS912X_DAMAGE_SCAN_SSE2;
  if (mode == MS912X_FAST_CHROMA_AUTO)
    expected.conversion = avx2    ? MS912X_FAST_CHROMA_AVX2
                          : ssse3 ? MS912X_FAST_CHROMA_SSSE3
                                  : MS912X_FAST_CHROMA_SSE2;
  else if ((mode == MS912X_FAST_CHROMA_AVX2 && avx2) ||
           (mode == MS912X_FAST_CHROMA_SSSE3 && ssse3) || mode == MS912X_FAST_CHROMA_SSE2)
    expected.conversion = mode;
  return expected;
}
#elif MS912X_HAS_NEON
static struct dispatch_expectation expected_dispatch(unsigned mode, unsigned flags, bool usable) {
  struct dispatch_expectation expected = {
      .conversion = MS912X_FAST_CHROMA_SCALAR,
      .scan = MS912X_DAMAGE_SCAN_SCALAR,
  };

  if (usable && (flags & 2)) {
    expected.scan = MS912X_DAMAGE_SCAN_NEON;
    if (mode == MS912X_FAST_CHROMA_AUTO || mode == MS912X_FAST_CHROMA_NEON)
      expected.conversion = MS912X_FAST_CHROMA_NEON;
  }
  return expected;
}
#else
static struct dispatch_expectation expected_dispatch(unsigned mode, unsigned flags, bool usable) {
  (void)mode;
  (void)flags;
  (void)usable;
  return (struct dispatch_expectation){
      .conversion = MS912X_FAST_CHROMA_SCALAR,
      .scan = MS912X_DAMAGE_SCAN_SCALAR,
  };
}
#endif

void dispatch_tests(void) {
  unsigned checks = 0;
  const char *names[] = {"scalar", "auto", "avx2", "neon", "sse2", "ssse3"};

  for (unsigned flags = 1; flags < 32; flags += 2) {
    bench_cpu_features = flags;
    bench_neon_present = !!(flags & 2);
    for (unsigned usable = 0; usable < 2; usable++) {
      bench_simd_allowed = usable;
      for (unsigned mode = 0; mode <= MS912X_FAST_CHROMA_SSSE3; mode++) {
        struct dispatch_expectation expected = expected_dispatch(mode, flags, usable);

        ms912x_parameters.fast_chroma = mode;
        assert(ms912x_fast_chroma_select() == expected.conversion);
        assert(!strcmp(ms912x_fast_chroma_selected_backend_name(), names[expected.conversion]));
        checks++;
      }
      {
        struct dispatch_expectation expected =
            expected_dispatch(MS912X_FAST_CHROMA_AUTO, flags, usable);

        assert(ms912x_core_scan_default_backend() == (enum ms912x_scan_backend)expected.scan);
        assert(ms912x_core_scan_backend_supported(MS912X_SCAN_AVX) ==
               expected.avx_available);
        checks++;
      }
    }
  }
  bench_simd_allowed = true;
  bench_neon_present = true;
  bench_cpu_features = 31;
  printf("Dispatch matrix: %u cases, feature masks, forced modes, SIMD guards and UYVY scan "
         "fallback\n",
         checks);
}
