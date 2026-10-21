/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Lights HAL for Glass Explorer Edition (lights.glass_1), legacy hardware/lights.h
 * interface; lights.h is the same in android-4.4.4_r2 and android-6.0.1_r31.
 * Glass has one light: the display backlight. The kernel builds no LED class devices
 * (notle_defconfig: CONFIG_NEW_LEDS is not set), so every other light id is refused.
 */
#define LOG_TAG "lights.glass_1"

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include <cutils/log.h>
#include <hardware/hardware.h>
#include <hardware/lights.h>

#include "lights_logic.h"

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

static int set_light_backlight(struct light_device_t *dev, struct light_state_t const *state)
{
    int value, err;

    if (!dev || !state)
        return -EINVAL;
    value = glass_panel_brightness(glass_rgb_to_brightness(state->color));
    pthread_mutex_lock(&g_lock);
    err = glass_write_int(GLASS_PANEL_BRIGHTNESS, value);
    pthread_mutex_unlock(&g_lock);
    if (err)
        ALOGE("cannot write %d to %s: %s", value, GLASS_PANEL_BRIGHTNESS, strerror(-err));
    return err;
}

static int close_lights(struct hw_device_t *dev)
{
    free(dev);
    return 0;
}

static int open_lights(const struct hw_module_t *module, const char *name,
                       struct hw_device_t **device)
{
    struct light_device_t *dev;

    if (!device)
        return -EINVAL;
    *device = NULL;
    if (!name || strcmp(name, LIGHT_ID_BACKLIGHT) != 0)
        return -EINVAL;

    dev = calloc(1, sizeof(*dev));
    if (!dev)
        return -ENOMEM;
    dev->common.tag = HARDWARE_DEVICE_TAG;
    dev->common.version = 0;
    dev->common.module = (struct hw_module_t *)module;
    dev->common.close = close_lights;
    dev->set_light = set_light_backlight;
    *device = (struct hw_device_t *)dev;
    return 0;
}

static struct hw_module_methods_t lights_module_methods = {
    .open = open_lights,
};

struct hw_module_t HAL_MODULE_INFO_SYM = {
    .tag = HARDWARE_MODULE_TAG,
    .module_api_version = HARDWARE_MODULE_API_VERSION(1, 0),
    .hal_api_version = HARDWARE_HAL_API_VERSION,
    .id = LIGHTS_HARDWARE_MODULE_ID,
    .name = "Glass lights HAL",
    .author = "OpenMyGlass",
    .methods = &lights_module_methods,
};
