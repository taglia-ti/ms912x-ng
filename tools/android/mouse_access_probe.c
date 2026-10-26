/* A short, explicit shell-UID capability probe; never forwards or retains input. */
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/input.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define BIT_COUNT(maximum) (((maximum) + 1 + sizeof(unsigned long) * CHAR_BIT - 1) / (sizeof(unsigned long) * CHAR_BIT))

static bool bit(const unsigned long *bits, unsigned int index)
{
    return (bits[index / (CHAR_BIT * sizeof(*bits))] >> (index % (CHAR_BIT * sizeof(*bits)))) & 1UL;
}

static bool hex_id(const char *text, unsigned int *value)
{
    char *end = NULL;
    errno = 0;
    unsigned long parsed = strtoul(text, &end, 16);
    if (!*text || *end || errno || parsed > 0xffff) return false;
    *value = (unsigned int)parsed;
    return true;
}

int main(int argc, char **argv)
{
    unsigned int vendor, product;
    bool grab = argc == 5 && strcmp(argv[4], "--grab") == 0;
    if ((argc != 4 && !grab) || strncmp(argv[1], "/dev/input/event", 16) != 0
            || !argv[1][16] || strspn(argv[1] + 16, "0123456789") != strlen(argv[1] + 16)
            || !hex_id(argv[2], &vendor) || !hex_id(argv[3], &product)) {
        fprintf(stderr, "Usage: mouse_access_probe /dev/input/eventN VENDOR_HEX PRODUCT_HEX [--grab]\n");
        return 1;
    }
    if (getuid() != 2000) { fprintf(stderr, "Requires shell UID 2000; root is not supported\n"); return 1; }
    int fd = open(argv[1], O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) { perror("open input device"); return 1; }
    struct input_id id = {0};
    unsigned long rel[BIT_COUNT(REL_MAX)] = {0}, keys[BIT_COUNT(KEY_MAX)] = {0}, held[BIT_COUNT(KEY_MAX)] = {0};
    int result = 1;
    if (ioctl(fd, EVIOCGID, &id) < 0 || ioctl(fd, EVIOCGBIT(EV_REL, sizeof(rel)), rel) < 0
            || ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keys)), keys) < 0 || ioctl(fd, EVIOCGKEY(sizeof(held)), held) < 0) {
        perror("read input capabilities"); goto out;
    }
    if (id.vendor != vendor || id.product != product || !bit(rel, REL_X) || !bit(rel, REL_Y) || !bit(keys, BTN_LEFT)) {
        fprintf(stderr, "Device identity or relative-mouse capabilities do not match; refusing grab\n"); goto out;
    }
    for (unsigned int button = BTN_LEFT; button <= BTN_TASK; button++) {
        if (bit(held, button)) { fprintf(stderr, "Mouse button held; refusing grab\n"); goto out; }
    }
    printf("uid=2000 device=%s vendor=%04x product=%04x REL_X=1 REL_Y=1 BTN_LEFT=1\n", argv[1], id.vendor, id.product);
    if (grab) {
        if (ioctl(fd, EVIOCGRAB, 1) < 0) { perror("EVIOCGRAB acquire"); result = 2; goto out; }
        puts("EVIOCGRAB acquired");
        if (ioctl(fd, EVIOCGRAB, 0) < 0) { perror("EVIOCGRAB release"); goto out; }
        puts("EVIOCGRAB released");
    }
    result = 0;
out:
    close(fd); /* Closing also releases any grab after a failed explicit release. */
    return result;
}
