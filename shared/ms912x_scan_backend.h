/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MS912X_SCAN_BACKEND_H
#define MS912X_SCAN_BACKEND_H
#include "../ms912x_scan_methods.h"
enum ms912x_scan_backend { MS912X_SCAN_AUTO, MS912X_SCAN_SCALAR, MS912X_SCAN_SSE2,
                           MS912X_SCAN_NEON, MS912X_SCAN_AVX };
int ms912x_core_scan_backend_supported(enum ms912x_scan_backend backend);
enum ms912x_scan_backend ms912x_core_scan_default_backend(void);
void ms912x_core_scan_row(const u32 *old, const u32 *new, unsigned int width,
                         u32 mask, struct ms912x_scan_row_result *result,
                         enum ms912x_scan_backend backend);
#endif
