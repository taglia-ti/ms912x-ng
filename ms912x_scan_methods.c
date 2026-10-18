// SPDX-License-Identifier: GPL-2.0-only

#include "ms912x_scan_methods.h"

#ifdef CONFIG_X86_64
#include <asm/cpufeature.h>
#include <asm/simd.h>

bool ms912x_scan_avx2_available(void)
{
	return boot_cpu_has(X86_FEATURE_AVX) &&
		boot_cpu_has(X86_FEATURE_AVX2) && may_use_simd();
}
#else
bool ms912x_scan_avx2_available(void)
{
	return false;
}
#endif
