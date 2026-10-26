/* SPDX-License-Identifier: GPL-2.0-only */
/* Integer entry points; the worker is kept behind explicit FPU boundaries. */
void MS912X_CUBIC_REGION(const u32 *src, u32 *dst,
		unsigned int sw, unsigned int sh, unsigned int dw, unsigned int dh,
		unsigned int ss, unsigned int ds, unsigned int ox, unsigned int oy)
{
	enum ms912x_downscale_stage stage;
	unsigned int first;

	if (!sw || !sh || !dw || !dh || ox >= sw || oy >= sh)
		return;
#ifdef MS912X_CUBIC_AVAILABLE
	if (!MS912X_CUBIC_AVAILABLE()) {
		ms912x_downscale_xrgb8888_3to2_scalar_region(src, dst,
			sw, sh, dw, dh, ss, ds, ox, oy);
		return;
	}
#endif
	/* In-place input is a full image, without staged-crop offsets. */
	if (src == dst) {
		for (first = 0; first < sh; first += MS912X_DOWNSCALE_ROWS_PER_FPU) {
			unsigned int end = min(first + MS912X_DOWNSCALE_ROWS_PER_FPU, sh);

			MS912X_CUBIC_BEGIN();
			MS912X_CUBIC_WORKER(src, dst, sw, sh, dw, ss, ss, first, end,
				MS912X_DOWNSCALE_HORIZONTAL, 0, 0);
			MS912X_CUBIC_END();
		}
		stage = MS912X_DOWNSCALE_VERTICAL;
	} else {
		stage = MS912X_DOWNSCALE_DIRECT;
	}
	for (first = 0; first < dh; first += MS912X_DOWNSCALE_ROWS_PER_FPU) {
		unsigned int end = min(first + MS912X_DOWNSCALE_ROWS_PER_FPU, dh);

		MS912X_CUBIC_BEGIN();
		MS912X_CUBIC_WORKER(src, dst, sw, sh, dw, ss, ds, first, end, stage, ox, oy);
		MS912X_CUBIC_END();
	}
}
