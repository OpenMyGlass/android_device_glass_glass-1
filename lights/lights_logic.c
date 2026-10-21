/* SPDX-License-Identifier: Apache-2.0 */
#include "lights_logic.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int glass_rgb_to_brightness(unsigned int color)
{
    unsigned int r = (color >> 16) & 0xff;
    unsigned int g = (color >> 8) & 0xff;
    unsigned int b = color & 0xff;

    /* Rec. 601 luma weights in 1/256ths (77 + 150 + 29 = 256): white gives 255. */
    return (int)((77 * r + 150 * g + 29 * b) >> 8);
}

int glass_panel_brightness(int brightness)
{
    if (brightness < 0)
        brightness = 0;
    if (brightness > 255)
        brightness = 255;
    return (brightness * GLASS_PANEL_MAX_BRIGHTNESS + 127) / 255;
}

int glass_write_int(const char *path, int value)
{
    char buf[16];
    int len = snprintf(buf, sizeof(buf), "%d\n", value);
    ssize_t n;
    int fd, err = 0;

    fd = open(path, O_WRONLY);
    if (fd < 0)
        return -errno;
    n = write(fd, buf, (size_t)len);
    if (n < 0)
        err = -errno;
    else if (n != len)
        err = -EIO;
    close(fd);
    return err;
}
