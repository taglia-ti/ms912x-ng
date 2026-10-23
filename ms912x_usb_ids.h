/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MS912X_USB_IDS_H
#define MS912X_USB_IDS_H

/* One catalog for kernel binding and userspace adapter discovery. */
#define MS912X_USB_DEVICES(X) \
    X(0x534d, 0x6021) \
    X(0x534d, 0x0821) \
    X(0x345f, 0x9132) \
    X(0x345f, 0x9133) \
    X(0x345f, 0x9135)

static inline int ms912x_usb_device_supported(unsigned int vendor, unsigned int product)
{
#define MS912X_USB_MATCH(v, p) if (vendor == (v) && product == (p)) return 1;
    MS912X_USB_DEVICES(MS912X_USB_MATCH)
#undef MS912X_USB_MATCH
    return 0;
}
#endif
