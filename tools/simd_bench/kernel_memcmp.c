/* SPDX-License-Identifier: GPL-2.0-only */
/* Same word-at-a-time comparison as the running kernel's lib/string.c. */
#include <stddef.h>
__attribute__((__noinline__)) int review_kernel_memcmp(const void *left, const void *right,
                                                       size_t count) {
    const unsigned char *a = left, *b = right;
    while (count >= sizeof(unsigned long)) {
        unsigned long x, y;
        __builtin_memcpy(&x, a, sizeof(x));
        __builtin_memcpy(&y, b, sizeof(y));
        if (x != y)
            break;
        a += sizeof(x);
        b += sizeof(y);
        count -= sizeof(x);
    }
    for (size_t i = 0; i < count; i++) {
        int difference = a[i] - b[i];
        if (difference)
            return difference;
    }
    return 0;
}
