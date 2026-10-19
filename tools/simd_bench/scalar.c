/* SPDX-License-Identifier: GPL-2.0-only */
#include "ms912x_scan_methods.h"
#include "ms912x_color.h"

void scalar_blocks(const u32 *a, const u32 *b, unsigned w, unsigned stride, unsigned rows,
                   struct ms912x_scan_row_result *r) {
    for (unsigned y = 0; y < rows; y++)
        ms912x_scan_row_scalar(a + y * stride, b + y * stride, w, r + y);
}
void scalar_relaxed(const u32 *a, const u32 *b, u32 *n, unsigned w, unsigned stride, unsigned rows,
                    unsigned t, struct ms912x_scan_row_result *r) {
    for (unsigned y = 0; y < rows; y++)
        ms912x_scan_row_scalar_relaxed(a + y * stride, b + y * stride, n + y * stride, w, t, r + y);
}
void scalar_convert(void *d, const __le32 *s, const struct drm_rect *r) {
    u8 *out = d;
    unsigned w = drm_rect_width(r);
    for (int y = r->y1; y < r->y2; y++)
        for (unsigned x = 0; x < w; x += 2)
            put_unaligned_le32(ms912x_xrgb_pair_to_uyvy(s + y * 1920 + r->x1 + x),
                               out + ((y - r->y1) * w + x) * 2);
}
