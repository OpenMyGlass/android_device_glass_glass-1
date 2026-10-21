/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Primary audio HAL for Glass Explorer Edition (audio.primary.glass_1).
 *
 * Legacy audio_hw_device, device API 2.0, so the same source works with the legacy audio
 * policy of 4.4.4 and with 6.0.1 (which still accepts 2.0). The only 6.0 difference used
 * here is the longer open_output_stream/open_input_stream signature, selected on
 * AUDIO_DEVICE_API_VERSION_3_0, which 6.0's audio.h defines and 4.4's does not.
 *
 * Plain AOSP tinyalsa on card 0: playback on device 0 (ABE Multimedia), capture on device 2
 * (ABE Voice uplink from DMIC0 L/R), resampled with audio_utils' resampler. No voice call,
 * no Bluetooth SCO. Design and sources: docs/hals.md and docs/specs/audio.md. Mixer tables,
 * path choice and the other pure logic are in audio_logic.c (host tested).
 *
 * Written for OpenMyGlass. Its structure follows the usual AOSP tinyalsa HAL layout (as in
 * device/samsung/tuna/audio), but no code is copied from one.
 */
#define LOG_TAG "audio.primary.glass_1"

#include <errno.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include <cutils/log.h>
#include <cutils/properties.h>
#include <hardware/audio.h>
#include <hardware/hardware.h>
#include <system/audio.h>
#include <tinyalsa/asoundlib.h>
#include <audio_utils/resampler.h>

#include "audio_logic.h"

/* audio_logic.h repeats a few system/audio.h values so that it needs no Android header. */
#define GLASS_SAME(a, b) typedef char glass_same_##a[((a) == (b)) ? 1 : -1]
GLASS_SAME(GLASS_DEV_OUT_EARPIECE, AUDIO_DEVICE_OUT_EARPIECE);
GLASS_SAME(GLASS_DEV_OUT_SPEAKER, AUDIO_DEVICE_OUT_SPEAKER);
GLASS_SAME(GLASS_DEV_OUT_WIRED_HEADSET, AUDIO_DEVICE_OUT_WIRED_HEADSET);
GLASS_SAME(GLASS_DEV_OUT_WIRED_HEADPHONE, AUDIO_DEVICE_OUT_WIRED_HEADPHONE);
GLASS_SAME(GLASS_SOURCE_VOICE_RECOGNITION, AUDIO_SOURCE_VOICE_RECOGNITION);

#define OUT_FRAME_SIZE (2 * sizeof(int16_t))
#define RESAMPLER_QUALITY RESAMPLER_QUALITY_DEFAULT /* 4, the stock choice (spec 5.2) */

/* Root of every sysfs path: empty on the device. */
static const char sysfs_root[] = "";

struct glass_out;
struct glass_in;

struct glass_dev {
    struct audio_hw_device hw;
    pthread_mutex_t lock;           /* the mixer, out and in pointers, gains */
    struct mixer *mixer;
    int mic_mute;
    int voice_gain;
    struct glass_out *out;
    struct glass_in *in;
};

struct glass_out {
    struct audio_stream_out stream;
    pthread_mutex_t lock;           /* taken after glass_dev.lock when both are held */
    struct glass_dev *dev;
    struct pcm_config config;
    struct pcm *pcm;
    int standby;
    audio_devices_t devices;
    enum glass_path path;
    int downmix;
    int id_monitor;                 /* this stream wrote "1" to id_monitor */
    uint64_t written;               /* frames since open, for get_presentation_position */
    int16_t *scratch;               /* down-mix copy of the caller's buffer */
    size_t scratch_frames;
};

struct glass_in {
    struct audio_stream_in stream;
    pthread_mutex_t lock;           /* taken after glass_dev.lock when both are held */
    struct glass_dev *dev;
    struct pcm_config config;
    struct pcm *pcm;
    int standby;
    uint32_t rate;                  /* requested */
    unsigned int channels;          /* requested: 1 or 2; the pcm is always 2 */
    audio_channel_mask_t channel_mask;
    audio_devices_t devices;
    int source;
    int paused_glasshub;            /* this stream wrote "1" to pause_for_audio */
    size_t mute_left;               /* bytes still to zero after the start */
    struct resampler_itfe *resampler;
    struct resampler_buffer_provider provider;
    int16_t *period;                /* one stereo period from the pcm, for the resampler */
    size_t period_left;             /* frames of period not yet consumed */
    int read_status;
    int16_t *stereo;                /* stereo frames before a mono down-mix */
    size_t stereo_frames;
};

static void pcm_config_from(struct pcm_config *c, const struct glass_pcm_params *p)
{
    memset(c, 0, sizeof(*c));
    c->channels = p->channels;
    c->rate = p->rate;
    c->period_size = p->period_size;
    c->period_count = p->period_count;
    c->format = PCM_FORMAT_S16_LE;
    /* start, stop and silence thresholds 0: tinyalsa's defaults (start when full). */
}

static int grow(int16_t **buf, size_t *frames, size_t want, size_t channels)
{
    int16_t *p;

    if (*frames >= want)
        return 0;
    p = realloc(*buf, want * channels * sizeof(int16_t));
    if (!p)
        return -ENOMEM;
    *buf = p;
    *frames = want;
    return 0;
}

static enum glass_headset read_headset(void)
{
    char text[32];
    int n = glass_audio_read(sysfs_root, GLASS_HEADSET_PATH, text, sizeof(text));

    if (n < 0) {
        ALOGW("cannot read %s: %d", GLASS_HEADSET_PATH, n);
        return GLASS_HEADSET_UNREAD;
    }
    return glass_parse_headset(text);
}

static void write_sysfs(const char *path, const char *value)
{
    int r = glass_audio_write(sysfs_root, path, value);
    if (r)
        ALOGW("cannot write '%s' to %s: %d", value, path, r);
}

/* ---- Output ---- */

/* Chooses and applies the output path. Called with dev->lock and out->lock held. */
static void select_output_l(struct glass_out *out)
{
    struct glass_dev *dev = out->dev;
    enum glass_headset headset = read_headset();
    enum glass_path path = glass_select_path(headset, out->devices);

    if (path == GLASS_PATH_HEADSET)
        glass_mixer_apply(dev->mixer, glass_headset_ctls, glass_headset_ctls_count);
    else
        glass_mixer_apply(dev->mixer, glass_earpiece_ctls, glass_earpiece_ctls_count);
    out->path = path;
    out->downmix = glass_downmix_needed(path, headset);

    /*
     * The mux carries the earbud only in audio mode. Stock writes this on every start; here
     * only when an earbud is detected (spec 7). The kernel keeps USB mode while USB is online.
     */
    if (headset == GLASS_HEADSET_MONO || headset == GLASS_HEADSET_STEREO)
        write_sysfs(GLASS_MUX_MODE_PATH, "audio");
    /*
     * While playing, let the kernel poll the ID pin (once a second) and report plugging to
     * the h2w switch, so AudioPolicy sends a routing change and the path follows. As stock,
     * on every start; cleared at standby.
     */
    if (!out->id_monitor) {
        write_sysfs(GLASS_ID_MONITOR_PATH, "1");
        out->id_monitor = 1;
    }
    ALOGV("output path %s, headset %d, routing 0x%x, downmix %d",
          path == GLASS_PATH_HEADSET ? "headset" : "earpiece", headset, out->devices,
          out->downmix);
}

static int start_output_l(struct glass_out *out)
{
    select_output_l(out);
    out->pcm = pcm_open(GLASS_CARD, GLASS_OUT_DEVICE, PCM_OUT | PCM_MONOTONIC, &out->config);
    if (!pcm_is_ready(out->pcm)) {
        ALOGE("cannot open pcm %d,%d for playback: %s", GLASS_CARD, GLASS_OUT_DEVICE,
              pcm_get_error(out->pcm));
        pcm_close(out->pcm);
        out->pcm = NULL;
        return -ENODEV;
    }
    out->standby = 0;
    return 0;
}

static void standby_output_l(struct glass_out *out)
{
    if (!out->standby) {
        pcm_close(out->pcm);
        out->pcm = NULL;
        out->standby = 1;
    }
    /* Also after a failed start, which may have set it already. */
    if (out->id_monitor) {
        write_sysfs(GLASS_ID_MONITOR_PATH, "0");
        out->id_monitor = 0;
    }
}

static uint32_t out_get_sample_rate(const struct audio_stream *stream)
{
    return ((const struct glass_out *)stream)->config.rate;
}

static int out_set_sample_rate(struct audio_stream *stream, uint32_t rate)
{
    (void)stream;
    (void)rate;
    return -ENOSYS;
}

static size_t out_get_buffer_size(const struct audio_stream *stream)
{
    /* One period; 1056 frames is a multiple of 16 as AudioFlinger wants. */
    return ((const struct glass_out *)stream)->config.period_size * OUT_FRAME_SIZE;
}

static audio_channel_mask_t out_get_channels(const struct audio_stream *stream)
{
    (void)stream;
    return AUDIO_CHANNEL_OUT_STEREO;
}

static audio_format_t out_get_format(const struct audio_stream *stream)
{
    (void)stream;
    return AUDIO_FORMAT_PCM_16_BIT;
}

static int out_set_format(struct audio_stream *stream, audio_format_t format)
{
    (void)stream;
    (void)format;
    return -ENOSYS;
}

static int out_standby(struct audio_stream *stream)
{
    struct glass_out *out = (struct glass_out *)stream;

    pthread_mutex_lock(&out->dev->lock);
    pthread_mutex_lock(&out->lock);
    standby_output_l(out);
    pthread_mutex_unlock(&out->lock);
    pthread_mutex_unlock(&out->dev->lock);
    return 0;
}

static int stream_dump(const struct audio_stream *stream, int fd)
{
    (void)stream;
    (void)fd;
    return 0;
}

static int out_set_parameters(struct audio_stream *stream, const char *kvpairs)
{
    struct glass_out *out = (struct glass_out *)stream;
    int routing;

    if (glass_param_int(kvpairs, AUDIO_PARAMETER_STREAM_ROUTING, &routing) != 1)
        return 0;
    pthread_mutex_lock(&out->dev->lock);
    pthread_mutex_lock(&out->lock);
    if (routing != 0 && (audio_devices_t)routing != out->devices) {
        out->devices = (audio_devices_t)routing;
        if (!out->standby)
            select_output_l(out);
    }
    pthread_mutex_unlock(&out->lock);
    pthread_mutex_unlock(&out->dev->lock);
    return 0;
}

static char *stream_get_parameters(const struct audio_stream *stream, const char *keys)
{
    (void)stream;
    (void)keys;
    return strdup("");
}

static int stream_add_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    (void)stream;
    (void)effect;
    return 0;
}

static uint32_t out_get_latency(const struct audio_stream_out *stream)
{
    const struct glass_out *out = (const struct glass_out *)stream;
    return out->config.period_size * out->config.period_count * 1000 / out->config.rate;
}

static int out_set_volume(struct audio_stream_out *stream, float left, float right)
{
    /* AudioFlinger applies stream volume in software. */
    (void)stream;
    (void)left;
    (void)right;
    return -ENOSYS;
}

static ssize_t out_write(struct audio_stream_out *stream, const void *buffer, size_t bytes)
{
    struct glass_out *out = (struct glass_out *)stream;
    struct glass_dev *dev = out->dev;
    size_t frames = bytes / OUT_FRAME_SIZE;
    const void *data = buffer;
    int ret;

    pthread_mutex_lock(&dev->lock);
    pthread_mutex_lock(&out->lock);
    ret = out->standby ? start_output_l(out) : 0;
    pthread_mutex_unlock(&dev->lock);
    if (ret)
        goto error;

    if (out->downmix) {
        ret = grow(&out->scratch, &out->scratch_frames, frames, 2);
        if (ret)
            goto error;
        memcpy(out->scratch, buffer, frames * OUT_FRAME_SIZE);
        glass_downmix_stereo(out->scratch, frames);
        data = out->scratch;
    }
    ret = pcm_write(out->pcm, data, frames * OUT_FRAME_SIZE);
    if (ret) {
        ALOGE("pcm_write: %s", pcm_get_error(out->pcm));
        goto error;
    }
    out->written += frames;
    pthread_mutex_unlock(&out->lock);
    return bytes;

error:
    pthread_mutex_unlock(&out->lock);
    out_standby(&stream->common);
    /* Keep AudioFlinger's timing: pretend the buffer played. */
    usleep((useconds_t)((uint64_t)frames * 1000000 / out->config.rate));
    return bytes;
}

static int out_get_render_position(const struct audio_stream_out *stream, uint32_t *dsp_frames)
{
    (void)stream;
    (void)dsp_frames;
    return -EINVAL;
}

static int out_get_next_write_timestamp(const struct audio_stream_out *stream,
                                        int64_t *timestamp)
{
    (void)stream;
    (void)timestamp;
    return -EINVAL;
}

static int out_get_presentation_position(const struct audio_stream_out *stream,
                                         uint64_t *frames, struct timespec *timestamp)
{
    struct glass_out *out = (struct glass_out *)stream;
    unsigned int avail;
    int ret = -ENODATA;

    pthread_mutex_lock(&out->lock);
    if (out->pcm && pcm_get_htimestamp(out->pcm, &avail, timestamp) == 0 &&
        glass_presented_frames(out->written, pcm_get_buffer_size(out->pcm), avail, frames) == 0)
        ret = 0;
    pthread_mutex_unlock(&out->lock);
    return ret;
}

/* ---- Input ---- */

static int get_next_buffer(struct resampler_buffer_provider *provider,
                           struct resampler_buffer *buffer)
{
    struct glass_in *in = (struct glass_in *)((char *)provider -
                                              offsetof(struct glass_in, provider));
    size_t period = in->config.period_size;

    if (!in->pcm) {
        buffer->raw = NULL;
        buffer->frame_count = 0;
        in->read_status = -ENODEV;
        return -ENODEV;
    }
    if (in->period_left == 0) {
        in->read_status = pcm_read(in->pcm, in->period, period * OUT_FRAME_SIZE);
        if (in->read_status) {
            ALOGE("pcm_read: %s", pcm_get_error(in->pcm));
            buffer->raw = NULL;
            buffer->frame_count = 0;
            return in->read_status;
        }
        in->period_left = period;
    }
    if (buffer->frame_count > in->period_left)
        buffer->frame_count = in->period_left;
    buffer->i16 = in->period + (period - in->period_left) * 2;
    return 0;
}

static void release_buffer(struct resampler_buffer_provider *provider,
                           struct resampler_buffer *buffer)
{
    struct glass_in *in = (struct glass_in *)((char *)provider -
                                              offsetof(struct glass_in, provider));
    in->period_left -= buffer->frame_count;
}

/* Reads frames stereo frames at the requested rate into dst. 0 or a negative errno. */
static int read_frames(struct glass_in *in, int16_t *dst, size_t frames)
{
    size_t done = 0;

    if (!in->resampler) {
        int r = pcm_read(in->pcm, dst, frames * OUT_FRAME_SIZE);
        if (r)
            ALOGE("pcm_read: %s", pcm_get_error(in->pcm));
        return r ? -EIO : 0;
    }
    while (done < frames) {
        size_t n = frames - done;
        in->resampler->resample_from_provider(in->resampler, dst + done * 2, &n);
        if (in->read_status)
            return -EIO;
        if (n == 0)
            return -EIO;
        done += n;
    }
    return 0;
}

static int start_input_l(struct glass_in *in)
{
    struct glass_dev *dev = in->dev;
    char value[PROPERTY_VALUE_MAX];
    int dmic_gain;

    in->pcm = pcm_open(GLASS_CARD, GLASS_IN_DEVICE, PCM_IN, &in->config);
    if (!pcm_is_ready(in->pcm)) {
        ALOGE("cannot open pcm %d,%d for capture: %s", GLASS_CARD, GLASS_IN_DEVICE,
              pcm_get_error(in->pcm));
        pcm_close(in->pcm);
        in->pcm = NULL;
        return -ENODEV;
    }
    /* Capture gains on every start (spec 4.4); the properties override the defaults. */
    property_get(GLASS_PROP_DMIC_GAIN, value, "");
    dmic_gain = glass_prop_int(value, GLASS_DMIC_GAIN_DEFAULT);
    glass_mixer_set_int(dev->mixer, GLASS_CTL_DMIC_GAIN, dmic_gain);
    glass_mixer_set_int(dev->mixer, GLASS_CTL_VOICE_GAIN, dev->voice_gain);

    if (glass_pause_for_source(in->source)) {
        write_sysfs(GLASS_PAUSE_FOR_AUDIO_PATH, "1");
        in->paused_glasshub = 1;
    }
    in->mute_left = glass_startup_mute_bytes(in->rate, in->channels * sizeof(int16_t));
    in->period_left = 0;
    in->read_status = 0;
    if (in->resampler)
        in->resampler->reset(in->resampler);
    in->standby = 0;
    return 0;
}

static void standby_input_l(struct glass_in *in)
{
    if (in->standby)
        return;
    pcm_close(in->pcm);
    in->pcm = NULL;
    if (in->paused_glasshub) {
        write_sysfs(GLASS_PAUSE_FOR_AUDIO_PATH, "0");
        in->paused_glasshub = 0;
    }
    in->standby = 1;
}

static uint32_t in_get_sample_rate(const struct audio_stream *stream)
{
    return ((const struct glass_in *)stream)->rate;
}

static int in_set_sample_rate(struct audio_stream *stream, uint32_t rate)
{
    (void)stream;
    (void)rate;
    return -ENOSYS;
}

static size_t in_get_buffer_size(const struct audio_stream *stream)
{
    const struct glass_in *in = (const struct glass_in *)stream;
    return glass_input_buffer_size(in->rate, in->channels);
}

static audio_channel_mask_t in_get_channels(const struct audio_stream *stream)
{
    return ((const struct glass_in *)stream)->channel_mask;
}

static int in_standby(struct audio_stream *stream)
{
    struct glass_in *in = (struct glass_in *)stream;

    pthread_mutex_lock(&in->dev->lock);
    pthread_mutex_lock(&in->lock);
    standby_input_l(in);
    pthread_mutex_unlock(&in->lock);
    pthread_mutex_unlock(&in->dev->lock);
    return 0;
}

static int in_set_parameters(struct audio_stream *stream, const char *kvpairs)
{
    struct glass_in *in = (struct glass_in *)stream;
    int source, routing;
    int has_source = glass_param_int(kvpairs, AUDIO_PARAMETER_STREAM_INPUT_SOURCE, &source);
    int has_routing = glass_param_int(kvpairs, AUDIO_PARAMETER_STREAM_ROUTING, &routing);

    pthread_mutex_lock(&in->dev->lock);
    pthread_mutex_lock(&in->lock);
    /* Either change restarts the capture, so pause_for_audio follows the new source. */
    if (has_source == 1 && source != 0 && source != in->source) {
        in->source = source;
        standby_input_l(in);
    }
    if (has_routing == 1 && routing != 0 && (audio_devices_t)routing != in->devices) {
        in->devices = (audio_devices_t)routing;
        standby_input_l(in);
    }
    pthread_mutex_unlock(&in->lock);
    pthread_mutex_unlock(&in->dev->lock);
    return 0;
}

static int in_set_gain(struct audio_stream_in *stream, float gain)
{
    (void)stream;
    (void)gain;
    return 0;
}

static ssize_t in_read(struct audio_stream_in *stream, void *buffer, size_t bytes)
{
    struct glass_in *in = (struct glass_in *)stream;
    struct glass_dev *dev = in->dev;
    size_t frames = bytes / (in->channels * sizeof(int16_t));
    int16_t *dst = buffer;
    int ret;

    pthread_mutex_lock(&dev->lock);
    pthread_mutex_lock(&in->lock);
    ret = in->standby ? start_input_l(in) : 0;
    pthread_mutex_unlock(&dev->lock);
    if (ret)
        goto error;

    if (in->channels == 1) {
        ret = grow(&in->stereo, &in->stereo_frames, frames, 2);
        if (ret)
            goto error;
        dst = in->stereo;
    }
    ret = read_frames(in, dst, frames);
    if (ret)
        goto error;
    if (in->channels == 1)
        glass_stereo_to_mono(in->stereo, buffer, frames);

    if (dev->mic_mute) {
        memset(buffer, 0, bytes);
    } else if (in->mute_left) {
        size_t n = in->mute_left < bytes ? in->mute_left : bytes;
        memset(buffer, 0, n);
        in->mute_left -= n;
    }
    pthread_mutex_unlock(&in->lock);
    return bytes;

error:
    pthread_mutex_unlock(&in->lock);
    in_standby(&stream->common);
    memset(buffer, 0, bytes);
    usleep((useconds_t)((uint64_t)frames * 1000000 / in->rate));
    return bytes;
}

static uint32_t in_get_input_frames_lost(struct audio_stream_in *stream)
{
    (void)stream;
    return 0;
}

/* ---- Device ---- */

static int adev_init_check(const struct audio_hw_device *hw)
{
    (void)hw;
    return 0;
}

static int adev_set_voice_volume(struct audio_hw_device *hw, float volume)
{
    /* No in-call audio. */
    (void)hw;
    (void)volume;
    return 0;
}

static int adev_set_master_volume(struct audio_hw_device *hw, float volume)
{
    /* -ENOSYS makes AudioFlinger apply master volume in software. */
    (void)hw;
    (void)volume;
    return -ENOSYS;
}

static int adev_get_master_volume(struct audio_hw_device *hw, float *volume)
{
    (void)hw;
    (void)volume;
    return -ENOSYS;
}

static int adev_set_mode(struct audio_hw_device *hw, audio_mode_t mode)
{
    (void)hw;
    (void)mode;
    return 0;
}

static int adev_set_mic_mute(struct audio_hw_device *hw, bool state)
{
    ((struct glass_dev *)hw)->mic_mute = state;
    return 0;
}

static int adev_get_mic_mute(const struct audio_hw_device *hw, bool *state)
{
    *state = ((const struct glass_dev *)hw)->mic_mute;
    return 0;
}

static uint32_t adev_get_supported_devices(const struct audio_hw_device *hw)
{
    (void)hw;
    return AUDIO_DEVICE_OUT_EARPIECE | AUDIO_DEVICE_OUT_SPEAKER |
           AUDIO_DEVICE_OUT_WIRED_HEADSET | AUDIO_DEVICE_OUT_WIRED_HEADPHONE |
           AUDIO_DEVICE_OUT_DEFAULT | AUDIO_DEVICE_IN_BUILTIN_MIC | AUDIO_DEVICE_IN_BACK_MIC |
           AUDIO_DEVICE_IN_DEFAULT;
}

static int adev_set_parameters(struct audio_hw_device *hw, const char *kvpairs)
{
    struct glass_dev *dev = (struct glass_dev *)hw;
    int gain;

    /* Glass key (spec 5.3): the AUDUL Voice UL gain of the capture path. */
    if (glass_param_int(kvpairs, "voice_volume", &gain) == 1) {
        pthread_mutex_lock(&dev->lock);
        dev->voice_gain = gain;
        glass_mixer_set_int(dev->mixer, GLASS_CTL_VOICE_GAIN, gain);
        pthread_mutex_unlock(&dev->lock);
    }
    return 0;
}

static char *adev_get_parameters(const struct audio_hw_device *hw, const char *keys)
{
    const struct glass_dev *dev = (const struct glass_dev *)hw;
    char reply[64] = "";
    size_t n = 0;

    if (glass_param_has(keys, "voice_volume"))
        n += snprintf(reply + n, sizeof(reply) - n, "voice_volume=%d", dev->voice_gain);
    if (glass_param_has(keys, "output_type")) {
        enum glass_headset h = read_headset();
        if (h == GLASS_HEADSET_UNREAD)
            h = GLASS_HEADSET_NONE;
        snprintf(reply + n, sizeof(reply) - n, "%soutput_type=%d", n ? ";" : "", (int)h);
    }
    return strdup(reply);
}

static size_t adev_get_input_buffer_size(const struct audio_hw_device *hw,
                                         const struct audio_config *config)
{
    unsigned int channels = __builtin_popcount(config->channel_mask);

    (void)hw;
    if (!glass_input_supported(config->sample_rate,
                               config->format == AUDIO_FORMAT_PCM_16_BIT, channels))
        return 0;
    return glass_input_buffer_size(config->sample_rate, channels);
}

#ifdef AUDIO_DEVICE_API_VERSION_3_0
static int adev_open_output_stream(struct audio_hw_device *hw, audio_io_handle_t handle,
                                   audio_devices_t devices, audio_output_flags_t flags,
                                   struct audio_config *config,
                                   struct audio_stream_out **stream_out, const char *address)
#else
static int adev_open_output_stream(struct audio_hw_device *hw, audio_io_handle_t handle,
                                   audio_devices_t devices, audio_output_flags_t flags,
                                   struct audio_config *config,
                                   struct audio_stream_out **stream_out)
#endif
{
    struct glass_dev *dev = (struct glass_dev *)hw;
    struct glass_out *out;

    (void)handle;
    (void)flags;
#ifdef AUDIO_DEVICE_API_VERSION_3_0
    (void)address;
#endif
    *stream_out = NULL;
    pthread_mutex_lock(&dev->lock);
    if (dev->out) {
        /* One output (the policy declares only the primary one). */
        pthread_mutex_unlock(&dev->lock);
        return -ENOSYS;
    }
    out = calloc(1, sizeof(*out));
    if (!out) {
        pthread_mutex_unlock(&dev->lock);
        return -ENOMEM;
    }
    pthread_mutex_init(&out->lock, NULL);
    out->dev = dev;
    pcm_config_from(&out->config, &glass_out_params);
    out->standby = 1;
    out->devices = devices;

    out->stream.common.get_sample_rate = out_get_sample_rate;
    out->stream.common.set_sample_rate = out_set_sample_rate;
    out->stream.common.get_buffer_size = out_get_buffer_size;
    out->stream.common.get_channels = out_get_channels;
    out->stream.common.get_format = out_get_format;
    out->stream.common.set_format = out_set_format;
    out->stream.common.standby = out_standby;
    out->stream.common.dump = stream_dump;
    out->stream.common.set_parameters = out_set_parameters;
    out->stream.common.get_parameters = stream_get_parameters;
    out->stream.common.add_audio_effect = stream_add_effect;
    out->stream.common.remove_audio_effect = stream_add_effect;
    out->stream.get_latency = out_get_latency;
    out->stream.set_volume = out_set_volume;
    out->stream.write = out_write;
    out->stream.get_render_position = out_get_render_position;
    out->stream.get_next_write_timestamp = out_get_next_write_timestamp;
    out->stream.get_presentation_position = out_get_presentation_position;

    /* The hardware runs at 48 kHz stereo 16-bit; report that, whatever was asked. */
    config->sample_rate = out->config.rate;
    config->channel_mask = AUDIO_CHANNEL_OUT_STEREO;
    config->format = AUDIO_FORMAT_PCM_16_BIT;

    dev->out = out;
    pthread_mutex_unlock(&dev->lock);
    *stream_out = &out->stream;
    return 0;
}

static void adev_close_output_stream(struct audio_hw_device *hw, struct audio_stream_out *stream)
{
    struct glass_dev *dev = (struct glass_dev *)hw;
    struct glass_out *out = (struct glass_out *)stream;

    out_standby(&stream->common);
    pthread_mutex_lock(&dev->lock);
    if (dev->out == out)
        dev->out = NULL;
    pthread_mutex_unlock(&dev->lock);
    pthread_mutex_destroy(&out->lock);
    free(out->scratch);
    free(out);
}

#ifdef AUDIO_DEVICE_API_VERSION_3_0
static int adev_open_input_stream(struct audio_hw_device *hw, audio_io_handle_t handle,
                                  audio_devices_t devices, struct audio_config *config,
                                  struct audio_stream_in **stream_in, audio_input_flags_t flags,
                                  const char *address, audio_source_t source)
#else
static int adev_open_input_stream(struct audio_hw_device *hw, audio_io_handle_t handle,
                                  audio_devices_t devices, struct audio_config *config,
                                  struct audio_stream_in **stream_in)
#endif
{
    struct glass_dev *dev = (struct glass_dev *)hw;
    struct glass_in *in;
    struct glass_pcm_params params;
    char value[PROPERTY_VALUE_MAX];
    unsigned int channels = __builtin_popcount(config->channel_mask);
    int rate_prop;

    (void)handle;
    *stream_in = NULL;
    if (!glass_input_supported(config->sample_rate,
                               config->format == AUDIO_FORMAT_PCM_16_BIT, channels)) {
        /* Suggest a config the HAL takes; 5.0+ AudioFlinger retries with it. */
        config->sample_rate = 48000;
        config->format = AUDIO_FORMAT_PCM_16_BIT;
        if (channels < 1 || channels > 2)
            config->channel_mask = AUDIO_CHANNEL_IN_STEREO;
        return -EINVAL;
    }
    pthread_mutex_lock(&dev->lock);
    if (dev->in) {
        pthread_mutex_unlock(&dev->lock);
        return -ENOSYS;
    }
    in = calloc(1, sizeof(*in));
    if (!in) {
        pthread_mutex_unlock(&dev->lock);
        return -ENOMEM;
    }
    pthread_mutex_init(&in->lock, NULL);
    in->dev = dev;
    in->standby = 1;
    in->rate = config->sample_rate;
    in->channels = channels;
    in->channel_mask = config->channel_mask;
    in->devices = devices;
#ifdef AUDIO_DEVICE_API_VERSION_3_0
    (void)flags;
    (void)address;
    in->source = source;
#else
    in->source = AUDIO_SOURCE_DEFAULT;
#endif

    /* audio.in.rate, if set, chooses the capture config instead of the requested rate. */
    property_get(GLASS_PROP_IN_RATE, value, "");
    rate_prop = glass_prop_int(value, 0);
    glass_input_params(rate_prop > 0 ? (uint32_t)rate_prop : in->rate, &params);
    pcm_config_from(&in->config, &params);

    if (in->config.rate != in->rate) {
        int r;
        in->period = malloc(in->config.period_size * OUT_FRAME_SIZE);
        in->provider.get_next_buffer = get_next_buffer;
        in->provider.release_buffer = release_buffer;
        r = in->period ? create_resampler(in->config.rate, in->rate, 2, RESAMPLER_QUALITY,
                                          &in->provider, &in->resampler) : -ENOMEM;
        if (r) {
            ALOGE("cannot resample %u to %u Hz: %d", in->config.rate, in->rate, r);
            free(in->period);
            pthread_mutex_destroy(&in->lock);
            free(in);
            pthread_mutex_unlock(&dev->lock);
            return -EINVAL;
        }
    }

    in->stream.common.get_sample_rate = in_get_sample_rate;
    in->stream.common.set_sample_rate = in_set_sample_rate;
    in->stream.common.get_buffer_size = in_get_buffer_size;
    in->stream.common.get_channels = in_get_channels;
    in->stream.common.get_format = out_get_format;
    in->stream.common.set_format = out_set_format;
    in->stream.common.standby = in_standby;
    in->stream.common.dump = stream_dump;
    in->stream.common.set_parameters = in_set_parameters;
    in->stream.common.get_parameters = stream_get_parameters;
    in->stream.common.add_audio_effect = stream_add_effect;
    in->stream.common.remove_audio_effect = stream_add_effect;
    in->stream.set_gain = in_set_gain;
    in->stream.read = in_read;
    in->stream.get_input_frames_lost = in_get_input_frames_lost;

    dev->in = in;
    pthread_mutex_unlock(&dev->lock);
    *stream_in = &in->stream;
    return 0;
}

static void adev_close_input_stream(struct audio_hw_device *hw, struct audio_stream_in *stream)
{
    struct glass_dev *dev = (struct glass_dev *)hw;
    struct glass_in *in = (struct glass_in *)stream;

    in_standby(&stream->common);
    pthread_mutex_lock(&dev->lock);
    if (dev->in == in)
        dev->in = NULL;
    pthread_mutex_unlock(&dev->lock);
    if (in->resampler)
        release_resampler(in->resampler);
    pthread_mutex_destroy(&in->lock);
    free(in->period);
    free(in->stereo);
    free(in);
}

static int adev_dump(const audio_hw_device_t *hw, int fd)
{
    (void)hw;
    (void)fd;
    return 0;
}

static int adev_close(hw_device_t *device)
{
    struct glass_dev *dev = (struct glass_dev *)device;

    mixer_close(dev->mixer);
    pthread_mutex_destroy(&dev->lock);
    free(dev);
    return 0;
}

static int adev_open(const hw_module_t *module, const char *name, hw_device_t **device)
{
    struct glass_dev *dev;
    char value[PROPERTY_VALUE_MAX];

    if (strcmp(name, AUDIO_HARDWARE_INTERFACE) != 0)
        return -EINVAL;
    dev = calloc(1, sizeof(*dev));
    if (!dev)
        return -ENOMEM;
    dev->mixer = mixer_open(GLASS_CARD);
    if (!dev->mixer) {
        ALOGE("cannot open mixer of card %d", GLASS_CARD);
        free(dev);
        return -EINVAL;
    }
    if (glass_mixer_missing(dev->mixer)) {
        ALOGE("card %d is not the Glass card: mixer controls missing", GLASS_CARD);
        mixer_close(dev->mixer);
        free(dev);
        return -EINVAL;
    }
    pthread_mutex_init(&dev->lock, NULL);
    property_get(GLASS_PROP_VOICE_GAIN, value, "");
    dev->voice_gain = glass_prop_int(value, GLASS_VOICE_GAIN_DEFAULT);
    glass_mixer_apply(dev->mixer, glass_init_ctls, glass_init_ctls_count);
    glass_mixer_apply(dev->mixer, glass_earpiece_ctls, glass_earpiece_ctls_count);

    dev->hw.common.tag = HARDWARE_DEVICE_TAG;
    dev->hw.common.version = AUDIO_DEVICE_API_VERSION_2_0;
    dev->hw.common.module = (struct hw_module_t *)module;
    dev->hw.common.close = adev_close;
    dev->hw.get_supported_devices = adev_get_supported_devices;
    dev->hw.init_check = adev_init_check;
    dev->hw.set_voice_volume = adev_set_voice_volume;
    dev->hw.set_master_volume = adev_set_master_volume;
    dev->hw.get_master_volume = adev_get_master_volume;
    dev->hw.set_mode = adev_set_mode;
    dev->hw.set_mic_mute = adev_set_mic_mute;
    dev->hw.get_mic_mute = adev_get_mic_mute;
    dev->hw.set_parameters = adev_set_parameters;
    dev->hw.get_parameters = adev_get_parameters;
    dev->hw.get_input_buffer_size = adev_get_input_buffer_size;
    dev->hw.open_output_stream = adev_open_output_stream;
    dev->hw.close_output_stream = adev_close_output_stream;
    dev->hw.open_input_stream = adev_open_input_stream;
    dev->hw.close_input_stream = adev_close_input_stream;
    dev->hw.dump = adev_dump;

    *device = &dev->hw.common;
    ALOGI("opened: card %d, playback device %d, capture device %d", GLASS_CARD,
          GLASS_OUT_DEVICE, GLASS_IN_DEVICE);
    return 0;
}

static struct hw_module_methods_t hal_module_methods = {
    .open = adev_open,
};

struct audio_module HAL_MODULE_INFO_SYM = {
    .common = {
        .tag = HARDWARE_MODULE_TAG,
        .module_api_version = AUDIO_MODULE_API_VERSION_0_1,
        .hal_api_version = HARDWARE_HAL_API_VERSION,
        .id = AUDIO_HARDWARE_MODULE_ID,
        .name = "Glass audio HAL",
        .author = "OpenMyGlass",
        .methods = &hal_module_methods,
    },
};
