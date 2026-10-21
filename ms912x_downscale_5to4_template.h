/* SPDX-License-Identifier: GPL-2.0-only */
/* Four output rows share seven source rows; load all results before stores. */
#define MS912X_5TO4_OUTPUTS(GET) \
	MS912X_5TO4_TYPE a = GET(rows[0]), b = GET(rows[1]); \
	MS912X_5TO4_TYPE c = GET(rows[2]), d = GET(rows[3]); \
	MS912X_5TO4_TYPE e = GET(rows[4]), f = GET(rows[5]), g = GET(rows[6]); \
	MS912X_5TO4_TYPE v0 = MS912X_5TO4_A(b, c, a, d); \
	MS912X_5TO4_TYPE v1 = MS912X_5TO4_B(c, d, b, e); \
	MS912X_5TO4_TYPE v2 = MS912X_5TO4_B(e, d, f, c); \
	MS912X_5TO4_TYPE v3 = MS912X_5TO4_A(f, e, g, d); \
	MS912X_5TO4_STORE(out + x, v0); \
	if (out1) MS912X_5TO4_STORE(out1 + x, v1); \
	if (out2) MS912X_5TO4_STORE(out2 + x, v2); \
	if (out3) MS912X_5TO4_STORE(out3 + x, v3)
#define MS912X_5TO4_READ(row) MS912X_5TO4_LOAD((row) + x)
#define MS912X_5TO4_FILTER(row) MS912X_5TO4_H(row, x, ox)

MS912X_5TO4_SCOPE void MS912X_5TO4_WORKER(const u32 *src, u32 *dst,
		unsigned int sw, unsigned int sh, unsigned int dw,
		unsigned int ss, unsigned int ds, unsigned int first, unsigned int end,
		enum ms912x_5to4_stage stage, unsigned int ox, unsigned int oy)
{
	unsigned int x, y;

	if (stage == MS912X_5TO4_HORIZONTAL) {
		unsigned int full = min(dw, (sw - 1) / 5 * 4);

		for (y = first; y < end; y++) {
			const u32 *row = src + y * ss;
			u32 *out = dst + y * ds;
			u32 edge = row[sw - 1];
			u32 leading[4];
			unsigned int count = min(dw, 4U);
			unsigned int padding = min(dw, DIV_ROUND_UP(sw * 4, 5));

			/* Preserve the first group's source taps before an in-place write. */
			for (x = 0; x < count; x++)
				leading[x] = ms912x_5to4_cubic_horizontal(row, sw, x, 0);
			for (x = 0; x < count; x++)
				out[x] = leading[x];
			for (; x + MS912X_5TO4_PIXELS <= full; x += MS912X_5TO4_PIXELS)
				MS912X_5TO4_STORE(out + x, MS912X_5TO4_H(row, x, 0));
			for (; x < padding; x++)
				out[x] = ms912x_5to4_cubic_horizontal(row, sw, x, 0);
			for (; x < dw; x++)
				out[x] = edge;
		}
		MS912X_5TO4_FINISH();
		return;
	}

	for (y = first; y < end; y += 4) {
		const u32 *rows[7];
		u32 *out = dst + y * ds;
		u32 *out1 = y + 1 < end ? out + ds : NULL;
		u32 *out2 = y + 2 < end ? out + 2 * ds : NULL;
		u32 *out3 = y + 3 < end ? out + 3 * ds : NULL;

		ms912x_5to4_cubic_rows(src, sh, ss, y, oy, rows);
		x = 0;
		if (stage == MS912X_5TO4_VERTICAL) {
			for (; x + MS912X_5TO4_PIXELS <= dw; x += MS912X_5TO4_PIXELS) {
				MS912X_5TO4_OUTPUTS(MS912X_5TO4_READ);
			}
		} else {
			unsigned int full = min(dw, (sw - ox - 1) / 5 * 4);

			if (!ox)
				for (; x < min(dw, 4U); x++) {
					u32 v[4];

					ms912x_5to4_cubic_column(rows, sw, x, ox, false, v);
					out[x] = v[0];
					if (out1) out1[x] = v[1];
					if (out2) out2[x] = v[2];
					if (out3) out3[x] = v[3];
				}
			for (; x + MS912X_5TO4_PIXELS <= full; x += MS912X_5TO4_PIXELS) {
				MS912X_5TO4_OUTPUTS(MS912X_5TO4_FILTER);
			}
		}
		for (; x < dw; x++) {
			u32 v[4];

			ms912x_5to4_cubic_column(rows, sw, x, ox,
						stage == MS912X_5TO4_VERTICAL, v);
			out[x] = v[0];
			if (out1) out1[x] = v[1];
			if (out2) out2[x] = v[2];
			if (out3) out3[x] = v[3];
		}
	}
	MS912X_5TO4_FINISH();
}

#undef MS912X_5TO4_FILTER
#undef MS912X_5TO4_READ
#undef MS912X_5TO4_OUTPUTS
