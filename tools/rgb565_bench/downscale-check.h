/* SPDX-License-Identifier: GPL-2.0-only */
/* Independent native-channel oracle, with explicit sampling phases. */
#ifndef MS912X_DOWNSCALE_AREA_FILTER
static int native_clamp(int value, int maximum)
{
	return value < 0 ? 0 : value > maximum ? maximum : value;
}

static void native_axis(unsigned mode, unsigned x, unsigned offset, int at[4],
			int weights[4])
{
	unsigned base = offset + x + (mode == 2 ? x / 2 : x / 4);
	unsigned phase = mode == 2 ? (x & 1) : (x & 3);
	bool reverse = mode == 2 ? phase : phase >= 2;
	at[0] = base + reverse;
	at[1] = base + !reverse;
	at[2] = reverse ? (int)base + 2 : (int)base - 1;
	at[3] = reverse ? (int)base - 1 : (int)base + 2;
	if (mode == 2) {
		weights[0] = 111;
		weights[1] = 29;
		weights[2] = -9;
		weights[3] = -3;
	} else {
		bool inner = phase == 1 || phase == 2;
		weights[0] = inner ? 93 : 123;
		weights[1] = inner ? 50 : 12;
		weights[2] = inner ? -9 : -6;
		weights[3] = inner ? -6 : -1;
	}
}

#endif

static u16 native_oracle(const u16 *src, unsigned sw, unsigned sh, unsigned ss,
			 unsigned x, unsigned y, unsigned ox, unsigned oy,
			 unsigned mode)
{
	const unsigned shifts[] = { 0, 5, 11 }, masks[] = { 31, 63, 31 };
	u16 pixel = 0;
	for (unsigned ch = 0; ch < 3; ch++) {
		unsigned shift = shifts[ch], mask = masks[ch];
		int value = 0;
		if (mode == 1) {
			for (unsigned yy = 0; yy < 2; yy++)
				for (unsigned xx = 0; xx < 2; xx++) {
					unsigned sx = min(ox + x * 2 + xx,
							  sw - 1),
						 sy = min(oy + y * 2 + yy,
							  sh - 1);
					value += (src[sy * ss + sx] >> shift) &
						 mask;
				}
			value = (value + 2) / 4;
		} else {
#ifdef MS912X_DOWNSCALE_AREA_FILTER
			unsigned p = mode == 2 ? 3 : 5;
			unsigned sx = ox + x + x / (p - 1),
				 sy = oy + y + y / (p - 1);
			unsigned wx = p - 1 - x % (p - 1),
				 wy = p - 1 - y % (p - 1);
			for (unsigned yy = 0; yy < 2; yy++)
				for (unsigned xx = 0; xx < 2; xx++)
					value +=
						((src[min(sy + yy, sh - 1) * ss +
						      min(sx + xx, sw - 1)] >>
						  shift) &
						 mask) *
						(xx ? p - wx : wx) *
						(yy ? p - wy : wy);
			value = (value + (p * p) / 2) / (p * p);
#else
			int ax[4], ay[4], wx[4], wy[4];
			native_axis(mode, x, ox, ax, wx);
			native_axis(mode, y, oy, ay, wy);
			for (unsigned yy = 0; yy < 4; yy++) {
				int horizontal = 0;
				for (unsigned xx = 0; xx < 4; xx++) {
					unsigned sx = native_clamp(ax[xx],
								   sw - 1),
						 sy = native_clamp(ay[yy],
								   sh - 1);
					horizontal +=
						((src[sy * ss + sx] >> shift) &
						 mask) *
						wx[xx];
				}
				horizontal = native_clamp(
					(horizontal + 64) >> 7, mask);
				value += horizontal * wy[yy];
			}
			value = native_clamp((value + 64) >> 7, mask);
#endif
		}
		pixel |= value << shift;
	}
	return pixel;
}

static void native_downscale_checks(void)
{
	unsigned tested = 0;
	for (unsigned mode = 1; mode <= 3; mode++)
		for (unsigned w = 1; w <= 67; w++)
			for (unsigned h = 1; h <= 13; h += 3)
				for (unsigned crop = 0; crop < 2; crop++) {
					unsigned sw = w + crop * 3,
						 sh = h + crop * 2, ss = sw + 7;
					unsigned ox = crop * 2, oy = crop;
					unsigned dw = ms912x_scale_dimension(
							 sw - ox, mode),
						 dh = ms912x_scale_dimension(
							 sh - oy, mode);
					/* Include padding pixels beyond the rational width and height. */
					dw += 2;
					dh++;
					unsigned ds = dw + 5;
					u16 *src = malloc(ss * sh * 2),
					    *out = malloc((ds * dh + 2) * 2),
					    *saved = malloc(ss * sh * 2);
					assert(src && out && saved);
					for (unsigned i = 0; i < ss * sh; i++)
						src[i] = random32();
					memcpy(saved, src, ss * sh * 2);
					for (unsigned i = 0; i < ds * dh + 2;
					     i++)
						out[i] = 0xa55a;
					ms912x_downscale_rgb565_region(
						mode, src, out + 1, sw, sh, dw,
						dh, ss, ds, ox, oy);
					for (unsigned y = 0; y < dh; y++)
						for (unsigned x = 0; x < ds;
						     x++) {
							u16 expected =
								x < dw ?
									native_oracle(
										src,
										sw,
										sh,
										ss,
										x,
										y,
										ox,
										oy,
										mode) :
									0xa55a;
							if (out[1 + y * ds + x] !=
							    expected) {
								fprintf(stderr,
									"RGB565 scale mismatch mode=%u %ux%u crop=%u at %u,%u got=%04x expected=%04x\n",
									mode,
									sw, sh,
									crop, x,
									y,
									out[1 +
									    y * ds +
									    x],
									expected);
								abort();
							}
						}
					assert(out[0] == 0xa55a &&
					       out[ds * dh + 1] == 0xa55a &&
					       !memcmp(src, saved,
						       ss * sh * 2));
					free(src);
					free(out);
					free(saved);
					tested++;
				}
	printf("PASS native RGB565 downscale: %u geometries/crops, all ratios/channels/phases, padding, source preservation and guards\n",
	       tested);
}
