/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Host tests for the pure logic of the power and lights HALs. Built and run by
 * tests/test_hal_logic.py with: cc -DGLASS_HOST_TEST power/power_logic.c
 * lights/lights_logic.c tests/hal_logic_test.c. argv[1] is an empty scratch directory.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "../lights/lights_logic.h"
#include "../power/power_logic.h"

static int failures;
static const char *tmp;

#define CHECK(cond) do { \
        if (!(cond)) { \
            fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

static void mkdirs(const char *root, const char *path)
{
    char buf[512];
    size_t i;

    snprintf(buf, sizeof(buf), "%s%s", root, path);
    for (i = strlen(root) + 1; buf[i]; i++) {
        if (buf[i] == '/') {
            buf[i] = '\0';
            mkdir(buf, 0755);
            buf[i] = '/';
        }
    }
}

static void put(const char *root, const char *path, const char *text)
{
    char buf[512];
    FILE *f;

    mkdirs(root, path);
    snprintf(buf, sizeof(buf), "%s%s", root, path);
    f = fopen(buf, "w");
    if (!f) {
        perror(buf);
        exit(2);
    }
    fputs(text, f);
    fclose(f);
}

static const char *get(const char *root, const char *path)
{
    static char buf[256];
    int n = glass_read(root, path, buf, sizeof(buf));
    return n < 0 ? "<missing>" : buf;
}

static char *new_root(const char *name)
{
    static char roots[8][256];
    static int next;
    char *r = roots[next++ % 8];

    snprintf(r, sizeof(roots[0]), "%s/%s", tmp, name);
    mkdir(r, 0755);
    return r;
}

static void test_lights(void)
{
    char path[512];
    const char *root = new_root("lights");

    CHECK(glass_rgb_to_brightness(0xffffffff) == 255);
    CHECK(glass_rgb_to_brightness(0x00ffffff) == 255); /* alpha ignored */
    CHECK(glass_rgb_to_brightness(0xff000000) == 0);
    CHECK(glass_rgb_to_brightness(0xff808080) == 128);
    CHECK(glass_rgb_to_brightness(0xffff0000) == 76);
    CHECK(glass_rgb_to_brightness(0xff00ff00) == 149);
    CHECK(glass_rgb_to_brightness(0xff0000ff) == 28);

    CHECK(glass_panel_brightness(-5) == 0);
    CHECK(glass_panel_brightness(0) == 0);
    CHECK(glass_panel_brightness(1) == 1);
    CHECK(glass_panel_brightness(100) == 100);
    CHECK(glass_panel_brightness(255) == GLASS_PANEL_MAX_BRIGHTNESS);
    CHECK(glass_panel_brightness(4000) == GLASS_PANEL_MAX_BRIGHTNESS);

    put(root, GLASS_PANEL_BRIGHTNESS, "0");
    snprintf(path, sizeof(path), "%s%s", root, GLASS_PANEL_BRIGHTNESS);
    CHECK(glass_write_int(path, 100) == 0);
    CHECK(strcmp(get(root, GLASS_PANEL_BRIGHTNESS), "100") == 0);
    snprintf(path, sizeof(path), "%s/absent/brightness", root);
    CHECK(glass_write_int(path, 1) == -ENOENT);
}

static void test_tunables(void)
{
    /* The interactive governor's attributes: cpufreq_interactive.c lines 871-881. */
    static const char *const known[] = {
        "target_loads", "hispeed_freq", "go_hispeed_load", "above_hispeed_delay",
        "min_sample_time", "timer_rate", "timer_slack", "boost", "boostpulse",
        "boostpulse_duration",
    };
    const char *root = new_root("tunables");
    char path[256];
    size_t i, j;

    CHECK(glass_tunables_count == 6);
    for (i = 0; i < glass_tunables_count; i++) {
        int found = 0;
        for (j = 0; j < sizeof(known) / sizeof(known[0]); j++)
            found |= strcmp(glass_tunables[i].name, known[j]) == 0;
        CHECK(found);
        /* Never a frequency, and never the pulse/boost triggers. */
        CHECK(strcmp(glass_tunables[i].name, "hispeed_freq") != 0);
        CHECK(strcmp(glass_tunables[i].name, "boost") != 0);
        CHECK(strcmp(glass_tunables[i].name, "boostpulse") != 0);
        CHECK(strspn(glass_tunables[i].value, "0123456789") == strlen(glass_tunables[i].value));
        for (j = 0; j < i; j++)
            CHECK(strcmp(glass_tunables[i].name, glass_tunables[j].name) != 0);
    }

    {
        struct glass_power p = GLASS_POWER_INITIALIZER(root, NULL);
        /* The governor directory is absent: every write fails, nothing crashes. */
        CHECK(glass_power_apply_tunables(&p) == (int)glass_tunables_count);
        for (i = 0; i < glass_tunables_count; i++) {
            snprintf(path, sizeof(path), "%s%s", GLASS_CPUFREQ_INTERACTIVE, glass_tunables[i].name);
            put(root, path, "x");
        }
        CHECK(glass_power_apply_tunables(&p) == 0);
        CHECK(strcmp(get(root, GLASS_CPUFREQ_INTERACTIVE "timer_rate"), "20000") == 0);
        CHECK(strcmp(get(root, GLASS_CPUFREQ_INTERACTIVE "min_sample_time"), "80000") == 0);
        CHECK(strcmp(get(root, GLASS_CPUFREQ_INTERACTIVE "go_hispeed_load"), "99") == 0);
        CHECK(strcmp(get(root, GLASS_CPUFREQ_INTERACTIVE "target_loads"), "90") == 0);
        CHECK(strcmp(get(root, GLASS_CPUFREQ_INTERACTIVE "above_hispeed_delay"), "20000") == 0);
        CHECK(strcmp(get(root, GLASS_CPUFREQ_INTERACTIVE "boostpulse_duration"), "80000") == 0);
    }
}

static void test_paths(void)
{
    char buf[32], longroot[300];

    CHECK(glass_path(buf, sizeof(buf), "/r", "/a") == 0 && strcmp(buf, "/r/a") == 0);
    memset(longroot, 'x', sizeof(longroot) - 1);
    longroot[sizeof(longroot) - 1] = '\0';
    CHECK(glass_path(buf, sizeof(buf), longroot, "/a") == -1);
    CHECK(glass_write(longroot, "/a", "1") == -ENAMETOOLONG);
    CHECK(glass_read(longroot, "/a", buf, sizeof(buf)) == -ENAMETOOLONG);

    CHECK(glass_freq_listed("300000 600000 800000 1008000 \n", "600000"));
    CHECK(glass_freq_listed("300000 600000 800000 1008000 \n", "1008000"));
    CHECK(!glass_freq_listed("300000 600000 800000 1008000 \n", "60000"));
    CHECK(!glass_freq_listed("300000 600000 800000 1008000 \n", "1200000"));
    CHECK(!glass_freq_listed("300000 600000", ""));
    CHECK(!glass_freq_listed("", "300000"));
}

#define FREQS "300000 600000 800000 1008000 \n"
/* Test-only cap: one of the existing OPPs; the device build has no cap. */
#define TEST_CAP "600000"

static void test_screen_off_cap(void)
{
    const char *root;

    CHECK(GLASS_SCREEN_OFF_MAX_FREQ == NULL);

    /* No cap configured: setInteractive changes nothing. */
    root = new_root("nocap");
    put(root, GLASS_AVAILABLE_FREQS, FREQS);
    put(root, GLASS_SCALING_MAX_FREQ, "1008000\n");
    {
        struct glass_power p = GLASS_POWER_INITIALIZER(root, GLASS_SCREEN_OFF_MAX_FREQ);
        glass_power_set_interactive(&p, 0);
        CHECK(strcmp(get(root, GLASS_SCALING_MAX_FREQ), "1008000") == 0);
        glass_power_set_interactive(&p, 1);
        CHECK(strcmp(get(root, GLASS_SCALING_MAX_FREQ), "1008000") == 0);
    }

    /* Cap applied on screen off, restored on screen on, repeated calls harmless. */
    root = new_root("cap");
    put(root, GLASS_AVAILABLE_FREQS, FREQS);
    put(root, GLASS_SCALING_MAX_FREQ, "1008000\n");
    {
        struct glass_power p = GLASS_POWER_INITIALIZER(root, TEST_CAP);
        glass_power_set_interactive(&p, 1);
        CHECK(strcmp(get(root, GLASS_SCALING_MAX_FREQ), "1008000") == 0);
        glass_power_set_interactive(&p, 0);
        CHECK(strcmp(get(root, GLASS_SCALING_MAX_FREQ), TEST_CAP) == 0);
        glass_power_set_interactive(&p, 0);
        CHECK(strcmp(get(root, GLASS_SCALING_MAX_FREQ), TEST_CAP) == 0);
        glass_power_set_interactive(&p, 1);
        CHECK(strcmp(get(root, GLASS_SCALING_MAX_FREQ), "1008000") == 0);
        glass_power_set_interactive(&p, 1);
        CHECK(strcmp(get(root, GLASS_SCALING_MAX_FREQ), "1008000") == 0);

        /* Someone changes the maximum while capped: their value is kept. */
        glass_power_set_interactive(&p, 0);
        put(root, GLASS_SCALING_MAX_FREQ, "300000\n");
        glass_power_set_interactive(&p, 1);
        CHECK(strcmp(get(root, GLASS_SCALING_MAX_FREQ), "300000") == 0);

        /* A maximum already below the cap is never raised. */
        glass_power_set_interactive(&p, 0);
        CHECK(strcmp(get(root, GLASS_SCALING_MAX_FREQ), "300000") == 0);
        glass_power_set_interactive(&p, 1);
        CHECK(strcmp(get(root, GLASS_SCALING_MAX_FREQ), "300000") == 0);
    }

    /* A cap that is not an OPP is refused. */
    root = new_root("badcap");
    put(root, GLASS_AVAILABLE_FREQS, FREQS);
    put(root, GLASS_SCALING_MAX_FREQ, "1008000\n");
    {
        struct glass_power p = GLASS_POWER_INITIALIZER(root, "700000");
        glass_power_set_interactive(&p, 0);
        CHECK(strcmp(get(root, GLASS_SCALING_MAX_FREQ), "1008000") == 0);
    }

    /* No frequency table: no cap. */
    root = new_root("notable");
    put(root, GLASS_SCALING_MAX_FREQ, "1008000\n");
    {
        struct glass_power p = GLASS_POWER_INITIALIZER(root, TEST_CAP);
        glass_power_set_interactive(&p, 0);
        CHECK(strcmp(get(root, GLASS_SCALING_MAX_FREQ), "1008000") == 0);
    }
}

static void test_boost(void)
{
    const char *root = new_root("boost");
    struct glass_power p = GLASS_POWER_INITIALIZER(root, NULL);

    CHECK(glass_power_boost(&p) == -ENOENT);
    CHECK(p.boostpulse_warned == 1);
    CHECK(glass_power_boost(&p) == -ENOENT);
    put(root, GLASS_BOOSTPULSE, "");
    CHECK(glass_power_boost(&p) == 0);
    CHECK(p.boostpulse_fd >= 0);
    {
        int fd = p.boostpulse_fd;
        CHECK(glass_power_boost(&p) == 0);
        CHECK(p.boostpulse_fd == fd); /* opened once */
    }
    CHECK(strcmp(get(root, GLASS_BOOSTPULSE), "11") == 0);
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s <empty scratch dir>\n", argv[0]);
        return 2;
    }
    tmp = argv[1];
    test_lights();
    test_tunables();
    test_paths();
    test_screen_off_cap();
    test_boost();
    if (failures)
        fprintf(stderr, "%d checks failed\n", failures);
    else
        printf("all checks passed\n");
    return failures ? 1 : 0;
}
