/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MS912X_RECT_LIMITS_H
#define MS912X_RECT_LIMITS_H

#define MS912X_MAX_DAMAGE_RECTS 8192
/* Current damage, previous damage replay, and the cursor overlay. */
#define MS912X_MAX_BATCH_RECTS (MS912X_MAX_DAMAGE_RECTS * 2 + 1)

#endif
