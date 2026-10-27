/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MS912X_MAP_COMPAT_H
#define MS912X_MAP_COMPAT_H

#include <linux/version.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 18, 0)
#include <linux/iosys-map.h>
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(5, 11, 0)
#include <linux/dma-buf-map.h>
#include <linux/io.h>
#include <linux/string.h>

#define iosys_map dma_buf_map

static inline void
ms912x_map_memcpy_from_dma_buf_map(void *dst, const struct dma_buf_map *src,
				  size_t offset, size_t len)
{
	if (src->is_iomem)
		memcpy_fromio(dst, (u8 __iomem *)src->vaddr_iomem + offset, len);
	else
		memcpy(dst, (u8 *)src->vaddr + offset, len);
}
#define iosys_map_memcpy_from ms912x_map_memcpy_from_dma_buf_map
#else
#include <linux/io.h>
#include <linux/string.h>

struct ms912x_legacy_iosys_map {
	void *vaddr;
};
#define iosys_map ms912x_legacy_iosys_map

static inline void
ms912x_map_memcpy_from_legacy_map(void *dst,
				  const struct ms912x_legacy_iosys_map *src,
				  size_t offset, size_t len)
{
	memcpy(dst, (u8 *)src->vaddr + offset, len);
}
#define iosys_map_memcpy_from ms912x_map_memcpy_from_legacy_map
#endif

#endif /* MS912X_MAP_COMPAT_H */
