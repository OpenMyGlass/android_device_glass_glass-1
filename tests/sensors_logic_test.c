/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Host tests for the pure logic of the sensors HAL. Built and run by tests/test_hal_logic.py
 * with: cc -DGLASS_HOST_TEST sensors/sensors_logic.c tests/sensors_logic_test.c -lm.
 * argv[1] is an empty scratch directory. Every record and sysfs file here is synthetic,
 * built from the layout in docs/specs/sensors.md §4; none comes from a device.
 */
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "../sensors/sensors_logic.h"

static int failures;
static const char *tmp;

#define CHECK(cond) do { \
        if (!(cond)) { \
            fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
            failures++; \
        } \
    } while (0)
#define NEAR(a, b, eps) (fabs((double)(a) - (double)(b)) <= (eps))

static void mkdirs(const char *path)
{
    char buf[512];
    size_t i;

    snprintf(buf, sizeof(buf), "%s", path);
    for (i = 1; buf[i]; i++) {
        if (buf[i] == '/') {
            buf[i] = '\0';
            mkdir(buf, 0755);
            buf[i] = '/';
        }
    }
    mkdir(buf, 0755);
}

static void put(const char *root, const char *path, const char *text)
{
    char buf[512];
    FILE *f;

    snprintf(buf, sizeof(buf), "%s%s", root, path);
    *strrchr(buf, '/') = '\0';
    mkdirs(buf);
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
    return sns_read_file(root, path, buf, sizeof(buf)) < 0 ? "<missing>" : buf;
}

static char *new_root(const char *name)
{
    static char roots[8][256];
    static int next;
    char *r = roots[next++ % 8];

    snprintf(r, 256, "%s/%s", tmp, name);
    mkdirs(r);
    return r;
}

/* ---- Sensor table -------------------------------------------------------------------- */

static void test_list(void)
{
    struct sns_desc copy[SNS_NUM_SENSORS];
    char why[128];
    int i;

    CHECK(sns_list_check(sns_sensors, SNS_NUM_SENSORS, why, sizeof(why)) == 0);
    for (i = 1; i <= SNS_HANDLE_MAX; i++)
        CHECK(sns_find(i) && sns_find(i)->handle == i);
    CHECK(sns_find(0) == NULL);
    CHECK(sns_find(SNS_HANDLE_MAX + 1) == NULL);

    /* Spec §7 values. */
    CHECK(NEAR(sns_find(SNS_HANDLE_ACCEL)->max_range, 19.6133, 1e-4));
    CHECK(NEAR(sns_find(SNS_HANDLE_GYRO)->max_range, 34.906586, 1e-5));
    CHECK(NEAR(sns_find(SNS_HANDLE_GYRO)->resolution, 0.0010652645, 1e-9));
    CHECK(NEAR(sns_find(SNS_HANDLE_MAG)->max_range, 1200.0, 1e-3));
    CHECK(sns_find(SNS_HANDLE_MAG)->min_delay_us == 10000);
    CHECK(sns_find(SNS_HANDLE_ACCEL)->min_delay_us == 5000);
    CHECK(sns_find(SNS_HANDLE_LIGHT)->min_delay_us == 0);
    CHECK(sns_find(SNS_HANDLE_LIGHT)->type == SNS_TYPE_LIGHT);
    /* No fused types: the framework provides them. */
    for (i = 0; i < SNS_NUM_SENSORS; i++)
        CHECK(sns_sensors[i].type != 3 && sns_sensors[i].type != 9 &&
              sns_sensors[i].type != 10 && sns_sensors[i].type != 11);

    memcpy(copy, sns_sensors, sizeof(copy));
    copy[1].handle = copy[0].handle;
    CHECK(sns_list_check(copy, SNS_NUM_SENSORS, why, sizeof(why)) != 0);
    memcpy(copy, sns_sensors, sizeof(copy));
    copy[3].min_delay_us = 1000; /* light is on-change */
    CHECK(sns_list_check(copy, SNS_NUM_SENSORS, why, sizeof(why)) != 0);
    memcpy(copy, sns_sensors, sizeof(copy));
    copy[0].resolution = 0;
    CHECK(sns_list_check(copy, SNS_NUM_SENSORS, why, sizeof(why)) != 0);
    memcpy(copy, sns_sensors, sizeof(copy));
    copy[0].max_delay_us = 1000; /* below min */
    CHECK(sns_list_check(copy, SNS_NUM_SENSORS, why, sizeof(why)) != 0);
    /* An uncalibrated sensor without its calibrated twin. */
    CHECK(sns_list_check(sns_sensors + 4, 2, why, sizeof(why)) != 0);
    memcpy(copy, sns_sensors, sizeof(copy));
    copy[0].type = 99;
    CHECK(sns_list_check(copy, SNS_NUM_SENSORS, why, sizeof(why)) != 0);
}

/* ---- Configuration and plan ---------------------------------------------------------- */

static void config(int h1, int64_t d1, int h2, int64_t d2, struct sns_iio_config *cfg)
{
    int en[SNS_HANDLE_MAX + 1] = { 0 };
    int64_t delay[SNS_HANDLE_MAX + 1];
    int i;

    for (i = 0; i <= SNS_HANDLE_MAX; i++)
        delay[i] = 200000000;
    if (h1) { en[h1] = 1; delay[h1] = d1; }
    if (h2) { en[h2] = 1; delay[h2] = d2; }
    sns_iio_config_for(en, delay, cfg);
}

static void test_config(void)
{
    struct sns_iio_config c;

    config(0, 0, 0, 0, &c);
    CHECK(c.rate_hz == 0 && !c.gyro && !c.accel && !c.mag && !c.accel_engine);
    config(SNS_HANDLE_LIGHT, 1000000, 0, 0, &c);
    CHECK(c.rate_hz == 0);

    config(SNS_HANDLE_ACCEL, 200000000, 0, 0, &c);
    CHECK(c.accel && c.accel_engine && !c.gyro && !c.mag && c.rate_hz == 5);
    config(SNS_HANDLE_ACCEL, 66667000, 0, 0, &c);  /* SENSOR_DELAY_UI-ish: rounds up */
    CHECK(c.rate_hz == 15);
    config(SNS_HANDLE_GYRO, 1000, 0, 0, &c);       /* below min delay: 200 Hz */
    CHECK(c.gyro && !c.accel && !c.accel_engine && c.rate_hz == 200);
    config(SNS_HANDLE_GYRO, 2000000000LL, 0, 0, &c);  /* 0.5 Hz: kernel minimum 4 */
    CHECK(c.rate_hz == SNS_MIN_RATE_HZ);
    config(SNS_HANDLE_GYRO_UNCAL, 20000000, 0, 0, &c);
    CHECK(c.gyro && c.rate_hz == 50);

    config(SNS_HANDLE_MAG, 5000000, 0, 0, &c);     /* compass min delay 10 ms */
    CHECK(c.mag && !c.accel && c.accel_engine && !c.gyro && c.rate_hz == 100);
    config(SNS_HANDLE_MAG_UNCAL, 20000000, 0, 0, &c);
    CHECK(c.mag && c.rate_hz == 50);
    /* Compass with accel at 125 Hz: moved to 200 Hz so the compass is read every 10 ms. */
    config(SNS_HANDLE_MAG, 100000000, SNS_HANDLE_ACCEL, 8000000, &c);
    CHECK(c.mag && c.accel && c.accel_engine && c.rate_hz == 200);
    config(SNS_HANDLE_MAG, 100000000, SNS_HANDLE_ACCEL, 10000000, &c);
    CHECK(c.rate_hz == 100);
    config(SNS_HANDLE_GYRO, 20000000, SNS_HANDLE_ACCEL, 5000000, &c);
    CHECK(c.gyro && c.accel && c.rate_hz == 200);
    CHECK(sns_expected_format(&c) == (SNS_FORMAT_GYRO | SNS_FORMAT_ACCEL));
    config(SNS_HANDLE_GYRO, 20000000, SNS_HANDLE_MAG, 20000000, &c);
    CHECK(sns_expected_format(&c) == (SNS_FORMAT_GYRO | SNS_FORMAT_COMPASS));
}

static int find_write(const struct sns_write *w, int n, const char *path)
{
    int i;

    for (i = 0; i < n; i++)
        if (strcmp(w[i].path, path) == 0)
            return i;
    return -1;
}

static void test_plan(void)
{
    struct sns_write w[SNS_MAX_WRITES];
    struct sns_iio_config c;
    const char *root = new_root("iio");
    char path[128];
    int n, i;

    config(0, 0, 0, 0, &c);
    n = sns_plan_iio(&c, 0, "mpu9150-dev0", w, SNS_MAX_WRITES);
    CHECK(n == 2);
    CHECK(strcmp(w[0].path, "buffer/enable") == 0 && strcmp(w[0].value, "0") == 0 && w[0].optional);
    CHECK(strcmp(w[1].path, "power_state") == 0 && strcmp(w[1].value, "0") == 0 && !w[1].optional);

    config(SNS_HANDLE_GYRO, 5000000, SNS_HANDLE_MAG, 10000000, &c);
    n = sns_plan_iio(&c, 0, "mpu9150-dev0", w, SNS_MAX_WRITES);
    CHECK(n > 0);
    CHECK(sns_plan_iio(&c, 0, "mpu9150-dev0", w, 5) == -1);
    /* Order of spec §4.6. */
    CHECK(strcmp(w[0].path, "power_state") == 0 && strcmp(w[0].value, "1") == 0);
    CHECK(strcmp(w[1].path, "buffer/enable") == 0 && strcmp(w[1].value, "0") == 0);
    CHECK(strcmp(w[n - 1].path, "buffer/enable") == 0 && strcmp(w[n - 1].value, "1") == 0);
    CHECK(!w[n - 1].optional);
    CHECK(find_write(w, n, "dmp_on") < 0);
    CHECK(find_write(w, n, "gyro_enable") < find_write(w, n, "scan_elements/in_anglvel_x_en"));
    CHECK(find_write(w, n, "accl_enable") < find_write(w, n, "scan_elements/in_accel_x_en"));
    CHECK(find_write(w, n, "compass_enable") < find_write(w, n, "scan_elements/in_magn_x_en"));
    CHECK(find_write(w, n, "sampling_frequency") > find_write(w, n, "compass_enable"));
    CHECK(find_write(w, n, "sampling_frequency") < find_write(w, n, "scan_elements/in_anglvel_x_en"));
    CHECK(find_write(w, n, "trigger/current_trigger") > find_write(w, n, "scan_elements/in_timestamp_en"));
    CHECK(strcmp(w[find_write(w, n, "sampling_frequency")].value, "200") == 0);
    CHECK(strcmp(w[find_write(w, n, "trigger/current_trigger")].value, "mpu9150-dev0") == 0);
    CHECK(strcmp(w[find_write(w, n, "buffer/length")].value, "480") == 0);
    CHECK(strcmp(w[find_write(w, n, "in_anglvel_scale")].value, "3") == 0);
    CHECK(strcmp(w[find_write(w, n, "in_accel_scale")].value, "0") == 0);
    /* Never anything DMP- or firmware-related to turn on. */
    for (i = 0; i < n; i++) {
        CHECK(strstr(w[i].path, "firmware") == NULL);
        CHECK(strncmp(w[i].path, "dmp", 3) != 0 || strcmp(w[i].value, "0") == 0);
    }
    n = sns_plan_iio(&c, 1, "", w, SNS_MAX_WRITES);
    CHECK(find_write(w, n, "dmp_on") == 2 && strcmp(w[2].value, "0") == 0);
    CHECK(find_write(w, n, "trigger/current_trigger") < 0);

    /* Run the plan against a scratch sysfs and read back the result. */
    for (i = 0; i < n; i++) {
        snprintf(path, sizeof(path), "/%s", w[i].path);
        put(root, path, "x");
    }
    for (i = 0; i < n; i++) {
        snprintf(path, sizeof(path), "/%s", w[i].path);
        CHECK(sns_write_file(root, path, w[i].value) == 0);
    }
    CHECK(strcmp(get(root, "/gyro_enable"), "1") == 0);
    CHECK(strcmp(get(root, "/accl_enable"), "0") == 0);
    CHECK(strcmp(get(root, "/compass_enable"), "1") == 0);
    CHECK(strcmp(get(root, "/scan_elements/in_anglvel_z_en"), "1") == 0);
    CHECK(strcmp(get(root, "/scan_elements/in_accel_y_en"), "0") == 0);
    CHECK(strcmp(get(root, "/scan_elements/in_magn_x_en"), "1") == 0);
    CHECK(strcmp(get(root, "/scan_elements/in_quaternion_r_en"), "0") == 0);
    CHECK(strcmp(get(root, "/scan_elements/in_timestamp_en"), "1") == 0);
    CHECK(strcmp(get(root, "/buffer/enable"), "1") == 0);
    CHECK(sns_write_file(root, "/absent/attr", "1") == -ENOENT);

    /* Compass alone keeps the accel engine on, out of the FIFO. */
    config(SNS_HANDLE_MAG, 10000000, 0, 0, &c);
    n = sns_plan_iio(&c, 0, "t", w, SNS_MAX_WRITES);
    CHECK(strcmp(w[find_write(w, n, "accl_enable")].value, "1") == 0);
    CHECK(strcmp(w[find_write(w, n, "scan_elements/in_accel_x_en")].value, "0") == 0);
    CHECK(strcmp(w[find_write(w, n, "gyro_enable")].value, "0") == 0);
}

/* ---- Records ------------------------------------------------------------------------- */

static void test_scan_type(void)
{
    struct sns_scan_type t;

    CHECK(sns_parse_scan_type("le:s16/16>>0", &t) == 0);
    CHECK(!t.big_endian && t.is_signed && t.bits == 16 && t.storage_bits == 16 && t.shift == 0);
    CHECK(sns_parse_scan_type("le:s64/64>>0\n", &t) == 0 && t.storage_bits == 64);
    CHECK(sns_parse_scan_type("be:u12/16>>4", &t) == 0);
    CHECK(t.big_endian && !t.is_signed && t.bits == 12 && t.shift == 4);
    CHECK(sns_parse_scan_type("le:s16/24>>0", &t) != 0);
    CHECK(sns_parse_scan_type("le:s32/16>>0", &t) != 0);
    CHECK(sns_parse_scan_type("le:s16/16>>4", &t) != 0);
    CHECK(sns_parse_scan_type("xe:s16/16>>0", &t) != 0);
    CHECK(sns_parse_scan_type("le:x16/16>>0", &t) != 0);
    CHECK(sns_parse_scan_type("le:s16/16", &t) != 0);
    CHECK(sns_parse_scan_type("", &t) != 0);
    sns_default_scan_type(SNS_SCAN_QUAT_R, &t);
    CHECK(t.storage_bits == 32);
    sns_default_scan_type(SNS_SCAN_MAGN_Z, &t);
    CHECK(t.storage_bits == 16 && t.is_signed);
    sns_default_scan_type(SNS_SCAN_TIMESTAMP, &t);
    CHECK(t.storage_bits == 64);
}

static void layout_for(int gyro, int accel, int mag, int quat, struct sns_layout *l, int *rc)
{
    int on[SNS_SCAN_COUNT] = { 0 }, i;
    struct sns_scan_type types[SNS_SCAN_COUNT];

    for (i = 0; i < SNS_SCAN_COUNT; i++) {
        sns_default_scan_type(i, &types[i]);
        on[i] = (i <= SNS_SCAN_QUAT_Z && quat) ||
                (i >= SNS_SCAN_GYRO_X && i <= SNS_SCAN_GYRO_Z && gyro) ||
                (i >= SNS_SCAN_ACCEL_X && i <= SNS_SCAN_ACCEL_Z && accel) ||
                (i >= SNS_SCAN_MAGN_X && i <= SNS_SCAN_MAGN_Z && mag) ||
                i == SNS_SCAN_TIMESTAMP;
    }
    *rc = sns_layout_build(on, types, l);
}

static void test_layout(void)
{
    struct sns_layout l;
    struct sns_scan_type types[SNS_SCAN_COUNT];
    int on[SNS_SCAN_COUNT] = { 0 }, rc, i;

    /* Spec §4.4 example: 18 data bytes, format byte 23, timestamp 24, 32 bytes. */
    layout_for(1, 1, 1, 0, &l, &rc);
    CHECK(rc == 0 && l.size == 32 && l.format_offset == 23);
    CHECK(l.offset[SNS_SCAN_GYRO_X] == 0 && l.offset[SNS_SCAN_ACCEL_X] == 6);
    CHECK(l.offset[SNS_SCAN_MAGN_X] == 12 && l.offset[SNS_SCAN_MAGN_Z] == 16);
    CHECK(l.offset[SNS_SCAN_TIMESTAMP] == 24 && l.offset[SNS_SCAN_QUAT_R] == -1);
    layout_for(1, 0, 0, 0, &l, &rc);
    CHECK(rc == 0 && l.size == 16 && l.format_offset == 7 && l.offset[SNS_SCAN_TIMESTAMP] == 8);
    layout_for(1, 1, 0, 0, &l, &rc);
    CHECK(rc == 0 && l.size == 24 && l.format_offset == 15 && l.offset[SNS_SCAN_ACCEL_Z] == 10);
    layout_for(0, 0, 1, 0, &l, &rc);
    CHECK(rc == 0 && l.size == 16 && l.offset[SNS_SCAN_MAGN_X] == 0);
    layout_for(1, 1, 1, 1, &l, &rc);
    CHECK(rc == 0 && l.offset[SNS_SCAN_GYRO_X] == 16 && l.size == 48 && l.format_offset == 39);
    layout_for(0, 0, 0, 0, &l, &rc);
    CHECK(rc == 0 && l.size == 8 && l.format_offset == -1);

    for (i = 0; i < SNS_SCAN_COUNT; i++)
        sns_default_scan_type(i, &types[i]);
    on[SNS_SCAN_GYRO_X] = 1;
    CHECK(sns_layout_build(on, types, &l) == -1); /* no timestamp */
}

static void put16(uint8_t *p, int v)
{
    p[0] = (uint8_t)(v & 0xff);
    p[1] = (uint8_t)((v >> 8) & 0xff);
}

static void put64(uint8_t *p, int64_t v)
{
    int i;
    for (i = 0; i < 8; i++)
        p[i] = (uint8_t)(((uint64_t)v >> (8 * i)) & 0xff);
}

static void test_record(void)
{
    struct sns_layout l;
    struct sns_sample s;
    uint8_t rec[48];
    int rc, i;
    static const int vals[9] = { 100, -200, 32767, -32768, 1, 16384, 120, -45, 300 };

    layout_for(1, 1, 1, 0, &l, &rc);
    memset(rec, 0xA5, sizeof(rec)); /* padding is garbage in the driver */
    for (i = 0; i < 9; i++)
        put16(rec + 2 * i, vals[i]);
    rec[23] = 0x0E;
    put64(rec + 24, 123456789012345LL);
    CHECK(sns_parse_record(rec, &l, 0x0E, &s) == 0);
    CHECK(s.has_gyro && s.has_accel && s.has_mag && s.format == 0x0E);
    CHECK(s.gyro[0] == 100 && s.gyro[1] == -200 && s.gyro[2] == 32767);
    CHECK(s.accel[0] == -32768 && s.accel[1] == 1 && s.accel[2] == 16384);
    CHECK(s.mag[0] == 120 && s.mag[1] == -45 && s.mag[2] == 300);
    CHECK(s.timestamp == 123456789012345LL);

    /* A zero compass triple is "no new sample". */
    put16(rec + 12, 0); put16(rec + 14, 0); put16(rec + 16, 0);
    CHECK(sns_parse_record(rec, &l, 0x0E, &s) == 0);
    CHECK(s.has_gyro && s.has_accel && !s.has_mag);
    /* Two zero axes are a real sample. */
    put16(rec + 16, 7);
    CHECK(sns_parse_record(rec, &l, 0x0E, &s) == 0 && s.has_mag && s.mag[2] == 7);

    /* Wrong or invalid format byte: rejected. */
    rec[23] = 0x06;
    CHECK(sns_parse_record(rec, &l, 0x0E, &s) == -1);
    rec[23] = 0x1E;
    CHECK(sns_parse_record(rec, &l, 0x0E, &s) == -1);

    /* Gyro only: 16-byte records. */
    layout_for(1, 0, 0, 0, &l, &rc);
    memset(rec, 0, sizeof(rec));
    put16(rec, -1); put16(rec + 2, 2); put16(rec + 4, -3);
    rec[7] = SNS_FORMAT_GYRO;
    put64(rec + 8, 42);
    CHECK(sns_parse_record(rec, &l, SNS_FORMAT_GYRO, &s) == 0);
    CHECK(s.has_gyro && !s.has_accel && !s.has_mag && s.gyro[0] == -1 && s.gyro[2] == -3);
    CHECK(s.timestamp == 42);
    /* An accel-only record of the same size is not taken for gyro data. */
    rec[7] = SNS_FORMAT_ACCEL;
    CHECK(sns_parse_record(rec, &l, SNS_FORMAT_GYRO, &s) == -1);

    /* Generic types: big-endian unsigned 12 bits shifted by 4. */
    for (i = 0; i < SNS_SCAN_COUNT; i++)
        l.offset[i] = -1;
    CHECK(sns_parse_scan_type("be:u12/16>>4", &l.type[SNS_SCAN_ACCEL_X]) == 0);
    l.type[SNS_SCAN_ACCEL_Y] = l.type[SNS_SCAN_ACCEL_Z] = l.type[SNS_SCAN_ACCEL_X];
    l.offset[SNS_SCAN_ACCEL_X] = 0; l.offset[SNS_SCAN_ACCEL_Y] = 2; l.offset[SNS_SCAN_ACCEL_Z] = 4;
    l.offset[SNS_SCAN_TIMESTAMP] = 8;
    l.format_offset = 7;
    l.size = 16;
    sns_default_scan_type(SNS_SCAN_TIMESTAMP, &l.type[SNS_SCAN_TIMESTAMP]);
    memset(rec, 0, sizeof(rec));
    rec[0] = 0xAB; rec[1] = 0xC0; /* 0xABC */
    rec[2] = 0xFF; rec[3] = 0xFF; /* 0xFFF unsigned */
    rec[7] = SNS_FORMAT_ACCEL;
    CHECK(sns_parse_record(rec, &l, SNS_FORMAT_ACCEL, &s) == 0);
    CHECK(s.accel[0] == 0xABC && s.accel[1] == 0xFFF && s.accel[2] == 0);
}

/* ---- Matrices and units -------------------------------------------------------------- */

static void test_matrix(void)
{
    int m[9];
    int32_t in[3] = { 1, 2, 3 };
    float out[3];

    /* board-notle.c mpu9150_notle_data (spec §4.3). */
    CHECK(sns_parse_matrix("0,1,0,0,0,1,1,0,0\n", m) == 1);
    sns_apply_matrix(m, in, out);
    CHECK(out[0] == 2 && out[1] == 3 && out[2] == 1);
    CHECK(sns_parse_matrix("1,0,0,0,0,-1,0,1,0", m) == 1);
    sns_apply_matrix(m, in, out);
    CHECK(out[0] == 1 && out[1] == -3 && out[2] == 2);
    CHECK(sns_parse_matrix("-1, 0, 0, 0, 1, 0, 0, 0, 1", m) == -1);
    sns_apply_matrix(sns_identity, in, out);
    CHECK(out[0] == 1 && out[1] == 2 && out[2] == 3);

    CHECK(sns_parse_matrix("1,1,0,0,0,1,1,0,0", m) == 0);  /* row with two entries */
    CHECK(sns_parse_matrix("1,0,0,1,0,0,0,0,1", m) == 0);  /* column twice */
    CHECK(sns_parse_matrix("2,0,0,0,1,0,0,0,1", m) == 0);  /* not +-1 */
    CHECK(sns_parse_matrix("1,0,0,0,1,0,0,0", m) == 0);    /* eight values */
    CHECK(sns_parse_matrix("1,0,0,0,1,0,0,0,1,0", m) == 0);
    CHECK(sns_parse_matrix("", m) == 0);
}

static void test_units(void)
{
    long long v;

    CHECK(NEAR(sns_gyro_scale(2000), 0.0010652645, 1e-9));
    CHECK(NEAR(sns_gyro_scale(250), 0.00013315806, 1e-10));
    CHECK(NEAR(sns_accel_scale(2), 0.00059855, 1e-8));
    CHECK(NEAR(32767 * sns_accel_scale(2), 19.6127, 1e-3));
    CHECK(NEAR(sns_accel_scale(16), 0.0047884, 1e-6));
    CHECK(NEAR(sns_mag_scale(9830LL << 15), 0.29999, 1e-5));
    CHECK(sns_parse_ll("322109440\n", &v) == 0 && v == 322109440);
    CHECK(sns_parse_ll(" 2000", &v) == 0 && v == 2000);
    CHECK(sns_parse_ll("2000 dps", &v) == -1);
    CHECK(sns_parse_ll("", &v) == -1);
}

/* ---- Clocks -------------------------------------------------------------------------- */

static void test_clock(void)
{
    struct sns_clock_sample s[3] = {
        { 1000000, 500, 1900000 },     /* 900 us bracket */
        { 3000000, 2000, 3200000 },    /* 200 us bracket: best */
        { 5000000, 4000, 4000000 },    /* after < before: ignored */
    };
    long long temp;
    int64_t robust, off, width;

    CHECK(sns_parse_temperature("2350 98765432101\n", &temp, &robust) == 0);
    CHECK(temp == 2350 && robust == 98765432101LL);
    CHECK(sns_parse_temperature("-1200 5", &temp, &robust) == 0 && temp == -1200 && robust == 5);
    CHECK(sns_parse_temperature("2350", &temp, &robust) == -1);
    CHECK(sns_parse_temperature("2350 x", &temp, &robust) == -1);
    CHECK(sns_parse_temperature("2350 12 7", &temp, &robust) == -1);

    CHECK(sns_clock_offset(s, 3, &off, &width) == 0);
    CHECK(width == 200000 && off == 3100000 - 2000);
    CHECK(sns_clock_offset(s + 2, 1, &off, NULL) == -1);
    CHECK(sns_clock_offset(s, 0, &off, NULL) == -1);

    /* A monotonic stamp of 5 s while monotonic reads 10 s and boottime 70 s. */
    CHECK(sns_clock_convert(5000000000LL, 10000000000LL, 70000000000LL) == 65000000000LL);

    CHECK(sns_clamp_timestamp(0, 100, 200) == 100);
    CHECK(sns_clamp_timestamp(0, 300, 200) == 200);   /* never in the future */
    CHECK(sns_clamp_timestamp(150, 100, 200) == 151); /* strictly increasing */
    CHECK(sns_clamp_timestamp(150, 150, 200) == 151);
    CHECK(sns_clamp_timestamp(150, 180, 200) == 180);
}

/* ---- Magnetometer calibration -------------------------------------------------------- */

/* Deterministic pseudo-random numbers in [-1, 1]. */
static float noise(unsigned *state)
{
    *state = *state * 1103515245u + 12345u;
    return (float)((*state >> 8) & 0xffff) / 32768.0f - 1.0f;
}

static void sphere_point(const float c[3], float r, double az, double el, float out[3])
{
    out[0] = c[0] + (float)(r * cos(el) * cos(az));
    out[1] = c[1] + (float)(r * cos(el) * sin(az));
    out[2] = c[2] + (float)(r * sin(el));
}

static void test_sphere_fit(void)
{
    static float pts[64][3];
    const float c[3] = { 30.0f, -20.0f, 45.0f };
    float center[3], r, rms, thin;
    unsigned st = 1;
    int i;

    for (i = 0; i < 64; i++) {
        sphere_point(c, 48.0f, i * 2.399963, asin(-1.0 + (2.0 * i + 1) / 64), pts[i]);
        pts[i][0] += 0.3f * noise(&st);
        pts[i][1] += 0.3f * noise(&st);
        pts[i][2] += 0.3f * noise(&st);
    }
    CHECK(sns_sphere_fit((const float (*)[3])pts, 64, center, &r, &rms, &thin) == 0);
    CHECK(NEAR(center[0], 30, 0.3) && NEAR(center[1], -20, 0.3) && NEAR(center[2], 45, 0.3));
    CHECK(NEAR(r, 48, 0.3) && rms < 0.5f && thin > 20.0f);

    /* Points on one circle: the fit may solve, but the thin direction is flat. */
    for (i = 0; i < 32; i++)
        sphere_point(c, 48.0f, i * 0.19635, 0.3, pts[i]);
    if (sns_sphere_fit((const float (*)[3])pts, 32, center, &r, &rms, &thin) == 0)
        CHECK(thin < 0.5f);
    CHECK(sns_sphere_fit((const float (*)[3])pts, 3, center, &r, &rms, &thin) == -1);
}

static void test_magcal(void)
{
    static struct sns_magcal c;
    const float bias[3] = { -12.0f, 33.0f, 7.5f };
    float v[3];
    char text[96];
    unsigned st = 7;
    int i, accepted = 0;

    sns_magcal_init(&c);
    CHECK(!c.valid && c.accuracy == SNS_ACCURACY_UNRELIABLE);
    /* A still device adds one point only. */
    for (i = 0; i < 100; i++) {
        v[0] = 10; v[1] = 20; v[2] = 30;
        CHECK(sns_magcal_add(&c, v) == 0);
    }
    CHECK(c.n == 1);

    /* A head turning about one axis traces a circle: never accepted. */
    sns_magcal_init(&c);
    for (i = 0; i < 400; i++) {
        sphere_point(bias, 45.0f, i * 0.05, 0.9, v);
        v[0] += 0.4f * noise(&st);
        v[1] += 0.4f * noise(&st);
        v[2] += 0.4f * noise(&st);
        accepted |= sns_magcal_add(&c, v);
    }
    CHECK(!accepted && !c.valid);

    /* Movement over a wide cap: accepted, bias recovered. */
    sns_magcal_init(&c);
    accepted = 0;
    for (i = 0; i < 600 && !accepted; i++) {
        sphere_point(bias, 45.0f, i * 2.399963, asin(-0.2 + 1.2 * ((i * 37) % 100) / 100.0), v);
        v[0] += 0.4f * noise(&st);
        v[1] += 0.4f * noise(&st);
        v[2] += 0.4f * noise(&st);
        accepted = sns_magcal_add(&c, v);
    }
    CHECK(accepted && c.valid && c.accuracy == SNS_ACCURACY_HIGH);
    CHECK(NEAR(c.bias[0], bias[0], 1.0) && NEAR(c.bias[1], bias[1], 1.0) && NEAR(c.bias[2], bias[2], 1.0));
    CHECK(NEAR(c.radius, 45, 1.0));
    CHECK(c.n >= SNS_MAGCAL_MIN_POINTS && c.n <= SNS_MAGCAL_POINTS);

    /* A field far too strong (a magnet nearby) is not taken. */
    {
        static struct sns_magcal d;
        int any = 0;
        sns_magcal_init(&d);
        for (i = 0; i < 600; i++) {
            sphere_point(bias, 400.0f, i * 2.399963, asin(-1.0 + (2.0 * (i % 64) + 1) / 64), v);
            any |= sns_magcal_add(&d, v);
        }
        CHECK(!any && !d.valid);
    }
    /* Non-finite input is ignored. */
    v[0] = NAN; v[1] = 0; v[2] = 0;
    CHECK(sns_magcal_add(&c, v) == 0);

    /* File round trip. */
    CHECK(sns_magcal_format(&c, text, sizeof(text)) > 0);
    CHECK(strncmp(text, "glass-magcal 1 ", 15) == 0);
    {
        static struct sns_magcal e;
        sns_magcal_init(&e);
        CHECK(sns_magcal_parse(&e, text) == 0);
        CHECK(e.valid && e.accuracy == SNS_ACCURACY_MEDIUM && e.n == 0);
        CHECK(NEAR(e.bias[0], c.bias[0], 0.001) && NEAR(e.bias[2], c.bias[2], 0.001));
        CHECK(sns_magcal_parse(&e, "glass-magcal 2 1 2 3 45\n") == -1);
        CHECK(sns_magcal_parse(&e, "glass-magcal 1 1 2 3 4\n") == -1);       /* radius */
        CHECK(sns_magcal_parse(&e, "glass-magcal 1 5000 2 3 45\n") == -1);   /* bias */
        CHECK(sns_magcal_parse(&e, "glass-magcal 1 1 2 nan 45\n") == -1);
        CHECK(sns_magcal_parse(&e, "glass-magcal 1 1 2 3 45 junk\n") == -1);
        CHECK(sns_magcal_parse(&e, "glass-magcal 1 1 2\n") == -1);
        CHECK(sns_magcal_parse(&e, "") == -1);
    }
}

static void test_gyrobias(void)
{
    struct sns_gyrobias g;
    float w[3];
    unsigned st = 3;
    int64_t ts = 1000000000LL;
    int i, changed = 0;

    sns_gyrobias_init(&g);
    /* Still at 100 Hz with a 0.02 rad/s offset and about 1 LSB of noise. */
    for (i = 0; i < 250; i++, ts += 10000000) {
        w[0] = 0.02f + 0.001f * noise(&st);
        w[1] = -0.01f + 0.001f * noise(&st);
        w[2] = 0.005f + 0.001f * noise(&st);
        changed |= sns_gyrobias_add(&g, w, ts);
    }
    CHECK(changed && g.valid);
    CHECK(NEAR(g.bias[0], 0.02, 0.001) && NEAR(g.bias[1], -0.01, 0.001) && NEAR(g.bias[2], 0.005, 0.001));

    /* Moving: no update. */
    sns_gyrobias_init(&g);
    changed = 0;
    for (i = 0; i < 1000; i++, ts += 10000000) {
        w[0] = 0.5f * (float)sin(i * 0.05);
        w[1] = 0.0f;
        w[2] = 0.1f;
        changed |= sns_gyrobias_add(&g, w, ts);
    }
    CHECK(!changed && !g.valid);

    /* A steady rotation faster than any bias: not taken for one. */
    sns_gyrobias_init(&g);
    for (i = 0; i < 300; i++, ts += 10000000) {
        w[0] = 1.0f; w[1] = 0; w[2] = 0;
        changed |= sns_gyrobias_add(&g, w, ts);
    }
    CHECK(!changed);
    /* Too few samples in a window (5 Hz): no update. */
    sns_gyrobias_init(&g);
    for (i = 0; i < 30; i++, ts += 200000000) {
        w[0] = 0.01f; w[1] = 0; w[2] = 0;
        changed |= sns_gyrobias_add(&g, w, ts);
    }
    CHECK(!changed);
}

/* ---- Discovery ----------------------------------------------------------------------- */

static void test_discovery(void)
{
    const char *root = new_root("sysfs");
    char path[512];
    int in = -1, ev = -1;
    char buf[8];

    CHECK(sns_find_input(root, "ltr506_als", &in, &ev) == -ENOENT);
    CHECK(sns_find_iio(root, "mpu9150") == -ENOENT);

    put(root, "/sys/class/input/input0/name", "glasshub\n");
    snprintf(path, sizeof(path), "%s/sys/class/input/input0/event0", root);
    mkdirs(path);
    put(root, "/sys/class/input/input3/name", "ltr506_ps\n");
    snprintf(path, sizeof(path), "%s/sys/class/input/input3/event4", root);
    mkdirs(path);
    put(root, "/sys/class/input/input5/name", "ltr506_als\n");
    put(root, "/sys/class/input/input5/als_enable", "0\n");
    CHECK(sns_find_input(root, "ltr506_als", &in, &ev) == -ENOENT); /* no event node yet */
    snprintf(path, sizeof(path), "%s/sys/class/input/input5/event7", root);
    mkdirs(path);
    put(root, "/sys/class/input/event7/dev", "13:71\n");
    CHECK(sns_find_input(root, "ltr506_als", &in, &ev) == 0 && in == 5 && ev == 7);
    CHECK(sns_find_input(root, "ltr506_ps", &in, &ev) == 0 && in == 3 && ev == 4);
    CHECK(sns_find_input(root, "ltr506", &in, &ev) == -ENOENT);

    put(root, "/sys/bus/iio/devices/trigger0/name", "mpu9150-dev1\n");
    put(root, "/sys/bus/iio/devices/iio:device0/name", "other\n");
    put(root, "/sys/bus/iio/devices/iio:device1/name", "mpu9150\n");
    CHECK(sns_find_iio(root, "mpu9150") == 1);
    CHECK(sns_find_iio(root, "mpu6050") == -ENOENT);

    CHECK(sns_read_file(root, "/sys/class/input/input5/name", buf, sizeof(buf)) == 7);
    CHECK(strcmp(buf, "ltr506_") == 0); /* truncated to the buffer */
    CHECK(sns_read_file(root, "/nothing", buf, sizeof(buf)) == -ENOENT);
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s <scratch-dir>\n", argv[0]);
        return 2;
    }
    tmp = argv[1];
    test_list();
    test_config();
    test_plan();
    test_scan_type();
    test_layout();
    test_record();
    test_matrix();
    test_units();
    test_clock();
    test_sphere_fit();
    test_magcal();
    test_gyrobias();
    test_discovery();
    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("sensors logic: all checks passed\n");
    return 0;
}
