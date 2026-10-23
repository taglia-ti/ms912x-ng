/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MS912X_CONTROL_CORE_H
#define MS912X_CONTROL_CORE_H
#include "../ms912x_protocol_values.h"

static inline void ms912x_build_control_write(unsigned char report[8],
    unsigned char command, const unsigned char data[6])
{
    unsigned int i;
    report[0] = MS912X_REQ_TYPE_WRITE_6_BYTES;
    report[1] = command;
    for (i = 0; i < 6; ++i) report[i + 2] = data[i];
}

static inline void ms912x_build_register_read(unsigned char report[8], unsigned int address)
{
    unsigned int i;
    report[0] = MS912X_REQ_TYPE_READ_BYTE;
    report[1] = address >> 8;
    report[2] = address;
    for (i = 3; i < 8; ++i) report[i] = 0;
}

static inline void ms912x_build_flash_read(unsigned char report[8], unsigned int address)
{
    unsigned int i;
    report[0] = MS912X_REQ_TYPE_READ_FLASH;
    report[1] = address >> 16; report[2] = address >> 8; report[3] = address;
    for (i = 4; i < 8; ++i) report[i] = 0;
}

struct ms912x_control_ops {
    int (*write)(void *context, unsigned char command, const unsigned char data[6]);
    int (*read)(void *context, unsigned int address);
};

/* The Linux manual-block modeset sequence; transport and locking stay with callers. */
static inline int ms912x_program_mode(void *context, const struct ms912x_control_ops *ops,
    unsigned int vic, unsigned int width, unsigned int height)
{
    unsigned char data[6] = {0};
    int ret;
    ret = ops->write(context, MS912X_CMD_STARTTRANS, data);
    if (ret < 0) return ret;
    ret = ops->read(context, MS912X_REG_OUTPUT_FORMAT);
    if (ret < 0) return ret;
    ret = ops->read(context, MS912X_REG_POWER_STATE);
    if (ret < 0) return ret;
    data[0] = MS912X_TRANSMODE_MANUAL_BLOCK;
    ret = ops->write(context, MS912X_CMD_TRANSMODE, data);
    if (ret < 0) return ret;
    data[0] = width >> 8; data[1] = width;
    data[2] = height >> 8; data[3] = height;
    data[4] = MS912X_PIXFMT_UYVY; data[5] = MS912X_BYTE_SELECT_UYVY;
    ret = ops->write(context, MS912X_CMD_VIDEO_IN, data);
    if (ret < 0) return ret;
    data[0] = vic; data[1] = 1;
    data[2] = width >> 8; data[3] = width;
    data[4] = height >> 8; data[5] = height;
    ret = ops->write(context, MS912X_CMD_VIDEO_OUT, data);
    if (ret < 0) return ret;
    data[0] = 1; data[1] = data[2] = data[3] = data[4] = data[5] = 0;
    ret = ops->write(context, MS912X_CMD_STARTTRANS, data);
    if (ret < 0) return ret;
    return ops->write(context, MS912X_CMD_VIDEOON, data);
}
#endif
