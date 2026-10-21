/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Glass lights HAL logic, kept free of Android headers so tests/test_hal_logic.py can build
 * and run it on the host. Sources: docs/hals.md.
 */
#ifndef GLASS_LIGHTS_LOGIC_H
#define GLASS_LIGHTS_LOGIC_H

/* The panel driver's brightness attribute (panel-notle-dpi.c registers it under manager2). */
#define GLASS_PANEL_BRIGHTNESS "/sys/devices/platform/omapdss/manager2/panel-notle-dpi/brightness"

/*
 * Top of the 0-255 scale that the brightness attribute takes: range_expand and range_reduce
 * (panel-notle-dpi.c:934-949) convert between it and the driver's internal value, and stock
 * init writes up to 255. (MAX_BRIGHTNESS, 0xFF at :49, is the internal default of
 * brightness_limit, :2177, which caps the light at about 223 on this scale.)
 */
#define GLASS_PANEL_MAX_BRIGHTNESS 255

/* Perceived brightness (0-255) of an ARGB color; alpha is ignored. */
int glass_rgb_to_brightness(unsigned int color);

/* Android brightness (0-255) scaled to the panel's 0..GLASS_PANEL_MAX_BRIGHTNESS. */
int glass_panel_brightness(int brightness);

/* Writes a decimal value to path. Returns 0 or -errno. */
int glass_write_int(const char *path, int value);

#endif
