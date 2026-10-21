/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Power HAL for Glass Explorer Edition (power.glass_1), legacy hardware/power.h interface.
 * Builds against android-6.0.1_r31 and android-5.1.1_r37 headers, whose power.h only
 * differ in a comment (POWER_HINT_INTERACTION's data is a duration from 6.0 on; it is
 * not used here).
 */
#define LOG_TAG "power.glass_1"

#include <cutils/log.h>
#include <hardware/hardware.h>
#include <hardware/power.h>

#include "power_logic.h"

struct glass_power_module {
    struct power_module base;
    struct glass_power state;
};

static struct glass_power *state_of(struct power_module *module)
{
    return &((struct glass_power_module *)module)->state;
}

static void glass_power_init(struct power_module *module)
{
    int failed = glass_power_apply_tunables(state_of(module));
    if (failed)
        ALOGE("%d interactive governor tunables not set; is the governor interactive?", failed);
    else
        ALOGI("interactive governor set to the stock XE24 values");
}

static void glass_power_set_interactive_hal(struct power_module *module, int on)
{
    glass_power_set_interactive(state_of(module), on);
}

static void glass_power_hint(struct power_module *module, power_hint_t hint, void *data)
{
    switch (hint) {
    case POWER_HINT_INTERACTION:
        glass_power_boost(state_of(module));
        break;
    case POWER_HINT_VSYNC:
        /* data is non-NULL when an app starts requesting VSYNC, NULL when it stops. */
        if (data)
            glass_power_boost(state_of(module));
        break;
    default:
        break;
    }
}

static struct hw_module_methods_t power_module_methods = {
    .open = NULL,
};

struct glass_power_module HAL_MODULE_INFO_SYM = {
    .base = {
        .common = {
            .tag = HARDWARE_MODULE_TAG,
            .module_api_version = POWER_MODULE_API_VERSION_0_2,
            .hal_api_version = HARDWARE_HAL_API_VERSION,
            .id = POWER_HARDWARE_MODULE_ID,
            .name = "Glass power HAL",
            .author = "OpenMyGlass",
            .methods = &power_module_methods,
        },
        .init = glass_power_init,
        .setInteractive = glass_power_set_interactive_hal,
        .powerHint = glass_power_hint,
    },
    .state = GLASS_POWER_INITIALIZER("", GLASS_SCREEN_OFF_MAX_FREQ),
};
