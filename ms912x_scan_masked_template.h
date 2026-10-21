/* SPDX-License-Identifier: GPL-2.0-only */
/* Instantiate with scalar/SIMD operations; no format dispatch inside the loop. */
static __always_inline unsigned int
MS912X_MASKED_BLOCK(const u32 *old, const u32 *new, u32 *next,
		    unsigned int count, u32 pixel_mask)
{
	MS912X_MASKED_TYPE d0 = MS912X_MASKED_ZERO();
	MS912X_MASKED_TYPE d1 = d0, d2 = d0, d3 = d0, diff;
	const u32 *selected;
	u32 tail = 0;
	unsigned int x = 0, changed;

	/* Independent reduction chains also suit in-order ARM cores. */
	for (; x + 4 * MS912X_MASKED_PIXELS <= count; x += 4 * MS912X_MASKED_PIXELS) {
		d0 = MS912X_MASKED_OR(d0, MS912X_MASKED_DIFF(old + x, new + x));
		d1 = MS912X_MASKED_OR(d1, MS912X_MASKED_DIFF(
			old + x + MS912X_MASKED_PIXELS, new + x + MS912X_MASKED_PIXELS));
		d2 = MS912X_MASKED_OR(d2, MS912X_MASKED_DIFF(
			old + x + 2 * MS912X_MASKED_PIXELS, new + x + 2 * MS912X_MASKED_PIXELS));
		d3 = MS912X_MASKED_OR(d3, MS912X_MASKED_DIFF(
			old + x + 3 * MS912X_MASKED_PIXELS, new + x + 3 * MS912X_MASKED_PIXELS));
	}
	diff = MS912X_MASKED_OR(MS912X_MASKED_OR(d0, d1), MS912X_MASKED_OR(d2, d3));
	for (; x + MS912X_MASKED_PIXELS <= count; x += MS912X_MASKED_PIXELS)
		diff = MS912X_MASKED_OR(diff, MS912X_MASKED_DIFF(old + x, new + x));
	for (; x < count; x++)
		tail |= old[x] ^ new[x];
	changed = MS912X_MASKED_CHANGED(diff, pixel_mask) | ((tail & pixel_mask) != 0);

	/* A changed block is sent in full; retain the old baseline for other blocks. */
	selected = changed ? new : old;
	x = 0;
	for (; x + MS912X_MASKED_PIXELS <= count; x += MS912X_MASKED_PIXELS)
		MS912X_MASKED_COPY(next + x, selected + x);
	for (; x < count; x++)
		next[x] = selected[x];
	return changed;
}

#ifdef MS912X_MASKED_PAIR
/* Compare both full blocks before selecting and copying their baselines. */
static __always_inline unsigned int
MS912X_MASKED_PAIR(const u32 *old, const u32 *new, u32 *next, u32 pixel_mask)
{
	const unsigned int block = MS912X_SCAN_MASKED_BLOCK_PIXELS;
	MS912X_MASKED_TYPE a0 = MS912X_MASKED_ZERO();
	MS912X_MASKED_TYPE a1 = a0, a2 = a0, a3 = a0;
	MS912X_MASKED_TYPE b0 = a0, b1 = a0, b2 = a0, b3 = a0;
	const u32 *selected0, *selected1;
	unsigned int x, changed0, changed1;

	/* Interleave the independent reductions without joining their damage. */
	for (x = 0; x < block; x += 4 * MS912X_MASKED_PIXELS) {
		a0 = MS912X_MASKED_OR(a0, MS912X_MASKED_DIFF(old + x, new + x));
		b0 = MS912X_MASKED_OR(b0, MS912X_MASKED_DIFF(
			old + block + x, new + block + x));
		a1 = MS912X_MASKED_OR(a1, MS912X_MASKED_DIFF(
			old + x + MS912X_MASKED_PIXELS, new + x + MS912X_MASKED_PIXELS));
		b1 = MS912X_MASKED_OR(b1, MS912X_MASKED_DIFF(
			old + block + x + MS912X_MASKED_PIXELS,
			new + block + x + MS912X_MASKED_PIXELS));
		a2 = MS912X_MASKED_OR(a2, MS912X_MASKED_DIFF(
			old + x + 2 * MS912X_MASKED_PIXELS, new + x + 2 * MS912X_MASKED_PIXELS));
		b2 = MS912X_MASKED_OR(b2, MS912X_MASKED_DIFF(
			old + block + x + 2 * MS912X_MASKED_PIXELS,
			new + block + x + 2 * MS912X_MASKED_PIXELS));
		a3 = MS912X_MASKED_OR(a3, MS912X_MASKED_DIFF(
			old + x + 3 * MS912X_MASKED_PIXELS, new + x + 3 * MS912X_MASKED_PIXELS));
		b3 = MS912X_MASKED_OR(b3, MS912X_MASKED_DIFF(
			old + block + x + 3 * MS912X_MASKED_PIXELS,
			new + block + x + 3 * MS912X_MASKED_PIXELS));
	}
	a0 = MS912X_MASKED_OR(MS912X_MASKED_OR(a0, a1), MS912X_MASKED_OR(a2, a3));
	b0 = MS912X_MASKED_OR(MS912X_MASKED_OR(b0, b1), MS912X_MASKED_OR(b2, b3));
	changed0 = MS912X_MASKED_CHANGED(a0, pixel_mask);
	changed1 = MS912X_MASKED_CHANGED(b0, pixel_mask);
	selected0 = changed0 ? new : old;
	selected1 = changed1 ? new + block : old + block;
	for (x = 0; x < block; x += 4 * MS912X_MASKED_PIXELS) {
		MS912X_MASKED_COPY(next + x, selected0 + x);
		MS912X_MASKED_COPY(next + block + x, selected1 + x);
		MS912X_MASKED_COPY(next + x + MS912X_MASKED_PIXELS,
			selected0 + x + MS912X_MASKED_PIXELS);
		MS912X_MASKED_COPY(next + block + x + MS912X_MASKED_PIXELS,
			selected1 + x + MS912X_MASKED_PIXELS);
		MS912X_MASKED_COPY(next + x + 2 * MS912X_MASKED_PIXELS,
			selected0 + x + 2 * MS912X_MASKED_PIXELS);
		MS912X_MASKED_COPY(next + block + x + 2 * MS912X_MASKED_PIXELS,
			selected1 + x + 2 * MS912X_MASKED_PIXELS);
		MS912X_MASKED_COPY(next + x + 3 * MS912X_MASKED_PIXELS,
			selected0 + x + 3 * MS912X_MASKED_PIXELS);
		MS912X_MASKED_COPY(next + block + x + 3 * MS912X_MASKED_PIXELS,
			selected1 + x + 3 * MS912X_MASKED_PIXELS);
	}
	return changed0 | (changed1 << 1);
}
#endif

static __always_inline void
MS912X_MASKED_ROW(const u32 *old, const u32 *new, u32 *next,
		  unsigned int width, u32 pixel_mask,
		  struct ms912x_scan_row_result *result)
{
	const unsigned int block = MS912X_SCAN_MASKED_BLOCK_PIXELS;
	unsigned int x = 0, full_pixels = width - width % block;
	unsigned int changed_blocks = 0, tail_changed = 0;
	unsigned int first = width, last = 0;

	ms912x_scan_row_result_init(result, width);
#ifdef MS912X_MASKED_PAIR
	for (; x < width - width % (2 * block); x += 2 * block) {
		unsigned int changed = MS912X_MASKED_PAIR(old + x, new + x,
							next + x, pixel_mask);
		unsigned int changed0 = changed & 1, changed1 = (changed >> 1) & 1;

		changed_blocks += changed0 + changed1;
		first = min(first, changed0 ? x : width);
		first = min(first, changed1 ? x + block : width);
		last = changed0 ? x + block - 1 : last;
		last = changed1 ? x + 2 * block - 1 : last;
	}
#endif
	for (; x < full_pixels; x += block) {
		unsigned int changed = MS912X_MASKED_BLOCK(old + x, new + x,
							 next + x, block, pixel_mask);

		changed_blocks += changed;
		first = min(first, changed ? x : width);
		last = changed ? x + block - 1 : last;
	}
	if (x < width) {
		tail_changed = MS912X_MASKED_BLOCK(old + x, new + x,
						 next + x, width - x, pixel_mask);
		first = min(first, tail_changed ? x : width);
		last = tail_changed ? width - 1 : last;
	}
	/* Publish once per row, including on ARM32 where counters are 64-bit. */
	result->avx2_blocks = DIV_ROUND_UP(width, block);
	result->avx2_equal_blocks = result->avx2_blocks - changed_blocks - tail_changed;
	result->changed_pixels = (u64)changed_blocks * block + (width - x) * tail_changed;
	result->min_x = first;
	result->max_x = last;
	result->equal_row = !result->changed_pixels;
}

#undef MS912X_MASKED_TYPE
#undef MS912X_MASKED_PIXELS
#undef MS912X_MASKED_ZERO
#undef MS912X_MASKED_OR
#undef MS912X_MASKED_DIFF
#undef MS912X_MASKED_CHANGED
#undef MS912X_MASKED_COPY
#undef MS912X_MASKED_BLOCK
#undef MS912X_MASKED_PAIR
#undef MS912X_MASKED_ROW
