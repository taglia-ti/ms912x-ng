/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MS912X_SCAN_BACKEND_H
#define MS912X_SCAN_BACKEND_H

#include "ms912x_scan_backend_types.h"
#include "../ms912x_scan_methods.h"

int ms912x_core_scan_backend_supported(enum ms912x_scan_backend backend);
enum ms912x_scan_backend ms912x_core_scan_default_backend(void);
/* AUTO selects the fastest available SIMD backend; unavailable requests use scalar. */
enum ms912x_scan_backend ms912x_core_scan_resolve_backend(enum ms912x_scan_backend backend);
u32 ms912x_core_scan_backend_mask(enum ms912x_scan_backend backend, u32 mask);
unsigned int ms912x_core_scan_run_words(enum ms912x_scan_backend backend,
				      unsigned int pixel_shift);
/* MEMCMP is exact (ignores mask), with full-row baseline copying when next != NULL.
 * Other backends retain Linux's masked-block selective baseline semantics.
 * Kernel calls retain the architecture wrappers' FPU scopes (up to 16 rows/section).
 */
void ms912x_core_scan_rows(const u32 *old, const u32 *new, u32 *next,
			  unsigned int width, size_t row_stride,
			  unsigned int row_count, u32 mask,
			  struct ms912x_scan_row_result *results,
			  enum ms912x_scan_backend backend);
void ms912x_core_scan_row(const u32 *old, const u32 *new, unsigned int width,
			 u32 mask, struct ms912x_scan_row_result *result,
			 enum ms912x_scan_backend backend);
#endif
