/* SPDX-License-Identifier: GPL-2.0-only */
/* Shared row scheduling; include once after defining the SIMD operations. */
MS912X_CUBIC_SCOPE void MS912X_CUBIC_WORKER(const u32 *src, u32 *dst,
		unsigned int sw, unsigned int sh, unsigned int dw,
		unsigned int ss, unsigned int ds, unsigned int first, unsigned int end,
		enum ms912x_downscale_stage stage, unsigned int ox, unsigned int oy)
{
	unsigned int y, x;

	if (stage == MS912X_DOWNSCALE_HORIZONTAL) {
		unsigned int full = min(dw, (sw - 1) / 3 * 2);

		for (y = first; y < end; y++) {
			const u32 *row = src + y * ss;
			u32 *out = dst + y * ds;
			u32 a = ms912x_downscale_cubic_horizontal(row, sw, 0, 0);
			u32 b = dw > 1 ? ms912x_downscale_cubic_horizontal(row, sw, 1, 0) : 0;

			/* Read both first-phase pixels before an in-place write. */
			out[0] = a;
			if (dw > 1)
				out[1] = b;
			for (x = min(dw, 2U); x + MS912X_CUBIC_PIXELS <= full;
			     x += MS912X_CUBIC_PIXELS)
				MS912X_CUBIC_STORE(out + x, MS912X_CUBIC_H(row, x, 0));
			for (; x < dw; x += 2) {
				a = ms912x_downscale_cubic_horizontal(row, sw, x, 0);
				b = x + 1 < dw ?
					ms912x_downscale_cubic_horizontal(row, sw, x + 1, 0) : 0;
				out[x] = a;
				if (x + 1 < dw)
					out[x + 1] = b;
			}
		}
		MS912X_CUBIC_FINISH();
		return;
	}

	/* Two destination rows share five source rows and two filter phases. */
	for (y = first; y < end; y += 2) {
		const u32 *rows[5];
		u32 *out = dst + y * ds;
		u32 *out1 = y + 1 < end ? out + ds : NULL;

		ms912x_downscale_cubic_rows(src, sh, ss, y, oy, rows);
		x = 0;
		if (stage == MS912X_DOWNSCALE_VERTICAL) {
			for (; x + MS912X_CUBIC_PIXELS <= dw; x += MS912X_CUBIC_PIXELS) {
				MS912X_CUBIC_TYPE a = MS912X_CUBIC_LOAD(rows[0] + x);
				MS912X_CUBIC_TYPE b = MS912X_CUBIC_LOAD(rows[1] + x);
				MS912X_CUBIC_TYPE c = MS912X_CUBIC_LOAD(rows[2] + x);
				MS912X_CUBIC_TYPE d = MS912X_CUBIC_LOAD(rows[3] + x);
				MS912X_CUBIC_TYPE e = MS912X_CUBIC_LOAD(rows[4] + x);
				MS912X_CUBIC_TYPE top = MS912X_CUBIC_FILTER(b, c, a, d);
				MS912X_CUBIC_TYPE bottom = MS912X_CUBIC_FILTER(d, c, e, b);

				/* Both results precede stores, including packed in-place output. */
				MS912X_CUBIC_STORE(out + x, top);
				if (out1)
					MS912X_CUBIC_STORE(out1 + x, bottom);
			}
			for (; x < dw; x++) {
				u32 top = ms912x_downscale_cubic_pixel(rows[1][x], rows[2][x],
							     rows[0][x], rows[3][x]);
				u32 bottom = ms912x_downscale_cubic_pixel(rows[3][x], rows[2][x],
								rows[4][x], rows[1][x]);

				out[x] = top;
				if (out1)
					out1[x] = bottom;
			}
		} else {
			unsigned int full = min(dw, (sw - ox - 1) / 3 * 2);

			/* The interior load includes one source pixel to its left. */
			if (!ox) {
				for (; x < min(dw, 2U); x++) {
					u32 top, bottom;

					ms912x_downscale_cubic_edge(rows, sw, x, ox, &top, &bottom);
					out[x] = top;
					if (out1)
						out1[x] = bottom;
				}
			}
			for (; x + MS912X_CUBIC_PIXELS <= full; x += MS912X_CUBIC_PIXELS) {
				MS912X_CUBIC_TYPE a = MS912X_CUBIC_H(rows[0], x, ox);
				MS912X_CUBIC_TYPE b = MS912X_CUBIC_H(rows[1], x, ox);
				MS912X_CUBIC_TYPE c = MS912X_CUBIC_H(rows[2], x, ox);
				MS912X_CUBIC_TYPE d = MS912X_CUBIC_H(rows[3], x, ox);
				MS912X_CUBIC_TYPE e = MS912X_CUBIC_H(rows[4], x, ox);
				MS912X_CUBIC_TYPE top = MS912X_CUBIC_FILTER(b, c, a, d);
				MS912X_CUBIC_TYPE bottom = MS912X_CUBIC_FILTER(d, c, e, b);

				MS912X_CUBIC_STORE(out + x, top);
				if (out1)
					MS912X_CUBIC_STORE(out1 + x, bottom);
			}
			for (; x < dw; x++) {
				u32 top, bottom;

				ms912x_downscale_cubic_edge(rows, sw, x, ox, &top, &bottom);
				out[x] = top;
				if (out1)
					out1[x] = bottom;
			}
		}
	}
	MS912X_CUBIC_FINISH();
}
