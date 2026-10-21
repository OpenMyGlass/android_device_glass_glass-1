/* SPDX-License-Identifier: Apache-2.0 */
#include "sensors_logic.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define GRAVITY 9.80665f

/* ---- Sensor table -------------------------------------------------------------------- */

/*
 * Values (spec §3, §7): gyro at +-2000 dps and accel at +-2 g, the power-on defaults the HAL
 * also writes (inv_mpu_core.c:270-317); the compass range is the AK8975's physical +-1200 uT,
 * not the kernel's scale constant; power figures are the stock HAL's. Gyro and accel run up
 * to 200 Hz, the compass to 100 Hz (single-measurement mode needs 7-9 ms). The slowest
 * continuous rate is the kernel's 4 Hz minimum FIFO rate.
 */
const struct sns_desc sns_sensors[SNS_NUM_SENSORS] = {
    { SNS_HANDLE_ACCEL, SNS_TYPE_ACCELEROMETER, "MPU-9150 Accelerometer", "InvenSense",
      2.0f * GRAVITY, 2.0f * GRAVITY / 32768.0f, 0.0f, 5000, 250000, SNS_ENGINE_ACCEL },
    { SNS_HANDLE_MAG, SNS_TYPE_MAGNETIC_FIELD, "AK8975 Magnetometer", "AKM",
      1200.0f, 0.3f, 10.0f, 10000, 250000, SNS_ENGINE_MAG },
    { SNS_HANDLE_GYRO, SNS_TYPE_GYROSCOPE, "MPU-9150 Gyroscope", "InvenSense",
      (float)(2000.0 * M_PI / 180.0), (float)(2000.0 / 32768.0 * M_PI / 180.0), 5.5f,
      5000, 250000, SNS_ENGINE_GYRO },
    { SNS_HANDLE_LIGHT, SNS_TYPE_LIGHT, "LTR-506ALS Light", "LiteOn",
      64000.0f, 1.0f, 0.75f, 0, 0, SNS_ENGINE_LIGHT },
    { SNS_HANDLE_MAG_UNCAL, SNS_TYPE_MAGNETIC_FIELD_UNCALIBRATED,
      "AK8975 Magnetometer (uncalibrated)", "AKM",
      1200.0f, 0.3f, 10.0f, 10000, 250000, SNS_ENGINE_MAG },
    { SNS_HANDLE_GYRO_UNCAL, SNS_TYPE_GYROSCOPE_UNCALIBRATED,
      "MPU-9150 Gyroscope (uncalibrated)", "InvenSense",
      (float)(2000.0 * M_PI / 180.0), (float)(2000.0 / 32768.0 * M_PI / 180.0), 5.5f,
      5000, 250000, SNS_ENGINE_GYRO },
};

const struct sns_desc *sns_find(int handle)
{
    int i;

    for (i = 0; i < SNS_NUM_SENSORS; i++)
        if (sns_sensors[i].handle == handle)
            return &sns_sensors[i];
    return NULL;
}

static int is_on_change(int type)
{
    return type == SNS_TYPE_LIGHT;
}

static int calibrated_twin(int type)
{
    if (type == SNS_TYPE_MAGNETIC_FIELD_UNCALIBRATED)
        return SNS_TYPE_MAGNETIC_FIELD;
    if (type == SNS_TYPE_GYROSCOPE_UNCALIBRATED)
        return SNS_TYPE_GYROSCOPE;
    return 0;
}

int sns_list_check(const struct sns_desc *list, int n, char *why, size_t why_len)
{
    int i, j;

#define FAIL(...) do { snprintf(why, why_len, __VA_ARGS__); return -1; } while (0)
    for (i = 0; i < n; i++) {
        const struct sns_desc *d = &list[i];
        int twin, found = 0;

        if (d->handle <= 0 || d->handle > SNS_HANDLE_MAX)
            FAIL("%s: handle %d out of range", d->name, d->handle);
        for (j = 0; j < i; j++) {
            if (list[j].handle == d->handle)
                FAIL("handle %d used twice", d->handle);
            if (list[j].type == d->type)
                FAIL("type %d listed twice", d->type);
        }
        switch (d->type) {
        case SNS_TYPE_ACCELEROMETER: case SNS_TYPE_MAGNETIC_FIELD: case SNS_TYPE_GYROSCOPE:
        case SNS_TYPE_LIGHT: case SNS_TYPE_MAGNETIC_FIELD_UNCALIBRATED:
        case SNS_TYPE_GYROSCOPE_UNCALIBRATED:
            break;
        default:
            FAIL("%s: unknown type %d", d->name, d->type);
        }
        if (!d->name || !d->name[0] || !d->vendor || !d->vendor[0])
            FAIL("handle %d: no name or vendor", d->handle);
        if (!(d->max_range > 0) || !(d->resolution > 0) || d->resolution > d->max_range)
            FAIL("%s: bad range or resolution", d->name);
        if (d->power_ma < 0)
            FAIL("%s: negative power", d->name);
        if (d->engines == 0)
            FAIL("%s: no engine", d->name);
        if (is_on_change(d->type)) {
            if (d->min_delay_us != 0 || d->max_delay_us != 0)
                FAIL("%s: on-change sensor with a delay", d->name);
        } else if (d->min_delay_us <= 0 || d->max_delay_us < d->min_delay_us) {
            FAIL("%s: bad min/max delay", d->name);
        }
        twin = calibrated_twin(d->type);
        if (twin) {
            for (j = 0; j < n; j++)
                if (list[j].type == twin && list[j].engines == d->engines &&
                    list[j].min_delay_us == d->min_delay_us)
                    found = 1;
            if (!found)
                FAIL("%s: no calibrated twin", d->name);
        }
    }
#undef FAIL
    return 0;
}

/* ---- IIO configuration and enable sequence ------------------------------------------- */

void sns_iio_config_for(const int enabled[], const int64_t delay_ns[], struct sns_iio_config *cfg)
{
    unsigned engines = 0;
    int rate = 0, h;

    memset(cfg, 0, sizeof(*cfg));
    for (h = 1; h <= SNS_HANDLE_MAX; h++) {
        const struct sns_desc *d = sns_find(h);
        int64_t period, min_period;
        int r;

        if (!d || !enabled[h] || (d->engines & SNS_ENGINE_LIGHT))
            continue;
        engines |= d->engines;
        min_period = (int64_t)d->min_delay_us * 1000;
        period = delay_ns[h];
        if (period < min_period)
            period = min_period;
        /* Round up so the rate is never below the one asked for. */
        r = (int)((1000000000LL + period - 1) / period);
        if (r > rate)
            rate = r;
    }
    cfg->gyro = !!(engines & SNS_ENGINE_GYRO);
    cfg->accel = !!(engines & SNS_ENGINE_ACCEL);
    cfg->mag = !!(engines & SNS_ENGINE_MAG);
    if (!engines)
        return;
    /*
     * The compass is read from the MPU's data-ready interrupt (inv_mpu_ring.c:614-656). With
     * the compass alone, the accel engine stays powered (but out of the FIFO) so the MPU keeps
     * a running sample clock; whether the data-ready interrupt needs it is a device check.
     */
    cfg->accel_engine = cfg->accel || (cfg->mag && !cfg->gyro);
    if (rate < SNS_MIN_RATE_HZ)
        rate = SNS_MIN_RATE_HZ;
    if (rate > SNS_MAX_RATE_HZ)
        rate = SNS_MAX_RATE_HZ;
    if (cfg->mag && !cfg->gyro && !cfg->accel && rate > SNS_MAX_MAG_RATE_HZ)
        rate = SNS_MAX_MAG_RATE_HZ;
    /*
     * Above 100 Hz the driver reads the compass every 10 * rate / 1000 samples
     * (inv_mpu_core.c:655-659): at 101-199 Hz that is every sample, faster than the AK8975
     * converts. 200 Hz gives every second sample, 10 ms apart.
     */
    if (cfg->mag && rate > SNS_MAX_MAG_RATE_HZ)
        rate = SNS_MAX_RATE_HZ;
    cfg->rate_hz = rate;
}

int sns_expected_format(const struct sns_iio_config *cfg)
{
    return (cfg->gyro ? SNS_FORMAT_GYRO : 0) | (cfg->accel ? SNS_FORMAT_ACCEL : 0) |
           (cfg->mag ? SNS_FORMAT_COMPASS : 0);
}

const char *const sns_scan_names[SNS_SCAN_COUNT] = {
    "in_quaternion_r", "in_quaternion_x", "in_quaternion_y", "in_quaternion_z",
    "in_anglvel_x", "in_anglvel_y", "in_anglvel_z",
    "in_accel_x", "in_accel_y", "in_accel_z",
    "in_magn_x", "in_magn_y", "in_magn_z",
    "in_timestamp",
};

static int add_write(struct sns_write *out, int max, int *n, const char *path, const char *value,
                     int optional)
{
    if (*n >= max)
        return -1;
    snprintf(out[*n].path, sizeof(out[*n].path), "%s", path);
    snprintf(out[*n].value, sizeof(out[*n].value), "%s", value);
    out[*n].optional = optional;
    (*n)++;
    return 0;
}

int sns_plan_iio(const struct sns_iio_config *cfg, int firmware_loaded, const char *trigger_name,
                 struct sns_write *out, int max)
{
    char path[64], rate[16];
    int n = 0, i, err = 0;

#define W(p, v, opt) do { err |= add_write(out, max, &n, (p), (v), (opt)); } while (0)
    if (cfg->rate_hz <= 0) {
        /* Nothing on: stop the buffer, then let the chip sleep (spec §4.6). */
        W("buffer/enable", "0", 1);
        W("power_state", "0", 0);
        return err ? -1 : n;
    }
    snprintf(rate, sizeof(rate), "%d", cfg->rate_hz);
    W("power_state", "1", 0);
    /* Configuration writes fail with EPERM while the buffer runs (inv_mpu_core.c:546-549). */
    W("buffer/enable", "0", 1);
    if (firmware_loaded)
        W("dmp_on", "0", 0);
    W("gyro_enable", cfg->gyro ? "1" : "0", 0);
    W("accl_enable", cfg->accel_engine ? "1" : "0", 0);
    W("compass_enable", cfg->mag ? "1" : "0", 0);
    /* Index form: 3 = +-2000 dps, 0 = +-2 g (inv_mpu_core.c:466-504), the probe defaults. */
    W("in_anglvel_scale", "3", 1);
    W("in_accel_scale", "0", 1);
    W("sampling_frequency", rate, 0);
    for (i = 0; i < SNS_SCAN_TIMESTAMP; i++) {
        int on = (i >= SNS_SCAN_GYRO_X && i <= SNS_SCAN_GYRO_Z && cfg->gyro) ||
                 (i >= SNS_SCAN_ACCEL_X && i <= SNS_SCAN_ACCEL_Z && cfg->accel) ||
                 (i >= SNS_SCAN_MAGN_X && i <= SNS_SCAN_MAGN_Z && cfg->mag);
        snprintf(path, sizeof(path), "scan_elements/%s_en", sns_scan_names[i]);
        W(path, on ? "1" : "0", 0);
    }
    W("scan_elements/in_timestamp_en", "1", 0);
    if (trigger_name && trigger_name[0])
        W("trigger/current_trigger", trigger_name, 1);
    W("buffer/length", "480", 1);
    W("buffer/enable", "1", 0);
#undef W
    return err ? -1 : n;
}

/* ---- Records ------------------------------------------------------------------------- */

int sns_parse_scan_type(const char *s, struct sns_scan_type *t)
{
    char *end;
    long v;

    memset(t, 0, sizeof(*t));
    if (!s || (s[0] != 'l' && s[0] != 'b') || s[1] != 'e' || s[2] != ':')
        return -1;
    t->big_endian = s[0] == 'b';
    if (s[3] != 's' && s[3] != 'u')
        return -1;
    t->is_signed = s[3] == 's';
    v = strtol(s + 4, &end, 10);
    if (end == s + 4 || *end != '/' || v <= 0 || v > 64)
        return -1;
    t->bits = (int)v;
    s = end + 1;
    v = strtol(s, &end, 10);
    if (end == s || (v != 8 && v != 16 && v != 32 && v != 64) || v < t->bits)
        return -1;
    t->storage_bits = (int)v;
    if (end[0] != '>' || end[1] != '>')
        return -1;
    s = end + 2;
    v = strtol(s, &end, 10);
    if (end == s || v < 0 || v + t->bits > t->storage_bits)
        return -1;
    t->shift = (int)v;
    while (*end == '\n' || *end == ' ')
        end++;
    return *end ? -1 : 0;
}

void sns_default_scan_type(int index, struct sns_scan_type *t)
{
    memset(t, 0, sizeof(*t));
    t->is_signed = 1;
    if (index == SNS_SCAN_TIMESTAMP)
        t->bits = t->storage_bits = 64;
    else if (index <= SNS_SCAN_QUAT_Z)
        t->bits = t->storage_bits = 32;
    else
        t->bits = t->storage_bits = 16;
}

int sns_layout_build(const int enabled[SNS_SCAN_COUNT], const struct sns_scan_type types[SNS_SCAN_COUNT],
                     struct sns_layout *l)
{
    int i, bytes = 0, len;

    memset(l, 0, sizeof(*l));
    for (i = 0; i < SNS_SCAN_COUNT; i++) {
        l->offset[i] = -1;
        l->type[i] = types[i];
    }
    if (!enabled[SNS_SCAN_TIMESTAMP])
        return -1;
    for (i = 0; i < SNS_SCAN_TIMESTAMP; i++) {
        if (!enabled[i])
            continue;
        len = types[i].storage_bits / 8;
        if (len <= 0)
            return -1;
        bytes = (bytes + len - 1) / len * len;
        l->offset[i] = bytes;
        bytes += len;
    }
    /* The driver puts the format byte just before the 8-byte aligned timestamp
     * (inv_mpu_ring.c:683-687). */
    l->format_offset = bytes ? ((bytes + 7) & ~7) - 1 : -1;
    len = types[SNS_SCAN_TIMESTAMP].storage_bits / 8;
    if (len != 8)
        return -1;
    bytes = (bytes + len - 1) / len * len;
    l->offset[SNS_SCAN_TIMESTAMP] = bytes;
    l->size = bytes + len;
    return 0;
}

static int64_t extract(const uint8_t *p, const struct sns_scan_type *t)
{
    int len = t->storage_bits / 8, i;
    uint64_t v = 0;

    for (i = 0; i < len; i++) {
        int b = t->big_endian ? i : len - 1 - i;
        v = (v << 8) | p[b];
    }
    v >>= t->shift;
    if (t->bits < 64) {
        v &= (1ULL << t->bits) - 1;
        if (t->is_signed && (v & (1ULL << (t->bits - 1))))
            v |= ~((1ULL << t->bits) - 1);
    }
    return (int64_t)v;
}

static int read_triple(const uint8_t *rec, const struct sns_layout *l, int first, int32_t out[3])
{
    int i;

    for (i = 0; i < 3; i++) {
        if (l->offset[first + i] < 0)
            return 0;
        out[i] = (int32_t)extract(rec + l->offset[first + i], &l->type[first + i]);
    }
    return 1;
}

int sns_parse_record(const uint8_t *rec, const struct sns_layout *l, int expected_format,
                     struct sns_sample *s)
{
    memset(s, 0, sizeof(*s));
    s->format = l->format_offset >= 0 ? rec[l->format_offset] : 0;
    if (s->format != expected_format)
        return -1;
    s->has_gyro = (s->format & SNS_FORMAT_GYRO) && read_triple(rec, l, SNS_SCAN_GYRO_X, s->gyro);
    s->has_accel = (s->format & SNS_FORMAT_ACCEL) && read_triple(rec, l, SNS_SCAN_ACCEL_X, s->accel);
    s->has_mag = (s->format & SNS_FORMAT_COMPASS) && read_triple(rec, l, SNS_SCAN_MAGN_X, s->mag);
    /* The driver leaves the compass triple at zero when it has no new sample (spec §4.4). */
    if (s->has_mag && !s->mag[0] && !s->mag[1] && !s->mag[2])
        s->has_mag = 0;
    s->timestamp = extract(rec + l->offset[SNS_SCAN_TIMESTAMP], &l->type[SNS_SCAN_TIMESTAMP]);
    return 0;
}

/* ---- Matrices and units -------------------------------------------------------------- */

const int sns_identity[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };

int sns_parse_matrix(const char *s, int m[9])
{
    int i, j;
    char *end;

    for (i = 0; i < 9; i++) {
        long v = strtol(s, &end, 10);
        if (end == s || v < -1 || v > 1)
            return 0;
        m[i] = (int)v;
        s = end;
        while (*s == ' ')
            s++;
        if (i < 8) {
            if (*s != ',')
                return 0;
            s++;
        }
    }
    while (*s == '\n' || *s == ' ')
        s++;
    if (*s)
        return 0;
    for (i = 0; i < 3; i++) {
        int row = 0, col = 0;
        for (j = 0; j < 3; j++) {
            row += m[3 * i + j] != 0;
            col += m[3 * j + i] != 0;
        }
        if (row != 1 || col != 1)
            return 0;
    }
    return m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) +
           m[2] * (m[3] * m[7] - m[4] * m[6]);
}

void sns_apply_matrix(const int m[9], const int32_t in[3], float out[3])
{
    int i;

    for (i = 0; i < 3; i++)
        out[i] = (float)(m[3 * i] * in[0] + m[3 * i + 1] * in[1] + m[3 * i + 2] * in[2]);
}

float sns_gyro_scale(int fsr_dps)
{
    return (float)(fsr_dps / 32768.0 * M_PI / 180.0);
}

float sns_accel_scale(int fsr_g)
{
    return (float)(fsr_g * 9.80665 / 32768.0);
}

float sns_mag_scale(long long q30)
{
    return (float)((double)q30 / 1073741824.0);
}

int sns_parse_ll(const char *s, long long *v)
{
    char *end;

    if (!s)
        return -1;
    errno = 0;
    *v = strtoll(s, &end, 10);
    if (end == s || errno)
        return -1;
    while (*end == ' ' || *end == '\n')
        end++;
    return *end ? -1 : 0;
}

/* ---- Clocks -------------------------------------------------------------------------- */

int sns_parse_temperature(const char *s, long long *temp, int64_t *robust_ns)
{
    char *end;
    long long t, r;

    errno = 0;
    t = strtoll(s, &end, 10);
    if (end == s || *end != ' ' || errno)
        return -1;
    s = end;
    r = strtoll(s, &end, 10);
    if (end == s || errno || r <= 0)
        return -1;
    while (*end == ' ' || *end == '\n')
        end++;
    if (*end)
        return -1;
    *temp = t;
    *robust_ns = r;
    return 0;
}

int sns_clock_offset(const struct sns_clock_sample *s, int n, int64_t *offset, int64_t *width)
{
    int i, best = -1;

    for (i = 0; i < n; i++) {
        if (s[i].after < s[i].before || s[i].robust <= 0)
            continue;
        if (best < 0 || s[i].after - s[i].before < s[best].after - s[best].before)
            best = i;
    }
    if (best < 0)
        return -1;
    *offset = s[best].before + (s[best].after - s[best].before) / 2 - s[best].robust;
    if (width)
        *width = s[best].after - s[best].before;
    return 0;
}

int64_t sns_clock_convert(int64_t ts, int64_t now_src, int64_t now_dst)
{
    return ts + (now_dst - now_src);
}

int64_t sns_clamp_timestamp(int64_t prev, int64_t ts, int64_t now)
{
    if (ts > now)
        ts = now;
    if (prev && ts <= prev)
        ts = prev + 1;
    return ts;
}

/* ---- Magnetometer hard-iron calibration ---------------------------------------------- */

void sns_magcal_init(struct sns_magcal *c)
{
    memset(c, 0, sizeof(*c));
    c->accuracy = SNS_ACCURACY_UNRELIABLE;
}

/* Solves the n x n system a x = b in place (partial pivoting). Returns 0 or -1 if singular. */
static int solve(double a[4][4], double b[4], int n, double x[4])
{
    int i, j, k;
    double scale = 0;

    for (i = 0; i < n; i++)
        for (j = 0; j < n; j++)
            if (fabs(a[i][j]) > scale)
                scale = fabs(a[i][j]);
    if (scale == 0)
        return -1;
    for (k = 0; k < n; k++) {
        int p = k;
        for (i = k + 1; i < n; i++)
            if (fabs(a[i][k]) > fabs(a[p][k]))
                p = i;
        if (fabs(a[p][k]) < 1e-12 * scale)
            return -1;
        if (p != k) {
            for (j = 0; j < n; j++) {
                double t = a[k][j]; a[k][j] = a[p][j]; a[p][j] = t;
            }
            { double t = b[k]; b[k] = b[p]; b[p] = t; }
        }
        for (i = k + 1; i < n; i++) {
            double f = a[i][k] / a[k][k];
            for (j = k; j < n; j++)
                a[i][j] -= f * a[k][j];
            b[i] -= f * b[k];
        }
    }
    for (i = n - 1; i >= 0; i--) {
        double s = b[i];
        for (j = i + 1; j < n; j++)
            s -= a[i][j] * x[j];
        x[i] = s / a[i][i];
    }
    return 0;
}

/* Smallest eigenvalue of a symmetric 3x3 matrix (trigonometric method). */
static double min_eigen(double m[3][3])
{
    double p1 = m[0][1] * m[0][1] + m[0][2] * m[0][2] + m[1][2] * m[1][2];
    double q, p2, p, b[3][3], r, phi, det;
    int i, j;

    if (p1 == 0) {
        double e = m[0][0];
        if (m[1][1] < e) e = m[1][1];
        if (m[2][2] < e) e = m[2][2];
        return e;
    }
    q = (m[0][0] + m[1][1] + m[2][2]) / 3;
    p2 = (m[0][0] - q) * (m[0][0] - q) + (m[1][1] - q) * (m[1][1] - q) +
         (m[2][2] - q) * (m[2][2] - q) + 2 * p1;
    p = sqrt(p2 / 6);
    for (i = 0; i < 3; i++)
        for (j = 0; j < 3; j++)
            b[i][j] = (m[i][j] - (i == j ? q : 0)) / p;
    det = b[0][0] * (b[1][1] * b[2][2] - b[1][2] * b[2][1]) -
          b[0][1] * (b[1][0] * b[2][2] - b[1][2] * b[2][0]) +
          b[0][2] * (b[1][0] * b[2][1] - b[1][1] * b[2][0]);
    r = det / 2;
    if (r <= -1)
        phi = M_PI / 3;
    else if (r >= 1)
        phi = 0;
    else
        phi = acos(r) / 3;
    return q + 2 * p * cos(phi + 2 * M_PI / 3);
}

int sns_sphere_fit(const float (*p)[3], int n, float center[3], float *radius, float *rms,
                   float *thin_sd)
{
    double mean[3] = { 0, 0, 0 }, a[4][4], b[4], x[4], cov[3][3], r2, err = 0, ev;
    int i, j, k;

    if (n < 4)
        return -1;
    for (i = 0; i < n; i++)
        for (j = 0; j < 3; j++)
            mean[j] += p[i][j];
    for (j = 0; j < 3; j++)
        mean[j] /= n;
    memset(a, 0, sizeof(a));
    memset(b, 0, sizeof(b));
    memset(cov, 0, sizeof(cov));
    /* |q|^2 = 2 c.q + (r^2 - |c|^2) with q = p - mean: linear in (c, r^2 - |c|^2). */
    for (i = 0; i < n; i++) {
        double q[4], s;
        for (j = 0; j < 3; j++)
            q[j] = p[i][j] - mean[j];
        q[3] = 1;
        s = q[0] * q[0] + q[1] * q[1] + q[2] * q[2];
        for (j = 0; j < 4; j++) {
            for (k = 0; k < 4; k++)
                a[j][k] += q[j] * q[k];
            b[j] += q[j] * s;
        }
        for (j = 0; j < 3; j++)
            for (k = 0; k < 3; k++)
                cov[j][k] += q[j] * q[k] / n;
    }
    if (solve(a, b, 4, x))
        return -1;
    for (j = 0; j < 3; j++)
        x[j] /= 2;
    r2 = x[3] + x[0] * x[0] + x[1] * x[1] + x[2] * x[2];
    if (!(r2 > 0))
        return -1;
    for (i = 0; i < n; i++) {
        double d = 0, e;
        for (j = 0; j < 3; j++) {
            double t = p[i][j] - mean[j] - x[j];
            d += t * t;
        }
        e = sqrt(d) - sqrt(r2);
        err += e * e;
    }
    for (j = 0; j < 3; j++)
        center[j] = (float)(mean[j] + x[j]);
    *radius = (float)sqrt(r2);
    *rms = (float)sqrt(err / n);
    ev = min_eigen(cov);
    *thin_sd = (float)sqrt(ev > 0 ? ev : 0);
    return 0;
}

int sns_magcal_add(struct sns_magcal *c, const float v[3])
{
    float center[3], radius, rms, thin;
    int i;

    for (i = 0; i < 3; i++)
        if (!isfinite(v[i]) || fabsf(v[i]) > 5000.0f)
            return 0;
    /* Keep only points spread over the sphere: a still device adds one. */
    for (i = 0; i < c->n; i++) {
        float dx = c->pts[i][0] - v[0], dy = c->pts[i][1] - v[1], dz = c->pts[i][2] - v[2];
        if (dx * dx + dy * dy + dz * dz < SNS_MAGCAL_MIN_SPACING * SNS_MAGCAL_MIN_SPACING)
            return 0;
    }
    memcpy(c->pts[c->next], v, sizeof(c->pts[0]));
    c->next = (c->next + 1) % SNS_MAGCAL_POINTS;
    if (c->n < SNS_MAGCAL_POINTS)
        c->n++;
    if (++c->since_fit < SNS_MAGCAL_REFIT_EVERY || c->n < SNS_MAGCAL_MIN_POINTS)
        return 0;
    c->since_fit = 0;
    if (sns_sphere_fit((const float (*)[3])c->pts, c->n, center, &radius, &rms, &thin))
        return 0;
    /*
     * Accept only a plausible field strength, points that lie on a sphere (hard iron only;
     * soft iron or a changing field raises the residual), and points that are not in one
     * plane: a head turning about one axis traces a circle, which fixes no center.
     */
    if (radius < SNS_MAGCAL_MIN_RADIUS || radius > SNS_MAGCAL_MAX_RADIUS)
        return 0;
    if (rms > (radius * 0.1f > 1.5f ? radius * 0.1f : 1.5f))
        return 0;
    if (thin < 0.12f * radius)
        return 0;
    for (i = 0; i < 3; i++)
        if (fabsf(center[i]) > SNS_MAGCAL_MAX_BIAS)
            return 0;
    memcpy(c->bias, center, sizeof(c->bias));
    c->radius = radius;
    c->valid = 1;
    c->accuracy = SNS_ACCURACY_HIGH;
    return 1;
}

int sns_magcal_format(const struct sns_magcal *c, char *buf, size_t len)
{
    return snprintf(buf, len, "glass-magcal 1 %.3f %.3f %.3f %.3f\n",
                    c->bias[0], c->bias[1], c->bias[2], c->radius);
}

int sns_magcal_parse(struct sns_magcal *c, const char *text)
{
    float b[3], r;
    int version, used = 0, i;

    if (sscanf(text, "glass-magcal %d %f %f %f %f%n", &version, &b[0], &b[1], &b[2], &r, &used) != 5
        || version != 1)
        return -1;
    text += used;
    while (*text == ' ' || *text == '\n')
        text++;
    if (*text)
        return -1;
    for (i = 0; i < 3; i++)
        if (!isfinite(b[i]) || fabsf(b[i]) > SNS_MAGCAL_MAX_BIAS)
            return -1;
    if (!(r >= SNS_MAGCAL_MIN_RADIUS && r <= SNS_MAGCAL_MAX_RADIUS))
        return -1;
    memcpy(c->bias, b, sizeof(c->bias));
    c->radius = r;
    c->valid = 1;
    /* A saved bias may be stale (new magnetized parts nearby): medium until refitted. */
    c->accuracy = SNS_ACCURACY_MEDIUM;
    return 0;
}

/* ---- Gyroscope bias at rest ---------------------------------------------------------- */

void sns_gyrobias_init(struct sns_gyrobias *g)
{
    memset(g, 0, sizeof(*g));
}

int sns_gyrobias_add(struct sns_gyrobias *g, const float w[3], int64_t ts)
{
    double mean[3];
    int i, still = 1;

    if (g->count == 0 || ts < g->start) {
        g->count = 0;
        memset(g->sum, 0, sizeof(g->sum));
        memset(g->sumsq, 0, sizeof(g->sumsq));
        g->start = ts;
    }
    for (i = 0; i < 3; i++) {
        g->sum[i] += w[i];
        g->sumsq[i] += (double)w[i] * w[i];
    }
    g->count++;
    if (ts - g->start < SNS_GYRO_REST_WINDOW_NS)
        return 0;
    if (g->count < SNS_GYRO_REST_MIN_SAMPLES) {
        g->count = 0;
        return 0;
    }
    for (i = 0; i < 3; i++) {
        double var;
        mean[i] = g->sum[i] / g->count;
        var = g->sumsq[i] / g->count - mean[i] * mean[i];
        if (var > (double)SNS_GYRO_REST_MAX_SD * SNS_GYRO_REST_MAX_SD ||
            fabs(mean[i]) > SNS_GYRO_MAX_BIAS)
            still = 0;
    }
    g->count = 0;
    if (!still)
        return 0;
    for (i = 0; i < 3; i++)
        g->bias[i] = (float)mean[i];
    g->valid = 1;
    return 1;
}

/* ---- Event queue --------------------------------------------------------------------- */

void sns_queue_init(struct sns_queue *q, void *items, unsigned char *meta, size_t size, int cap)
{
    memset(q, 0, sizeof(*q));
    q->items = items;
    q->meta = meta;
    q->size = size;
    q->cap = cap;
}

static int slot(const struct sns_queue *q, int pos)
{
    return (q->head + pos) % q->cap;
}

int sns_queue_push(struct sns_queue *q, const void *item, int is_meta)
{
    int dropped = 0, pos, j;

    if (q->count == q->cap) {
        for (pos = 0; pos < q->count && q->meta[slot(q, pos)]; pos++)
            ;
        if (pos == q->count)
            return -1;
        /* Close the gap at pos by moving the older items up one slot, keeping their order. */
        for (j = pos; j > 0; j--) {
            memcpy(q->items + (size_t)slot(q, j) * q->size,
                   q->items + (size_t)slot(q, j - 1) * q->size, q->size);
            q->meta[slot(q, j)] = q->meta[slot(q, j - 1)];
        }
        q->head = slot(q, 1);
        q->count--;
        q->dropped++;
        dropped = 1;
    }
    pos = slot(q, q->count);
    memcpy(q->items + (size_t)pos * q->size, item, q->size);
    q->meta[pos] = (unsigned char)!!is_meta;
    q->count++;
    return dropped;
}

int sns_queue_pop(struct sns_queue *q, void *out)
{
    if (!q->count)
        return 0;
    memcpy(out, q->items + (size_t)q->head * q->size, q->size);
    q->head = slot(q, 1);
    q->count--;
    return 1;
}

/* ---- Files and discovery ------------------------------------------------------------- */

static void join(char *out, size_t len, const char *root, const char *path)
{
    snprintf(out, len, "%s%s", root ? root : "", path);
}

int sns_read_file(const char *root, const char *path, char *buf, size_t len)
{
    char full[256];
    ssize_t n;
    int fd;

    if (len == 0)
        return -EINVAL;
    join(full, sizeof(full), root, path);
    fd = open(full, O_RDONLY);
    if (fd < 0)
        return -errno;
    n = read(fd, buf, len - 1);
    if (n < 0) {
        int err = -errno;
        close(fd);
        return err;
    }
    close(fd);
    buf[n] = '\0';
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == ' '))
        buf[--n] = '\0';
    return (int)n;
}

int sns_write_file(const char *root, const char *path, const char *value)
{
    char full[256];
    size_t len = strlen(value);
    ssize_t n;
    int fd, err = 0;

    join(full, sizeof(full), root, path);
    fd = open(full, O_WRONLY);
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

static int numbered(const char *entry, const char *prefix)
{
    size_t len = strlen(prefix);
    char *end;
    long v;

    if (strncmp(entry, prefix, len) != 0 || !entry[len])
        return -1;
    v = strtol(entry + len, &end, 10);
    return (*end || v < 0 || v > 9999) ? -1 : (int)v;
}

int sns_find_input(const char *root, const char *name, int *input_num, int *event_num)
{
    char dir[256], path[sizeof(dir) + 2 * 256], buf[64];
    struct dirent *e;
    DIR *d, *sub;
    int found = -ENOENT;

    join(dir, sizeof(dir), root, "/sys/class/input");
    d = opendir(dir);
    if (!d)
        return -ENOENT;
    while (found && (e = readdir(d)) != NULL) {
        int in = numbered(e->d_name, "input");
        if (in < 0)
            continue;
        snprintf(path, sizeof(path), "/sys/class/input/%s/name", e->d_name);
        if (sns_read_file(root, path, buf, sizeof(buf)) < 0 || strcmp(buf, name) != 0)
            continue;
        snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        sub = opendir(path);
        if (!sub)
            continue;
        while ((e = readdir(sub)) != NULL) {
            int ev = numbered(e->d_name, "event");
            if (ev >= 0) {
                *input_num = in;
                *event_num = ev;
                found = 0;
                break;
            }
        }
        closedir(sub);
    }
    closedir(d);
    return found;
}

int sns_find_iio(const char *root, const char *name)
{
    char dir[256], path[sizeof(dir) + 2 * 256], buf[64];
    struct dirent *e;
    DIR *d;
    int found = -ENOENT;

    join(dir, sizeof(dir), root, "/sys/bus/iio/devices");
    d = opendir(dir);
    if (!d)
        return -ENOENT;
    while (found < 0 && (e = readdir(d)) != NULL) {
        int n = numbered(e->d_name, "iio:device");
        if (n < 0)
            continue;
        snprintf(path, sizeof(path), "/sys/bus/iio/devices/%s/name", e->d_name);
        if (sns_read_file(root, path, buf, sizeof(buf)) >= 0 && strcmp(buf, name) == 0)
            found = n;
    }
    closedir(d);
    return found;
}

int sns_find_trigger(const char *root, const char *name)
{
    char dir[256], path[sizeof(dir) + 2 * 256], buf[64];
    struct dirent *e;
    DIR *d;
    int found = -ENOENT;

    join(dir, sizeof(dir), root, "/sys/bus/iio/devices");
    d = opendir(dir);
    if (!d)
        return -ENOENT;
    while (found < 0 && (e = readdir(d)) != NULL) {
        int n = numbered(e->d_name, "trigger");
        if (n < 0)
            continue;
        snprintf(path, sizeof(path), "/sys/bus/iio/devices/%s/name", e->d_name);
        if (sns_read_file(root, path, buf, sizeof(buf)) >= 0 && strcmp(buf, name) == 0)
            found = n;
    }
    closedir(d);
    return found;
}
