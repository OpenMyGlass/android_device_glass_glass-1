/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Glass sensors HAL logic, kept free of Android headers so tests/test_hal_logic.py can build
 * and run it on the host: the sensor table, the IIO record layout and parser, mounting
 * matrices, unit and clock conversions, hard-iron and gyro bias estimation, the sysfs enable
 * sequence and device discovery. Facts: docs/specs/sensors.md (cited as "spec §n") and the
 * GPL kernel source (drivers/staging/iio/imu/inv_mpu, drivers/input/misc/ltr506als.c).
 */
#ifndef GLASS_SENSORS_LOGIC_H
#define GLASS_SENSORS_LOGIC_H

#include <stddef.h>
#include <stdint.h>

/* ---- Sensor table -------------------------------------------------------------------- */

/* Handles (spec §7). 0 is not used. */
enum {
    SNS_HANDLE_ACCEL = 1,
    SNS_HANDLE_MAG = 2,
    SNS_HANDLE_GYRO = 3,
    SNS_HANDLE_LIGHT = 4,
    SNS_HANDLE_MAG_UNCAL = 5,
    SNS_HANDLE_GYRO_UNCAL = 6,
    SNS_HANDLE_MAX = 6,
};
#define SNS_NUM_SENSORS 6

/* Android sensor types: the same values in 4.4 and 6.0 hardware/sensors.h (sensors.c checks). */
#define SNS_TYPE_ACCELEROMETER 1
#define SNS_TYPE_MAGNETIC_FIELD 2
#define SNS_TYPE_GYROSCOPE 4
#define SNS_TYPE_LIGHT 5
#define SNS_TYPE_MAGNETIC_FIELD_UNCALIBRATED 14
#define SNS_TYPE_GYROSCOPE_UNCALIBRATED 16

/* Hardware a sensor needs. */
#define SNS_ENGINE_GYRO 0x1u
#define SNS_ENGINE_ACCEL 0x2u
#define SNS_ENGINE_MAG 0x4u
#define SNS_ENGINE_LIGHT 0x8u

struct sns_desc {
    int handle;
    int type;
    const char *name;
    const char *vendor;
    float max_range;
    float resolution;
    float power_ma;
    int32_t min_delay_us; /* 0: on-change */
    int32_t max_delay_us; /* 0 for on-change sensors */
    unsigned engines;
};

extern const struct sns_desc sns_sensors[SNS_NUM_SENSORS];

/* The table entry for handle, or NULL. */
const struct sns_desc *sns_find(int handle);

/*
 * Checks a sensor table: unique positive handles, known types, positive ranges and
 * resolutions, on-change sensors with min delay 0, continuous ones with a positive min delay
 * and a max delay not below it, each uncalibrated sensor next to its calibrated twin on the
 * same engine. Returns 0, or -1 with the reason in why.
 */
int sns_list_check(const struct sns_desc *list, int n, char *why, size_t why_len);

/* ---- IIO configuration and enable sequence ------------------------------------------- */

/* FIFO rate limits: the kernel accepts 4..1000 Hz (inv_mpu_iio.h MIN/MAX_FIFO_RATE); the HAL
 * stops at 200 Hz like stock (spec §5.2), and at 100 Hz for the compass alone (spec §7). */
#define SNS_MIN_RATE_HZ 4
#define SNS_MAX_RATE_HZ 200
#define SNS_MAX_MAG_RATE_HZ 100

struct sns_iio_config {
    int gyro;          /* gyro engine and scan elements */
    int accel;         /* accel scan elements */
    int accel_engine;  /* accel engine (also on for the compass alone, see sensors_logic.c) */
    int mag;           /* compass_enable and scan elements */
    int rate_hz;       /* sampling_frequency; 0 when nothing is enabled */
};

/*
 * IIO configuration for the enabled handles. enabled[] and delay_ns[] are indexed by handle
 * (SNS_HANDLE_MAX + 1 entries).
 */
void sns_iio_config_for(const int enabled[], const int64_t delay_ns[], struct sns_iio_config *cfg);

/* Expected data-format byte (inv_mpu_ring.c:536-539) of records under cfg. */
int sns_expected_format(const struct sns_iio_config *cfg);

struct sns_write {
    char path[64];   /* relative to the IIO device directory, or absolute */
    char value[48];
    int optional;    /* failure is logged, the sequence goes on */
};
#define SNS_MAX_WRITES 32

/*
 * Sysfs writes, in order, that move the driver to cfg (spec §4.6): firmware_loaded says
 * whether dmp_on must be cleared, trigger_name goes to trigger/current_trigger. Returns the
 * number of writes, or -1 if out is too small.
 */
int sns_plan_iio(const struct sns_iio_config *cfg, int firmware_loaded, const char *trigger_name,
                 struct sns_write *out, int max);

/* ---- Records ------------------------------------------------------------------------- */

/* Scan element indices (inv_mpu_iio.h enum inv_mpu_scan). */
enum {
    SNS_SCAN_QUAT_R, SNS_SCAN_QUAT_X, SNS_SCAN_QUAT_Y, SNS_SCAN_QUAT_Z,
    SNS_SCAN_GYRO_X, SNS_SCAN_GYRO_Y, SNS_SCAN_GYRO_Z,
    SNS_SCAN_ACCEL_X, SNS_SCAN_ACCEL_Y, SNS_SCAN_ACCEL_Z,
    SNS_SCAN_MAGN_X, SNS_SCAN_MAGN_Y, SNS_SCAN_MAGN_Z,
    SNS_SCAN_TIMESTAMP,
    SNS_SCAN_COUNT
};

/* scan_elements/<name>_en and _type file stems, by index. */
extern const char *const sns_scan_names[SNS_SCAN_COUNT];

#define SNS_FORMAT_QUAT 0x01
#define SNS_FORMAT_GYRO 0x02
#define SNS_FORMAT_ACCEL 0x04
#define SNS_FORMAT_COMPASS 0x08

struct sns_scan_type {
    int big_endian;
    int is_signed;
    int bits;
    int storage_bits;
    int shift;
};

/* Parses an IIO type string such as "le:s16/16>>0". Returns 0 or -1. */
int sns_parse_scan_type(const char *s, struct sns_scan_type *t);

/* The driver's fixed types (spec §4.4): s32 quaternion, s16 data, s64 timestamp. */
void sns_default_scan_type(int index, struct sns_scan_type *t);

struct sns_layout {
    int offset[SNS_SCAN_COUNT]; /* -1: not enabled */
    struct sns_scan_type type[SNS_SCAN_COUNT];
    int format_offset;          /* -1: no data before the timestamp */
    int size;                   /* bytes per record */
};

/*
 * Record layout for the enabled scan elements, computed as the IIO core does
 * (industrialio-buffer.c iio_sw_buffer_preenable: each element aligned to its own size, the
 * timestamp last). The timestamp must be enabled. Returns 0 or -1.
 */
int sns_layout_build(const int enabled[SNS_SCAN_COUNT], const struct sns_scan_type types[SNS_SCAN_COUNT],
                     struct sns_layout *l);

struct sns_sample {
    int has_gyro, has_accel, has_mag; /* has_mag is 0 for an all-zero triple */
    int32_t gyro[3], accel[3], mag[3];
    int64_t timestamp;                /* robust clock, ns */
    int format;
};

/*
 * Parses one record. Returns 0, or -1 when the format byte is not expected_format (a record
 * of another layout, or a record the driver marks invalid).
 */
int sns_parse_record(const uint8_t *rec, const struct sns_layout *l, int expected_format,
                     struct sns_sample *s);

/* ---- Matrices and units -------------------------------------------------------------- */

/*
 * Parses a mounting matrix "a,b,c,d,e,f,g,h,i" (row-major, inv_mpu_core.c:1049-1070). It must
 * be a signed permutation matrix (one +-1 per row and column). Returns its determinant (+1 or
 * -1), or 0 if the text is not such a matrix.
 */
int sns_parse_matrix(const char *s, int m[9]);

/* out = M x in (InvenSense convention: body = M x chip, spec §4.3). */
void sns_apply_matrix(const int m[9], const int32_t in[3], float out[3]);

extern const int sns_identity[9];

/* SI units per LSB (spec §4.2). */
float sns_gyro_scale(int fsr_dps);       /* rad/s */
float sns_accel_scale(int fsr_g);        /* m/s^2 */
float sns_mag_scale(long long q30);      /* uT, from in_magn_scale */

/* Parses a decimal integer, allowing surrounding white space. Returns 0 or -1. */
int sns_parse_ll(const char *s, long long *v);

/* ---- Clocks -------------------------------------------------------------------------- */

/* Parses the temperature attribute: "<temperature> <robust ns>" (inv_mpu_core.c:1164). */
int sns_parse_temperature(const char *s, long long *temp, int64_t *robust_ns);

struct sns_clock_sample {
    int64_t before; /* CLOCK_BOOTTIME before the read */
    int64_t robust; /* robust-clock value the attribute printed */
    int64_t after;  /* CLOCK_BOOTTIME after the read */
};

/*
 * Offset to add to robust-clock values to get CLOCK_BOOTTIME, from the sample with the
 * narrowest bracket (midpoint minus robust). Writes the bracket width to *width when non-NULL.
 * Returns 0, or -1 if no sample has after >= before.
 */
int sns_clock_offset(const struct sns_clock_sample *s, int n, int64_t *offset, int64_t *width);

/* ts moved from one clock to another, given both clocks read at the same moment. */
int64_t sns_clock_convert(int64_t ts, int64_t now_src, int64_t now_dst);

/* A timestamp no later than now and strictly after prev (prev 0: none yet). */
int64_t sns_clamp_timestamp(int64_t prev, int64_t ts, int64_t now);

/* ---- Magnetometer hard-iron calibration ---------------------------------------------- */

#define SNS_MAGCAL_POINTS 48
#define SNS_MAGCAL_MIN_POINTS 24
#define SNS_MAGCAL_REFIT_EVERY 8
#define SNS_MAGCAL_MIN_SPACING 5.0f    /* uT between kept points */
#define SNS_MAGCAL_MIN_RADIUS 15.0f    /* uT; the geomagnetic field is about 25-65 uT */
#define SNS_MAGCAL_MAX_RADIUS 100.0f
#define SNS_MAGCAL_MAX_BIAS 1000.0f    /* uT; beyond this the file is rejected */

/* Accuracy values of sensors_event_t.status (SENSOR_STATUS_*, same in 4.4 and 6.0). */
#define SNS_ACCURACY_UNRELIABLE 0
#define SNS_ACCURACY_LOW 1
#define SNS_ACCURACY_MEDIUM 2
#define SNS_ACCURACY_HIGH 3

struct sns_magcal {
    float pts[SNS_MAGCAL_POINTS][3];
    int n, next, since_fit;
    float bias[3];
    float radius;
    int valid;
    int accuracy;
};

void sns_magcal_init(struct sns_magcal *c);

/*
 * Feeds one magnetometer sample (uT, device frame). Returns 1 when it led to a new accepted
 * fit (bias updated, accuracy HIGH), else 0.
 */
int sns_magcal_add(struct sns_magcal *c, const float v[3]);

/*
 * Least-squares sphere through n points. Returns 0 with the center, radius, RMS residual and
 * the standard deviation of the points along their thinnest direction, or -1 if singular.
 */
int sns_sphere_fit(const float (*p)[3], int n, float center[3], float *radius, float *rms,
                   float *thin_sd);

/* Calibration file text: "glass-magcal 1 <bx> <by> <bz> <radius>\n". Returns the length. */
int sns_magcal_format(const struct sns_magcal *c, char *buf, size_t len);

/* Loads a bias from file text (accuracy MEDIUM). Returns 0, or -1 if the text is not valid. */
int sns_magcal_parse(struct sns_magcal *c, const char *text);

/* ---- Gyroscope bias at rest ---------------------------------------------------------- */

#define SNS_GYRO_REST_WINDOW_NS 2000000000LL
#define SNS_GYRO_REST_MIN_SAMPLES 20
#define SNS_GYRO_REST_MAX_SD 0.004f  /* rad/s per axis, about 4 LSB at 2000 dps */
#define SNS_GYRO_MAX_BIAS 0.35f      /* rad/s, about 20 dps */

struct sns_gyrobias {
    float bias[3];
    int valid;
    int count;
    double sum[3], sumsq[3];
    int64_t start;
};

void sns_gyrobias_init(struct sns_gyrobias *g);

/*
 * Feeds one gyro sample (rad/s, device frame). At the end of each window the bias is set to
 * the window mean when every axis stayed still. Returns 1 when the bias changed, else 0.
 */
int sns_gyrobias_add(struct sns_gyrobias *g, const float w[3], int64_t ts);

/* ---- Files and discovery ------------------------------------------------------------- */

/* Reads root+path into buf (NUL-terminated, trailing newline removed). Returns length or -errno. */
int sns_read_file(const char *root, const char *path, char *buf, size_t len);

/* Writes value to root+path. Returns 0 or -errno. */
int sns_write_file(const char *root, const char *path, const char *value);

/*
 * Finds the input device called name under root/sys/class/input: its input number and its
 * event node number (/dev/input/event<N>). Returns 0 or -ENOENT.
 */
int sns_find_input(const char *root, const char *name, int *input_num, int *event_num);

/* Finds the IIO device called name under root/sys/bus/iio/devices. Returns its number or -ENOENT. */
int sns_find_iio(const char *root, const char *name);

#endif
