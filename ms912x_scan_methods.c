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

bool ms912x_scan_sse41_available(void)
{
	return boot_cpu_has(X86_FEATURE_XMM4_1) && may_use_simd();
}

bool ms912x_scan_sse2_available(void)
{
	return boot_cpu_has(X86_FEATURE_XMM2) && may_use_simd();
}

bool ms912x_scan_neon_available(void)
{
	return false;
}
#elif MS912X_HAS_NEON
#include <asm/neon.h>
#include <asm/simd.h>

bool ms912x_scan_avx2_available(void)
{
	return false;
}

bool ms912x_scan_sse41_available(void)
{
	return false;
}

bool ms912x_scan_sse2_available(void)
{
	return false;
}

bool ms912x_scan_neon_available(void)
{
	return cpu_has_neon() && may_use_simd();
}
#else
bool ms912x_scan_avx2_available(void)
{
	return false;
}

bool ms912x_scan_sse41_available(void)
{
	return false;
}

bool ms912x_scan_sse2_available(void)
{
	return false;
}

bool ms912x_scan_neon_available(void)
{
	return false;
}
#endif

bool ms912x_scan_simd_available(void)
{
	return ms912x_scan_avx2_available() ||
		ms912x_scan_sse2_available() ||
		ms912x_scan_neon_available();
}
