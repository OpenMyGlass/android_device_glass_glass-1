/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Glass power HAL logic, kept free of Android headers so tests/test_hal_logic.py can build
 * and run it on the host. Sources of every path and value: docs/hals.md.
 */
#ifndef GLASS_POWER_LOGIC_H
#define GLASS_POWER_LOGIC_H

#include <pthread.h>
#include <stddef.h>

#define GLASS_CPUFREQ_INTERACTIVE "/sys/devices/system/cpu/cpufreq/interactive/"
#define GLASS_CPUFREQ_CPU0 "/sys/devices/system/cpu/cpu0/cpufreq/"
#define GLASS_BOOSTPULSE GLASS_CPUFREQ_INTERACTIVE "boostpulse"
#define GLASS_SCALING_MAX_FREQ GLASS_CPUFREQ_CPU0 "scaling_max_freq"
#define GLASS_AVAILABLE_FREQS GLASS_CPUFREQ_CPU0 "scaling_available_frequencies"

/*
 * Screen-off cap on scaling_max_freq, in kHz, as a string, or NULL for no cap.
 * Disabled: neither stock XE24 nor glass-kernel's docs/tuning.md gives a screen-off value,
 * so choosing one is the owner's decision (docs/hals.md). When set, it must be one of
 * the kernel's OPPs; the HAL checks it against scaling_available_frequencies.
 */
#define GLASS_SCREEN_OFF_MAX_FREQ NULL

/*
 * 1 to send POWER_HINT_INTERACTION and POWER_HINT_VSYNC to boostpulse, 0 to ignore them.
 * Disabled: stock XE24 has no power HAL, so nothing pulses boostpulse there, and a pulse
 * raises the time spent at the top OPP (the governor jumps to hispeed_freq, the policy
 * maximum). Turning it on is the owner's decision (docs/hals.md).
 */
#define GLASS_BOOSTPULSE_ENABLED 0

#define GLASS_FREQ_LEN 16

/* One interactive governor tunable written at init, relative to GLASS_CPUFREQ_INTERACTIVE. */
struct glass_tunable {
    const char *name;
    const char *value;
};

extern const struct glass_tunable glass_tunables[];
extern const size_t glass_tunables_count;

struct glass_power {
    const char *root;               /* prefix for every sysfs path; "" on the device */
    const char *screen_off_max;     /* GLASS_SCREEN_OFF_MAX_FREQ, or a test value */
    int boost_enabled;              /* GLASS_BOOSTPULSE_ENABLED, or a test value */
    pthread_mutex_t lock;
    int boostpulse_fd;              /* -1 until opened */
    int boostpulse_warned;
    int screen_on;                  /* -1 until the first setInteractive */
    int capped;                     /* scaling_max_freq holds the screen-off cap */
    char saved_max[GLASS_FREQ_LEN]; /* scaling_max_freq before the cap */
};

#define GLASS_POWER_INITIALIZER(root, cap, boost) \
    { (root), (cap), (boost), PTHREAD_MUTEX_INITIALIZER, -1, 0, -1, 0, "" }

/* Joins root and path into buf. Returns 0, or -1 if it does not fit. */
int glass_path(char *buf, size_t size, const char *root, const char *path);

/* Reads a small sysfs file, trailing whitespace removed. Returns its length or -errno. */
int glass_read(const char *root, const char *path, char *buf, size_t size);

/* Writes a string to a sysfs file. Returns 0 or -errno. */
int glass_write(const char *root, const char *path, const char *value);

/* 1 if khz is one of the space-separated frequencies in list, else 0. */
int glass_freq_listed(const char *list, const char *khz);

/* Writes glass_tunables. Returns the number of writes that failed. */
int glass_power_apply_tunables(struct glass_power *p);

/* Screen on (1) or off (0): applies or removes the screen-off cap, if one is set. */
void glass_power_set_interactive(struct glass_power *p, int on);

/*
 * Writes "1" to boostpulse if boost_enabled; otherwise does nothing and returns 0.
 * Returns 0, or -errno if boostpulse is absent or the write failed.
 */
int glass_power_boost(struct glass_power *p);

#endif
