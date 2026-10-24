/* SPDX-License-Identifier: GPL-2.0-only */
/* Integer entry points preserve the SIMD boundary and in-place source rows. */
#include <linux/string.h>

void MS912X_5TO4_REGION(const u32 *src, u32 *dst,
		unsigned int sw, unsigned int sh, unsigned int dw, unsigned int dh,
		unsigned int ss, unsigned int ds, unsigned int ox, unsigned int oy)
{
	enum ms912x_5to4_stage stage;
	unsigned int first, vertical_stride = ds;

	if (!sw || !sh || !dw || !dh || ox >= sw || oy >= sh)
		return;
	if (src == dst) {
		for (first = 0; first < sh; first += MS912X_DOWNSCALE_ROWS_PER_FPU) {
			unsigned int end = min(first + MS912X_DOWNSCALE_ROWS_PER_FPU, sh);

			MS912X_5TO4_BEGIN();
			MS912X_5TO4_WORKER(src, dst, sw, sh, dw, ss, ss, first, end,
				MS912X_5TO4_HORIZONTAL, 0, 0);
			MS912X_5TO4_END();
		}
		stage = MS912X_5TO4_VERTICAL;
		vertical_stride = ss;
	} else {
		stage = MS912X_5TO4_DIRECT;
	}
	for (first = 0; first < dh; first += MS912X_DOWNSCALE_ROWS_PER_FPU) {
		unsigned int end = min(first + MS912X_DOWNSCALE_ROWS_PER_FPU, dh);

		MS912X_5TO4_BEGIN();
		MS912X_5TO4_WORKER(src, dst, sw, sh, dw, ss, vertical_stride,
			first, end, stage, ox, oy);
		MS912X_5TO4_END();
	}
	/* Packed output can overwrite future taps inside a four-row group. */
	if (src == dst && ds != ss) {
		if (ds < ss)
			for (first = 0; first < dh; first++)
				memmove(dst + first * ds, dst + first * ss, dw * sizeof(*dst));
		else
			for (first = dh; first-- > 0;)
				memmove(dst + first * ds, dst + first * ss, dw * sizeof(*dst));
	}
}
