/* SPDX-License-Identifier: Apache-2.0 */
#define LOG_TAG "power.glass_1"

#include "power_logic.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef GLASS_HOST_TEST
/* Test files are regular files, which unlike sysfs attributes keep old bytes past a write. */
#define GLASS_WRITE_FLAGS (O_WRONLY | O_TRUNC)
#define GLASS_LOGE(...) (fprintf(stderr, LOG_TAG ": " __VA_ARGS__), fputc('\n', stderr))
#define GLASS_LOGI(...) ((void)0)
#else
#define GLASS_WRITE_FLAGS O_WRONLY
#include <cutils/log.h>
#define GLASS_LOGE(...) ALOGE(__VA_ARGS__)
#define GLASS_LOGI(...) ALOGI(__VA_ARGS__)
#endif

/*
 * The stock XE24 interactive governor settings: the kernel defaults, since the stock
 * init.omap4430.rc has its own tuning commented out and stock init.rc only sets owners and modes.
 * kernel-omap glass-omap-xrx13b (989d925) drivers/cpufreq/cpufreq_interactive.c; the same
 * values are the "stock" profile of glass-kernel docs/tuning.md. hispeed_freq is left out:
 * the kernel sets it to the policy maximum when the governor starts (line 927).
 */
const struct glass_tunable glass_tunables[] = {
    { "timer_rate", "20000" },          /* DEFAULT_TIMER_RATE, line 88 */
    { "min_sample_time", "80000" },     /* DEFAULT_MIN_SAMPLE_TIME, line 82 */
    { "go_hispeed_load", "99" },        /* DEFAULT_GO_HISPEED_LOAD, line 69 */
    { "target_loads", "90" },           /* DEFAULT_TARGET_LOAD, line 73 */
    { "above_hispeed_delay", "20000" }, /* DEFAULT_ABOVE_HISPEED_DELAY, line 95 */
    { "boostpulse_duration", "80000" }, /* DEFAULT_MIN_SAMPLE_TIME, line 101 */
};
const size_t glass_tunables_count = sizeof(glass_tunables) / sizeof(glass_tunables[0]);

int glass_path(char *buf, size_t size, const char *root, const char *path)
{
    int n = snprintf(buf, size, "%s%s", root, path);
    return (n < 0 || (size_t)n >= size) ? -1 : 0;
}

int glass_read(const char *root, const char *path, char *buf, size_t size)
{
    char full[256];
    ssize_t n;
    int fd;

    if (size == 0 || glass_path(full, sizeof(full), root, path))
        return -ENAMETOOLONG;
    fd = open(full, O_RDONLY);
    if (fd < 0)
        return -errno;
    n = read(fd, buf, size - 1);
    if (n < 0) {
        int err = errno;
        close(fd);
        return -err;
    }
    close(fd);
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == ' ' || buf[n - 1] == '\t'))
        n--;
    buf[n] = '\0';
    return (int)n;
}

int glass_write(const char *root, const char *path, const char *value)
{
    char full[256];
    size_t len = strlen(value);
    ssize_t n;
    int fd, err = 0;

    if (glass_path(full, sizeof(full), root, path))
        return -ENAMETOOLONG;
    fd = open(full, GLASS_WRITE_FLAGS);
    if (fd < 0)
        return -errno;
    n = write(fd, value, len);
    if (n < 0)
        err = -errno;
    else if ((size_t)n != len)
        err = -EIO;
    close(fd);
    return err;
}

int glass_freq_listed(const char *list, const char *khz)
{
    size_t len = strlen(khz);
    const char *p = list;

    if (len == 0)
        return 0;
    while (*p) {
        size_t tok;
        while (*p == ' ' || *p == '\t' || *p == '\n')
            p++;
        tok = strcspn(p, " \t\n");
        if (tok == len && strncmp(p, khz, len) == 0)
            return 1;
        p += tok;
    }
    return 0;
}

int glass_power_apply_tunables(struct glass_power *p)
{
    char path[128];
    size_t i;
    int failed = 0;

    for (i = 0; i < glass_tunables_count; i++) {
        int r;
        snprintf(path, sizeof(path), "%s%s", GLASS_CPUFREQ_INTERACTIVE, glass_tunables[i].name);
        r = glass_write(p->root, path, glass_tunables[i].value);
        if (r) {
            GLASS_LOGE("cannot write %s to %s: %s", glass_tunables[i].value, path, strerror(-r));
            failed++;
        }
    }
    return failed;
}

/* Called with p->lock held. */
static void apply_cap(struct glass_power *p)
{
    char avail[256], cur[GLASS_FREQ_LEN];
    int r;

    r = glass_read(p->root, GLASS_AVAILABLE_FREQS, avail, sizeof(avail));
    if (r <= 0 || !glass_freq_listed(avail, p->screen_off_max)) {
        GLASS_LOGE("screen-off cap %s is not an available frequency; not capping",
                   p->screen_off_max);
        return;
    }
    r = glass_read(p->root, GLASS_SCALING_MAX_FREQ, cur, sizeof(cur));
    if (r <= 0) {
        GLASS_LOGE("cannot read scaling_max_freq; not capping");
        return;
    }
    /* Never raise the maximum: a lower one set by someone else stays. */
    if (strtoul(cur, NULL, 10) <= strtoul(p->screen_off_max, NULL, 10))
        return;
    r = glass_write(p->root, GLASS_SCALING_MAX_FREQ, p->screen_off_max);
    if (r) {
        GLASS_LOGE("cannot cap scaling_max_freq: %s", strerror(-r));
        return;
    }
    memcpy(p->saved_max, cur, sizeof(p->saved_max));
    p->capped = 1;
}

/* Called with p->lock held. */
static void remove_cap(struct glass_power *p)
{
    char cur[GLASS_FREQ_LEN];
    int r;

    p->capped = 0;
    /* If someone else changed the maximum while capped, keep their value. */
    r = glass_read(p->root, GLASS_SCALING_MAX_FREQ, cur, sizeof(cur));
    if (r <= 0 || strcmp(cur, p->screen_off_max) != 0)
        return;
    r = glass_write(p->root, GLASS_SCALING_MAX_FREQ, p->saved_max);
    if (r)
        GLASS_LOGE("cannot restore scaling_max_freq %s: %s", p->saved_max, strerror(-r));
}

void glass_power_set_interactive(struct glass_power *p, int on)
{
    on = on ? 1 : 0;
    pthread_mutex_lock(&p->lock);
    if (on != p->screen_on) {
        p->screen_on = on;
        if (p->screen_off_max) {
            if (!on && !p->capped)
                apply_cap(p);
            else if (on && p->capped)
                remove_cap(p);
        }
    }
    pthread_mutex_unlock(&p->lock);
}

int glass_power_boost(struct glass_power *p)
{
    int err = 0;

    if (!p->boost_enabled)
        return 0;
    pthread_mutex_lock(&p->lock);
    if (p->boostpulse_fd < 0) {
        char full[256];
        if (glass_path(full, sizeof(full), p->root, GLASS_BOOSTPULSE) != 0)
            err = -ENAMETOOLONG;
        else if ((p->boostpulse_fd = open(full, O_WRONLY)) < 0)
            err = -errno;
        if (err && !p->boostpulse_warned) {
            GLASS_LOGE("cannot open %s: %s", GLASS_BOOSTPULSE, strerror(-err));
            p->boostpulse_warned = 1;
        }
    }
    if (p->boostpulse_fd >= 0) {
        ssize_t n = write(p->boostpulse_fd, "1", 1);
        if (n != 1) {
            err = n < 0 ? -errno : -EIO;
            /* The governor may have changed and removed the file: reopen next time. */
            close(p->boostpulse_fd);
            p->boostpulse_fd = -1;
        }
    }
    pthread_mutex_unlock(&p->lock);
    return err;
}
