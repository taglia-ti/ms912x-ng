/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef BENCH_CPUFEATURE_H
#define BENCH_CPUFEATURE_H

/* Synthetic feature bits used only by the dispatch matrix checks. */
#define X86_FEATURE_XMM2 0
#define X86_FEATURE_SSSE3 1
#define X86_FEATURE_XMM4_1 2
#define X86_FEATURE_AVX 3
#define X86_FEATURE_AVX2 4
extern unsigned int bench_cpu_features;
#define boot_cpu_has(feature) (!!(bench_cpu_features & (1U << (feature))))

#endif
