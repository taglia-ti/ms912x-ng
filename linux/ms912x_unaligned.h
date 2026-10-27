/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MS912X_UNALIGNED_H
#define MS912X_UNALIGNED_H

#include <linux/version.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
#include <linux/unaligned.h>
#else
#include <asm/unaligned.h>
#endif

#endif /* MS912X_UNALIGNED_H */
