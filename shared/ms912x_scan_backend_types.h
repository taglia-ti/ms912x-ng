/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MS912X_SCAN_BACKEND_TYPES_H
#define MS912X_SCAN_BACKEND_TYPES_H

/* Keep the Linux parameter values stable on both platforms. */
enum ms912x_scan_backend {
	MS912X_SCAN_SCALAR,
	MS912X_SCAN_MEMCMP,
	MS912X_SCAN_AVX,
	MS912X_SCAN_SSE2,
	MS912X_SCAN_NEON,
	MS912X_SCAN_AUTO,
	MS912X_SCAN_BACKEND_COUNT,
};

#endif
