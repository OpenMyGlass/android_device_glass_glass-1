/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Sensors HAL for Glass Explorer Edition (sensors.glass_1): legacy sensors_poll_device_1 at
 * device API 1.1, which android-4.4.4_r2 and android-6.0.1_r31 both take.
 *
 * Accelerometer, gyroscope and magnetometer come raw from the InvenSense MPU-9150 IIO driver
 * (no DMP firmware, no MPL library); light comes from the LTR-506ALS input device. One reader
 * thread polls both and queues events; poll() hands them out. Orientation, rotation vector,
 * gravity and linear acceleration are left to the framework's virtual sensors.
 * Facts: docs/specs/sensors.md. Design: docs/hals.md.
 */
#define LOG_TAG "sensors.glass_1"

#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include <cutils/log.h>
#include <hardware/hardware.h>
#include <hardware/sensors.h>

#include "sensors_logic.h"

/* Remove the at-rest gyro bias from GYROSCOPE events (GYROSCOPE_UNCALIBRATED stays raw). */
#define GLASS_GYRO_BIAS_AT_REST 1

#define IIO_NAME "mpu9150"           /* I2C_BOARD_INFO("mpu9150", 0x68), board-notle.c */
#define ALS_NAME "ltr506_als"        /* ltr506als.c als_setup_input_device */
#define ALS_I2C_ENABLE "/sys/devices/platform/omap_i2c.4/i2c-4/4-003a/als_enable"
#define MAGCAL_FILE "/data/misc/sensors/glass_magcal.txt"
#define QUEUE_LEN 256
#define OFFSET_REFRESH_NS (10LL * 1000000000LL)
#define MAGCAL_SAVE_EVERY_NS (300LL * 1000000000LL)
#define DEFAULT_DELAY_NS 200000000LL /* SENSOR_DELAY_NORMAL */

_Static_assert(SNS_TYPE_ACCELEROMETER == SENSOR_TYPE_ACCELEROMETER, "type");
_Static_assert(SNS_TYPE_MAGNETIC_FIELD == SENSOR_TYPE_MAGNETIC_FIELD, "type");
_Static_assert(SNS_TYPE_GYROSCOPE == SENSOR_TYPE_GYROSCOPE, "type");
_Static_assert(SNS_TYPE_LIGHT == SENSOR_TYPE_LIGHT, "type");
_Static_assert(SNS_TYPE_MAGNETIC_FIELD_UNCALIBRATED == SENSOR_TYPE_MAGNETIC_FIELD_UNCALIBRATED, "type");
_Static_assert(SNS_TYPE_GYROSCOPE_UNCALIBRATED == SENSOR_TYPE_GYROSCOPE_UNCALIBRATED, "type");
_Static_assert(SNS_ACCURACY_HIGH == SENSOR_STATUS_ACCURACY_HIGH, "status");
_Static_assert(SNS_ACCURACY_UNRELIABLE == SENSOR_STATUS_UNRELIABLE, "status");
_Static_assert(SNS_HANDLE_MAX < SENSORS_HANDLE_COUNT, "handles");

struct glass_ctx {
    sensors_poll_device_1_t dev; /* first: the framework's pointer is ours */
    pthread_mutex_t lock;
    pthread_cond_t cond;
    pthread_t thread;
    int thread_started;
    int stop;
    int wake[2];

    int enabled[SNS_HANDLE_MAX + 1];
    int64_t delay[SNS_HANDLE_MAX + 1];
    int64_t last_ts[SNS_HANDLE_MAX + 1];

    /* MPU-9150 over IIO */
    char iio[64];                /* "/sys/bus/iio/devices/iio:deviceN/" */
    char trigger[64];
    int iio_fd;
    struct sns_iio_config cfg;
    struct sns_layout layout;
    int format;
    int gyro_m[9], accel_m[9], mag_m[9];
    float gyro_scale, accel_scale, mag_scale;
    int64_t offset;              /* robust clock -> CLOCK_BOOTTIME */
    int offset_state;            /* 0 none, 1 estimated from data, 2 measured */
    int64_t offset_at;
    unsigned bad_records;

    struct sns_magcal magcal;
    int magcal_dirty;
    int64_t magcal_saved_at;
    struct sns_gyrobias gbias;

    /* LTR-506ALS */
    int als_fd;
    char als_enable[64];
    int als_clock_mono;
    int als_value;
    int als_pending;

    sensors_event_t queue[QUEUE_LEN];
    int q_head, q_count;
    unsigned dropped;
};

static int64_t now_ns(clockid_t clk)
{
    struct timespec ts;

    clock_gettime(clk, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static void iio_path(const struct glass_ctx *c, const char *rel, char *out, size_t len)
{
    if (rel[0] == '/')
        snprintf(out, len, "%s", rel);
    else
        snprintf(out, len, "%s%s", c->iio, rel);
}

static int iio_read(const struct glass_ctx *c, const char *rel, char *buf, size_t len)
{
    char path[160];

    iio_path(c, rel, path, sizeof(path));
    return sns_read_file(NULL, path, buf, len);
}

static int iio_write(const struct glass_ctx *c, const char *rel, const char *value)
{
    char path[160];

    iio_path(c, rel, path, sizeof(path));
    return sns_write_file(NULL, path, value);
}

static long long iio_read_ll(const struct glass_ctx *c, const char *rel, long long dflt)
{
    char buf[64];
    long long v;

    if (iio_read(c, rel, buf, sizeof(buf)) < 0 || sns_parse_ll(buf, &v) || v <= 0) {
        ALOGW("cannot read %s, using %lld", rel, dflt);
        return dflt;
    }
    return v;
}

/* ---- Event queue (lock held) --------------------------------------------------------- */

static void push_event(struct glass_ctx *c, const sensors_event_t *ev)
{
    if (c->q_count == QUEUE_LEN) {
        c->q_head = (c->q_head + 1) % QUEUE_LEN;
        c->q_count--;
        if (c->dropped++ % 1000 == 0)
            ALOGW("event queue full, dropped %u events", c->dropped);
    }
    c->queue[(c->q_head + c->q_count) % QUEUE_LEN] = *ev;
    c->q_count++;
}

static void new_event(sensors_event_t *ev, int handle, int64_t ts)
{
    const struct sns_desc *d = sns_find(handle);

    memset(ev, 0, sizeof(*ev));
    ev->version = sizeof(*ev);
    ev->sensor = handle;
    ev->type = d ? d->type : 0;
    ev->timestamp = ts;
}

/* ---- MPU-9150 ------------------------------------------------------------------------ */

static void read_matrix(struct glass_ctx *c, const char *rel, int m[9])
{
    char buf[96];
    int det = 0;

    if (iio_read(c, rel, buf, sizeof(buf)) >= 0)
        det = sns_parse_matrix(buf, m);
    if (det == 0) {
        ALOGE("%s%s is missing or not a signed permutation; using identity", c->iio, rel);
        memcpy(m, sns_identity, sizeof(sns_identity));
    } else if (det < 0) {
        ALOGW("%s%s is a reflection (determinant -1)", c->iio, rel);
    }
}

static void iio_init(struct glass_ctx *c)
{
    char node[32];
    int n = sns_find_iio(NULL, IIO_NAME);

    c->iio_fd = -1;
    if (n < 0) {
        ALOGE("no IIO device named %s; motion sensors unavailable", IIO_NAME);
        return;
    }
    snprintf(c->iio, sizeof(c->iio), "/sys/bus/iio/devices/iio:device%d/", n);
    snprintf(node, sizeof(node), "/dev/iio:device%d", n);
    read_matrix(c, "gyro_matrix", c->gyro_m);
    read_matrix(c, "accl_matrix", c->accel_m);
    read_matrix(c, "compass_matrix", c->mag_m);
    /* The driver names its trigger "<name>-dev<id>" (inv_mpu_trigger.c:67). */
    if (sns_read_file(NULL, "/sys/bus/iio/devices/trigger0/name", c->trigger, sizeof(c->trigger)) <= 0)
        snprintf(c->trigger, sizeof(c->trigger), "%s-dev%d", IIO_NAME, n);
    c->iio_fd = open(node, O_RDONLY | O_NONBLOCK);
    if (c->iio_fd < 0)
        ALOGE("cannot open %s: %s", node, strerror(errno));
    /* Start from a known state: buffer off, chip asleep. */
    iio_write(c, "buffer/enable", "0");
    iio_write(c, "power_state", "0");
}

/* Measures the robust-clock offset from the temperature attribute (chip powered). */
static void measure_offset(struct glass_ctx *c)
{
    struct sns_clock_sample s[3];
    int i, n = 0;
    int64_t offset, width;

    for (i = 0; i < 3; i++) {
        char buf[64];
        long long temp;

        s[n].before = now_ns(CLOCK_BOOTTIME);
        if (iio_read(c, "temperature", buf, sizeof(buf)) < 0)
            continue;
        s[n].after = now_ns(CLOCK_BOOTTIME);
        if (sns_parse_temperature(buf, &temp, &s[n].robust) == 0)
            n++;
    }
    if (sns_clock_offset(s, n, &offset, &width) == 0 && width < 20000000) {
        if (c->offset_state == 2 && llabs(offset - c->offset) > 1000000)
            ALOGI("robust clock offset moved by %lld us", (long long)(offset - c->offset) / 1000);
        c->offset = offset;
        c->offset_state = 2;
    }
    c->offset_at = now_ns(CLOCK_BOOTTIME);
}

static void build_layout(struct glass_ctx *c)
{
    int on[SNS_SCAN_COUNT], i;
    struct sns_scan_type types[SNS_SCAN_COUNT];

    for (i = 0; i < SNS_SCAN_COUNT; i++) {
        char rel[64], buf[32];
        on[i] = i == SNS_SCAN_TIMESTAMP ||
                (i >= SNS_SCAN_GYRO_X && i <= SNS_SCAN_GYRO_Z && c->cfg.gyro) ||
                (i >= SNS_SCAN_ACCEL_X && i <= SNS_SCAN_ACCEL_Z && c->cfg.accel) ||
                (i >= SNS_SCAN_MAGN_X && i <= SNS_SCAN_MAGN_Z && c->cfg.mag);
        snprintf(rel, sizeof(rel), "scan_elements/%s_type", sns_scan_names[i]);
        if (!on[i] || iio_read(c, rel, buf, sizeof(buf)) < 0 || sns_parse_scan_type(buf, &types[i]))
            sns_default_scan_type(i, &types[i]);
    }
    if (sns_layout_build(on, types, &c->layout)) {
        ALOGE("unusable scan element layout");
        memset(&c->layout, 0, sizeof(c->layout));
    }
    c->format = sns_expected_format(&c->cfg);
}

static int run_plan(struct glass_ctx *c, const struct sns_iio_config *cfg)
{
    struct sns_write w[SNS_MAX_WRITES];
    char buf[16];
    int fw, n, i, err;

    fw = iio_read(c, "firmware_loaded", buf, sizeof(buf)) > 0 && strcmp(buf, "1") == 0;
    n = sns_plan_iio(cfg, fw, c->trigger, w, SNS_MAX_WRITES);
    if (n < 0)
        return -EINVAL;
    for (i = 0; i < n; i++) {
        err = iio_write(c, w[i].path, w[i].value);
        if (!err)
            continue;
        if (w[i].optional) {
            ALOGW("cannot write %s to %s%s: %s", w[i].value, c->iio, w[i].path, strerror(-err));
            continue;
        }
        ALOGE("cannot write %s to %s%s: %s", w[i].value, c->iio, w[i].path, strerror(-err));
        return err;
    }
    return 0;
}

/* Moves the driver to the configuration the enabled sensors need (lock held). */
static int apply_config(struct glass_ctx *c)
{
    struct sns_iio_config cfg, off;
    int err;

    sns_iio_config_for(c->enabled, c->delay, &cfg);
    if (!memcmp(&cfg, &c->cfg, sizeof(cfg)))
        return 0;
    if (cfg.rate_hz && c->iio_fd < 0)
        return -ENODEV;
    err = run_plan(c, &cfg);
    if (err) {
        memset(&off, 0, sizeof(off));
        run_plan(c, &off);
        memset(&c->cfg, 0, sizeof(c->cfg));
        memset(&c->layout, 0, sizeof(c->layout));
        return err;
    }
    c->cfg = cfg;
    if (!cfg.rate_hz) {
        memset(&c->layout, 0, sizeof(c->layout));
        return 0;
    }
    /* Scales read -EINVAL while the chip sleeps (mpu_read_raw), so read them now. */
    c->gyro_scale = sns_gyro_scale((int)iio_read_ll(c, "in_anglvel_scale", 2000));
    c->accel_scale = sns_accel_scale((int)iio_read_ll(c, "in_accel_scale", 2));
    c->mag_scale = sns_mag_scale(iio_read_ll(c, "in_magn_scale", 9830LL << 15));
    build_layout(c);
    measure_offset(c);
    ALOGI("IIO on: gyro %d accel %d mag %d at %d Hz, record %d bytes", cfg.gyro, cfg.accel,
          cfg.mag, cfg.rate_hz, c->layout.size);
    return 0;
}

static void save_magcal(struct glass_ctx *c)
{
    char text[96], tmp[sizeof(MAGCAL_FILE) + 4];
    int len, fd, ok;

    if (!c->magcal.valid)
        return;
    len = sns_magcal_format(&c->magcal, text, sizeof(text));
    snprintf(tmp, sizeof(tmp), "%s.tmp", MAGCAL_FILE);
    fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0660);
    if (fd < 0) {
        ALOGW("cannot save %s: %s", tmp, strerror(errno));
        return;
    }
    ok = write(fd, text, (size_t)len) == len && fsync(fd) == 0;
    close(fd);
    if (!ok || rename(tmp, MAGCAL_FILE)) {
        ALOGW("cannot save %s: %s", MAGCAL_FILE, strerror(errno));
        unlink(tmp);
        return;
    }
    c->magcal_dirty = 0;
    c->magcal_saved_at = now_ns(CLOCK_BOOTTIME);
}

static int64_t stamp(struct glass_ctx *c, int handle, int64_t ts, int64_t now)
{
    ts = sns_clamp_timestamp(c->last_ts[handle], ts, now);
    c->last_ts[handle] = ts;
    return ts;
}

static void process_sample(struct glass_ctx *c, const struct sns_sample *s, int64_t now)
{
    int64_t ts = s->timestamp + c->offset;
    sensors_event_t ev;
    float v[3];
    int i;

    if (s->has_accel && c->enabled[SNS_HANDLE_ACCEL]) {
        sns_apply_matrix(c->accel_m, s->accel, v);
        new_event(&ev, SNS_HANDLE_ACCEL, stamp(c, SNS_HANDLE_ACCEL, ts, now));
        for (i = 0; i < 3; i++)
            ev.acceleration.v[i] = v[i] * c->accel_scale;
        ev.acceleration.status = SENSOR_STATUS_ACCURACY_HIGH;
        push_event(c, &ev);
    }
    if (s->has_gyro) {
        sns_apply_matrix(c->gyro_m, s->gyro, v);
        for (i = 0; i < 3; i++)
            v[i] *= c->gyro_scale;
        if (GLASS_GYRO_BIAS_AT_REST && sns_gyrobias_add(&c->gbias, v, s->timestamp))
            ALOGV("gyro bias %f %f %f", c->gbias.bias[0], c->gbias.bias[1], c->gbias.bias[2]);
        if (c->enabled[SNS_HANDLE_GYRO]) {
            new_event(&ev, SNS_HANDLE_GYRO, stamp(c, SNS_HANDLE_GYRO, ts, now));
            for (i = 0; i < 3; i++)
                ev.gyro.v[i] = v[i] - (GLASS_GYRO_BIAS_AT_REST ? c->gbias.bias[i] : 0);
            ev.gyro.status = c->gbias.valid || !GLASS_GYRO_BIAS_AT_REST ?
                SENSOR_STATUS_ACCURACY_HIGH : SENSOR_STATUS_ACCURACY_MEDIUM;
            push_event(c, &ev);
        }
        if (c->enabled[SNS_HANDLE_GYRO_UNCAL]) {
            new_event(&ev, SNS_HANDLE_GYRO_UNCAL, stamp(c, SNS_HANDLE_GYRO_UNCAL, ts, now));
            for (i = 0; i < 3; i++) {
                ev.uncalibrated_gyro.uncalib[i] = v[i];
                ev.uncalibrated_gyro.bias[i] = GLASS_GYRO_BIAS_AT_REST ? c->gbias.bias[i] : 0;
            }
            push_event(c, &ev);
        }
    }
    if (s->has_mag) {
        sns_apply_matrix(c->mag_m, s->mag, v);
        for (i = 0; i < 3; i++)
            v[i] *= c->mag_scale;
        if (sns_magcal_add(&c->magcal, v)) {
            c->magcal_dirty = 1;
            ALOGD("hard-iron bias %.1f %.1f %.1f uT, field %.1f uT", c->magcal.bias[0],
                  c->magcal.bias[1], c->magcal.bias[2], c->magcal.radius);
        }
        if (c->enabled[SNS_HANDLE_MAG]) {
            new_event(&ev, SNS_HANDLE_MAG, stamp(c, SNS_HANDLE_MAG, ts, now));
            for (i = 0; i < 3; i++)
                ev.magnetic.v[i] = v[i] - c->magcal.bias[i];
            ev.magnetic.status = (int8_t)c->magcal.accuracy;
            push_event(c, &ev);
        }
        if (c->enabled[SNS_HANDLE_MAG_UNCAL]) {
            new_event(&ev, SNS_HANDLE_MAG_UNCAL, stamp(c, SNS_HANDLE_MAG_UNCAL, ts, now));
            for (i = 0; i < 3; i++) {
                ev.uncalibrated_magnetic.uncalib[i] = v[i];
                ev.uncalibrated_magnetic.bias[i] = c->magcal.bias[i];
            }
            push_event(c, &ev);
        }
    }
}

/* Reads and queues whatever the IIO buffer holds (lock held). Returns -1 on a read error. */
static int read_iio(struct glass_ctx *c)
{
    uint8_t buf[48 * 32];
    ssize_t n = read(c->iio_fd, buf, sizeof(buf));
    int64_t now = now_ns(CLOCK_BOOTTIME);
    struct sns_sample s;
    int size = c->layout.size, i, count;

    if (n < 0)
        return errno == EAGAIN ? 0 : -1;
    if (size <= 0 || !c->cfg.rate_hz)
        return 0;
    if (n % size)
        ALOGW("read %zd bytes, not a multiple of the %d-byte record", n, size);
    count = (int)(n / size);
    if (count && c->offset_state == 0 &&
        sns_parse_record(buf + (count - 1) * size, &c->layout, c->format, &s) == 0) {
        /* No measurement yet: the newest record is close to now. */
        c->offset = now - s.timestamp;
        c->offset_state = 1;
    }
    for (i = 0; i < count; i++) {
        if (sns_parse_record(buf + i * size, &c->layout, c->format, &s)) {
            if (c->bad_records++ % 1000 == 0)
                ALOGW("record with data format 0x%02x, expected 0x%02x (%u so far)", s.format,
                      c->format, c->bad_records);
            continue;
        }
        process_sample(c, &s, now);
    }
    if (now - c->offset_at > OFFSET_REFRESH_NS)
        measure_offset(c);
    if (c->magcal_dirty && now - c->magcal_saved_at > MAGCAL_SAVE_EVERY_NS)
        save_magcal(c);
    return 0;
}

/* ---- LTR-506ALS ---------------------------------------------------------------------- */

static void als_init(struct glass_ctx *c)
{
    char node[32];
    int input, event, mono = CLOCK_MONOTONIC;

    c->als_fd = -1;
    if (sns_find_input(NULL, ALS_NAME, &input, &event)) {
        ALOGE("no input device named %s; light sensor unavailable", ALS_NAME);
        return;
    }
    snprintf(node, sizeof(node), "/dev/input/event%d", event);
    snprintf(c->als_enable, sizeof(c->als_enable), "/sys/class/input/input%d/als_enable", input);
    c->als_fd = open(node, O_RDONLY | O_NONBLOCK);
    if (c->als_fd < 0) {
        ALOGE("cannot open %s: %s", node, strerror(errno));
        return;
    }
    /* Event times in CLOCK_MONOTONIC instead of CLOCK_REALTIME (evdev, linux/input.h). */
    c->als_clock_mono = ioctl(c->als_fd, EVIOCSCLOCKID, &mono) == 0;
    if (!c->als_clock_mono)
        ALOGW("EVIOCSCLOCKID failed: %s; converting from CLOCK_REALTIME", strerror(errno));
}

static int als_set(struct glass_ctx *c, int on)
{
    const char *v = on ? "1" : "0";
    int err;

    if (c->als_fd < 0)
        return -ENODEV;
    /* The input device's attribute and the I2C device's share one handler (ltr506als.c). */
    err = sns_write_file(NULL, c->als_enable, v);
    if (err)
        err = sns_write_file(NULL, ALS_I2C_ENABLE, v);
    if (err)
        ALOGE("cannot write %s to %s: %s", v, c->als_enable, strerror(-err));
    return err;
}

static void read_als(struct glass_ctx *c)
{
    struct input_event in[16];
    ssize_t n = read(c->als_fd, in, sizeof(in));
    int i;

    for (i = 0; n > 0 && i < (int)(n / sizeof(in[0])); i++) {
        if (in[i].type == EV_ABS && in[i].code == ABS_MISC) {
            c->als_value = in[i].value;
            c->als_pending = 1;
        } else if (in[i].type == EV_SYN && in[i].code == SYN_REPORT && c->als_pending) {
            int64_t t = (int64_t)in[i].time.tv_sec * 1000000000LL + in[i].time.tv_usec * 1000LL;
            int64_t now = now_ns(CLOCK_BOOTTIME);
            sensors_event_t ev;

            c->als_pending = 0;
            if (!c->enabled[SNS_HANDLE_LIGHT])
                continue;
            t = sns_clock_convert(t, now_ns(c->als_clock_mono ? CLOCK_MONOTONIC : CLOCK_REALTIME), now);
            new_event(&ev, SNS_HANDLE_LIGHT, stamp(c, SNS_HANDLE_LIGHT, t, now));
            /* ADC counts reported as lux, like stock (spec §6); no lux factor is known. */
            ev.light = (float)c->als_value;
            push_event(c, &ev);
        }
    }
}

/* ---- Reader thread ------------------------------------------------------------------- */

static void *reader(void *arg)
{
    struct glass_ctx *c = arg;

    for (;;) {
        struct pollfd fds[3] = {
            { .fd = c->wake[0], .events = POLLIN },
            { .fd = c->iio_fd, .events = POLLIN },
            { .fd = c->als_fd, .events = POLLIN },
        };
        int queued, failed = 0;

        if (poll(fds, 3, -1) < 0) {
            if (errno != EINTR) {
                ALOGE("poll: %s", strerror(errno));
                usleep(100000);
            }
            continue;
        }
        pthread_mutex_lock(&c->lock);
        if (c->stop) {
            pthread_mutex_unlock(&c->lock);
            break;
        }
        queued = c->q_count;
        if (fds[1].revents & POLLIN)
            failed = read_iio(c);
        if (fds[2].revents & POLLIN)
            read_als(c);
        if (c->q_count != queued)
            pthread_cond_broadcast(&c->cond);
        pthread_mutex_unlock(&c->lock);
        if (failed || ((fds[1].revents | fds[2].revents) & (POLLERR | POLLHUP | POLLNVAL)))
            usleep(50000); /* do not spin on a failing descriptor */
    }
    return NULL;
}

/* ---- Device -------------------------------------------------------------------------- */

static struct glass_ctx *ctx_of(void *dev)
{
    return (struct glass_ctx *)dev;
}

static int set_delay_locked(struct glass_ctx *c, int handle, int64_t ns)
{
    c->delay[handle] = ns > 0 ? ns : DEFAULT_DELAY_NS;
    if (handle == SNS_HANDLE_LIGHT || !c->enabled[handle])
        return 0;
    return apply_config(c);
}

static int dev_activate(struct sensors_poll_device_t *dev, int handle, int enabled)
{
    struct glass_ctx *c = ctx_of(dev);
    int was, err;

    if (!sns_find(handle))
        return -EINVAL;
    pthread_mutex_lock(&c->lock);
    was = c->enabled[handle];
    c->enabled[handle] = !!enabled;
    if (handle == SNS_HANDLE_LIGHT) {
        err = was == c->enabled[handle] ? 0 : als_set(c, c->enabled[handle]);
        c->als_pending = 0;
    } else {
        err = apply_config(c);
    }
    if (err) {
        c->enabled[handle] = was;
        if (handle != SNS_HANDLE_LIGHT)
            apply_config(c);
    }
    if (!c->cfg.mag && c->magcal_dirty)
        save_magcal(c);
    pthread_mutex_unlock(&c->lock);
    return err;
}

static int dev_set_delay(struct sensors_poll_device_t *dev, int handle, int64_t ns)
{
    struct glass_ctx *c = ctx_of(dev);
    int err;

    if (!sns_find(handle))
        return -EINVAL;
    pthread_mutex_lock(&c->lock);
    err = set_delay_locked(c, handle, ns);
    pthread_mutex_unlock(&c->lock);
    return err;
}

static int dev_batch(struct sensors_poll_device_1 *dev, int handle, int flags, int64_t period_ns,
                     int64_t timeout)
{
    if (!sns_find(handle))
        return -EINVAL;
    /* No hardware FIFO for batching: only timeout 0 (sensors.h batch(), spec §2). */
    if (timeout != 0)
        return -EINVAL;
    if (flags & SENSORS_BATCH_DRY_RUN)
        return 0;
    return dev_set_delay(&dev->v0, handle, period_ns);
}

static int dev_flush(struct sensors_poll_device_1 *dev, int handle)
{
    struct glass_ctx *c = ctx_of(dev);
    sensors_event_t ev;

    if (!sns_find(handle))
        return -EINVAL;
    pthread_mutex_lock(&c->lock);
    if (!c->enabled[handle]) {
        pthread_mutex_unlock(&c->lock);
        return -EINVAL;
    }
    memset(&ev, 0, sizeof(ev));
    ev.version = META_DATA_VERSION;
    ev.type = SENSOR_TYPE_META_DATA;
    ev.sensor = 0;
    ev.meta_data.what = META_DATA_FLUSH_COMPLETE;
    ev.meta_data.sensor = handle;
    push_event(c, &ev);
    pthread_cond_broadcast(&c->cond);
    pthread_mutex_unlock(&c->lock);
    return 0;
}

static int dev_poll(struct sensors_poll_device_t *dev, sensors_event_t *data, int count)
{
    struct glass_ctx *c = ctx_of(dev);
    int n = 0;

    if (count <= 0)
        return -EINVAL;
    pthread_mutex_lock(&c->lock);
    while (!c->q_count && !c->stop)
        pthread_cond_wait(&c->cond, &c->lock);
    while (n < count && c->q_count) {
        data[n++] = c->queue[c->q_head];
        c->q_head = (c->q_head + 1) % QUEUE_LEN;
        c->q_count--;
    }
    pthread_mutex_unlock(&c->lock);
    return n;
}

static int dev_close(struct hw_device_t *dev)
{
    struct glass_ctx *c = ctx_of(dev);
    int h;

    pthread_mutex_lock(&c->lock);
    c->stop = 1;
    pthread_cond_broadcast(&c->cond);
    pthread_mutex_unlock(&c->lock);
    if (c->wake[1] >= 0 && write(c->wake[1], "x", 1) < 0)
        ALOGW("cannot wake the reader: %s", strerror(errno));
    if (c->thread_started)
        pthread_join(c->thread, NULL);
    pthread_mutex_lock(&c->lock);
    for (h = 0; h <= SNS_HANDLE_MAX; h++)
        c->enabled[h] = 0;
    apply_config(c);
    if (c->als_fd >= 0)
        als_set(c, 0);
    if (c->magcal_dirty)
        save_magcal(c);
    pthread_mutex_unlock(&c->lock);
    if (c->iio_fd >= 0)
        close(c->iio_fd);
    if (c->als_fd >= 0)
        close(c->als_fd);
    if (c->wake[0] >= 0)
        close(c->wake[0]);
    if (c->wake[1] >= 0)
        close(c->wake[1]);
    pthread_cond_destroy(&c->cond);
    pthread_mutex_destroy(&c->lock);
    free(c);
    return 0;
}

static int open_sensors(const struct hw_module_t *module, const char *name,
                        struct hw_device_t **device)
{
    struct glass_ctx *c;
    char text[128];
    int h;

    if (!device)
        return -EINVAL;
    *device = NULL;
    if (!name || strcmp(name, SENSORS_HARDWARE_POLL) != 0)
        return -EINVAL;
    c = calloc(1, sizeof(*c));
    if (!c)
        return -ENOMEM;
    c->dev.common.tag = HARDWARE_DEVICE_TAG;
    c->dev.common.version = SENSORS_DEVICE_API_VERSION_1_1;
    c->dev.common.module = (struct hw_module_t *)module;
    c->dev.common.close = dev_close;
    c->dev.activate = dev_activate;
    c->dev.setDelay = dev_set_delay;
    c->dev.poll = dev_poll;
    c->dev.batch = dev_batch;
    c->dev.flush = dev_flush;
    pthread_mutex_init(&c->lock, NULL);
    pthread_cond_init(&c->cond, NULL);
    for (h = 0; h <= SNS_HANDLE_MAX; h++)
        c->delay[h] = DEFAULT_DELAY_NS;
    c->wake[0] = c->wake[1] = -1;

    sns_magcal_init(&c->magcal);
    sns_gyrobias_init(&c->gbias);
    if (sns_read_file(NULL, MAGCAL_FILE, text, sizeof(text)) > 0) {
        if (sns_magcal_parse(&c->magcal, text) == 0)
            ALOGI("loaded hard-iron bias %.1f %.1f %.1f uT", c->magcal.bias[0],
                  c->magcal.bias[1], c->magcal.bias[2]);
        else
            ALOGW("ignoring %s: not a valid calibration", MAGCAL_FILE);
    }
    iio_init(c);
    als_init(c);
    if (c->als_fd >= 0)
        als_set(c, 0);

    if (pipe(c->wake) == 0) {
        fcntl(c->wake[0], F_SETFL, O_NONBLOCK);
        fcntl(c->wake[1], F_SETFL, O_NONBLOCK);
        c->thread_started = pthread_create(&c->thread, NULL, reader, c) == 0;
    }
    if (!c->thread_started) {
        ALOGE("cannot start the reader thread");
        dev_close(&c->dev.common);
        return -ENOMEM;
    }
    *device = &c->dev.common;
    return 0;
}

/* ---- Module -------------------------------------------------------------------------- */

static struct sensor_t g_list[SNS_NUM_SENSORS];
static pthread_once_t g_list_once = PTHREAD_ONCE_INIT;

#ifdef SENSORS_DEVICE_API_VERSION_1_3
/* 6.0 headers: fields a 1.3+ framework reads (6.0 derives them from the type for 1.1). */
static const char *string_type(int type)
{
    switch (type) {
    case SENSOR_TYPE_ACCELEROMETER: return SENSOR_STRING_TYPE_ACCELEROMETER;
    case SENSOR_TYPE_MAGNETIC_FIELD: return SENSOR_STRING_TYPE_MAGNETIC_FIELD;
    case SENSOR_TYPE_GYROSCOPE: return SENSOR_STRING_TYPE_GYROSCOPE;
    case SENSOR_TYPE_LIGHT: return SENSOR_STRING_TYPE_LIGHT;
    case SENSOR_TYPE_MAGNETIC_FIELD_UNCALIBRATED: return SENSOR_STRING_TYPE_MAGNETIC_FIELD_UNCALIBRATED;
    case SENSOR_TYPE_GYROSCOPE_UNCALIBRATED: return SENSOR_STRING_TYPE_GYROSCOPE_UNCALIBRATED;
    }
    return NULL;
}
#endif

static void build_list(void)
{
    char why[128];
    int i;

    if (sns_list_check(sns_sensors, SNS_NUM_SENSORS, why, sizeof(why)))
        ALOGE("sensor table: %s", why);
    for (i = 0; i < SNS_NUM_SENSORS; i++) {
        const struct sns_desc *d = &sns_sensors[i];
        struct sensor_t *s = &g_list[i];

        s->name = d->name;
        s->vendor = d->vendor;
        s->version = 1;
        s->handle = d->handle;
        s->type = d->type;
        s->maxRange = d->max_range;
        s->resolution = d->resolution;
        s->power = d->power_ma;
        s->minDelay = d->min_delay_us;
        s->fifoReservedEventCount = 0;
        s->fifoMaxEventCount = 0;
#ifdef SENSORS_DEVICE_API_VERSION_1_3
        s->stringType = string_type(d->type);
        s->requiredPermission = NULL;
        s->maxDelay = d->max_delay_us;
        s->flags = d->min_delay_us == 0 ? SENSOR_FLAG_ON_CHANGE_MODE : SENSOR_FLAG_CONTINUOUS_MODE;
#endif
    }
}

static int get_sensors_list(struct sensors_module_t *module, struct sensor_t const **list)
{
    (void)module;
    pthread_once(&g_list_once, build_list);
    *list = g_list;
    return SNS_NUM_SENSORS;
}

static struct hw_module_methods_t sensors_module_methods = {
    .open = open_sensors,
};

struct sensors_module_t HAL_MODULE_INFO_SYM = {
    .common = {
        .tag = HARDWARE_MODULE_TAG,
        .module_api_version = SENSORS_MODULE_API_VERSION_0_1,
        .hal_api_version = HARDWARE_HAL_API_VERSION,
        .id = SENSORS_HARDWARE_MODULE_ID,
        .name = "Glass sensors HAL",
        .author = "OpenMyGlass",
        .methods = &sensors_module_methods,
    },
    .get_sensors_list = get_sensors_list,
};
