/* SPDX-License-Identifier: Apache-2.0 */
#define LOG_TAG "audio.primary.glass_1"

#include "audio_logic.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef GLASS_HOST_TEST
/* Test files are regular files, which unlike sysfs attributes keep old bytes past a write. */
#define GLASS_WRITE_FLAGS (O_WRONLY | O_TRUNC)
#define GLASS_LOGE(...) (fprintf(stderr, LOG_TAG ": " __VA_ARGS__), fputc('\n', stderr))
#else
#define GLASS_WRITE_FLAGS O_WRONLY
#include <cutils/log.h>
#define GLASS_LOGE(...) ALOGE(__VA_ARGS__)
#endif

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

/* Spec 4.1: the stock HAL refuses to open unless all of these exist. */
const char *const glass_required_ctls[] = {
    "DL1 Mixer Multimedia",
    "DL1 Mixer Voice",
    "DL1 PDM Switch",
    "Headset Left Playback",
    "Headset Right Playback",
    "Earphone Playback Switch",
    "AUDUL Voice UL Volume",
    "Headset Playback Volume",
    "Earphone Playback Volume",
    "DMIC1 UL Volume",
};
const size_t glass_required_ctls_count = ARRAY_LEN(glass_required_ctls);

/*
 * Spec 4.2, in the stock order. Kernel files: sound/soc/omap/omap-abe-mixer.c (ABE),
 * sound/soc/codecs/twl6040.c (codec), sound/soc/omap/omap-abe-twl6040.c (machine). The three
 * power-mode enums are the stock values, unchanged (rule 6). The stock table lists
 * "DL1 PDM Switch" twice; once is enough.
 */
const struct glass_ctl glass_init_ctls[] = {
    { "DL1 Media Playback Volume", NULL, 120 },  /* ABE, 0 dB */
    { "DL1 Voice Playback Volume", NULL, 120 },  /* ABE, 0 dB */
    { "SDT DL Volume", NULL, 120 },              /* ABE, 0 dB */
    { "Earphone Playback Switch", NULL, 1 },
    { "Earphone Playback Volume", NULL, GLASS_EARPHONE_VOLUME_MAX }, /* +6 dB */
    { "Headset Playback Volume", NULL, GLASS_HEADSET_VOLUME_MAX },   /* -6 dB */
    { "Capture Preamplifier Volume", NULL, 1 },  /* analog mics, not fitted */
    { "Capture Volume", NULL, 4 },               /* analog mics, not fitted */
    { "DL1 Mixer Multimedia", NULL, 1 },
    { "DL1 Mixer Voice", NULL, 1 },
    { "DL1 Mono Mixer", NULL, 0 },
    { "DL1 PDM Switch", NULL, 1 },
    { "Sidetone Mixer Playback", NULL, 1 },
    { "BT UL Volume", NULL, 120 },               /* ABE, 0 dB */
    { "TWL6040 Power Mode", "Low-Power", 0 },
    { "Headset Power Mode", "High-Performance", 0 },
    { "PLL Selection", "Low-Power", 0 },
    { "MUX_UL10", "DMic0L", 0 },                 /* MM_UL2 left */
    { "MUX_UL11", "DMic0R", 0 },                 /* MM_UL2 right */
    { GLASS_CTL_DMIC_GAIN, NULL, GLASS_DMIC_GAIN_DEFAULT },
    { "MUX_VX0", "DMic0L", 0 },                  /* voice uplink left, device 2 capture */
    { "MUX_VX1", "DMic0R", 0 },                  /* voice uplink right */
    { "Voice Capture Mixer Capture", NULL, 1 },
};
const size_t glass_init_ctls_count = ARRAY_LEN(glass_init_ctls);

/* Spec 4.3. Headset Left/Right Playback are enums: "Off", "HS DAC", "Line-In amp". */
const struct glass_ctl glass_earpiece_ctls[] = {
    { "Earphone Playback Switch", NULL, 1 },
    { "Headset Left Playback", "Off", 0 },
    { "Headset Right Playback", "Off", 0 },
};
const size_t glass_earpiece_ctls_count = ARRAY_LEN(glass_earpiece_ctls);

const struct glass_ctl glass_headset_ctls[] = {
    { "Earphone Playback Switch", NULL, 0 },
    { "Headset Left Playback", "HS DAC", 0 },
    { "Headset Right Playback", "HS DAC", 0 },
};
const size_t glass_headset_ctls_count = ARRAY_LEN(glass_headset_ctls);

const struct glass_pcm_params glass_out_params = { 2, 48000, 1056, 4 };

static const struct glass_pcm_params in_mm = { 2, 48000, 1056, 4 };
static const struct glass_pcm_params in_wb = { 2, 16000, 384, 2 };
static const struct glass_pcm_params in_nb = { 2, 8000, 192, 2 };

int glass_mixer_missing(struct mixer *mixer)
{
    size_t i;
    int missing = 0;

    for (i = 0; i < glass_required_ctls_count; i++) {
        if (!mixer_get_ctl_by_name(mixer, glass_required_ctls[i])) {
            GLASS_LOGE("missing mixer control '%s'", glass_required_ctls[i]);
            missing++;
        }
    }
    return missing;
}

static int capped(const char *name, int value)
{
    if (!strcmp(name, "Earphone Playback Volume") && value > GLASS_EARPHONE_VOLUME_MAX)
        return GLASS_EARPHONE_VOLUME_MAX;
    if (!strcmp(name, "Headset Playback Volume") && value > GLASS_HEADSET_VOLUME_MAX)
        return GLASS_HEADSET_VOLUME_MAX;
    return value;
}

int glass_mixer_set_int(struct mixer *mixer, const char *name, int value)
{
    struct mixer_ctl *ctl = mixer_get_ctl_by_name(mixer, name);
    unsigned int i, n;
    int ret = 0;

    if (!ctl)
        return -ENOENT;
    value = capped(name, value);
    n = mixer_ctl_get_num_values(ctl);
    for (i = 0; i < n; i++) {
        int r = mixer_ctl_set_value(ctl, i, value);
        if (r < 0)
            ret = r;
    }
    return ret;
}

int glass_mixer_apply(struct mixer *mixer, const struct glass_ctl *ctls, size_t count)
{
    size_t i;
    int failed = 0;

    for (i = 0; i < count; i++) {
        int r;
        if (ctls[i].enum_str) {
            struct mixer_ctl *ctl = mixer_get_ctl_by_name(mixer, ctls[i].name);
            r = ctl ? mixer_ctl_set_enum_by_string(ctl, ctls[i].enum_str) : -ENOENT;
        } else {
            r = glass_mixer_set_int(mixer, ctls[i].name, ctls[i].value);
        }
        if (r < 0) {
            GLASS_LOGE("cannot set mixer control '%s': %d", ctls[i].name, r);
            failed++;
        }
    }
    return failed;
}

enum glass_headset glass_parse_headset(const char *text)
{
    if (!text)
        return GLASS_HEADSET_UNREAD;
    if (!strncmp(text, "Mono", 4))
        return GLASS_HEADSET_MONO;
    if (!strncmp(text, "Stereo", 6))
        return GLASS_HEADSET_STEREO;
    return GLASS_HEADSET_NONE;
}

enum glass_path glass_select_path(enum glass_headset headset, uint32_t routing)
{
    switch (headset) {
    case GLASS_HEADSET_MONO:
    case GLASS_HEADSET_STEREO:
        return GLASS_PATH_HEADSET;
    case GLASS_HEADSET_NONE:
        return GLASS_PATH_EARPIECE;
    default:
        break;
    }
    if (routing & (GLASS_DEV_OUT_WIRED_HEADSET | GLASS_DEV_OUT_WIRED_HEADPHONE))
        return GLASS_PATH_HEADSET;
    return GLASS_PATH_EARPIECE;
}

int glass_downmix_needed(enum glass_path path, enum glass_headset headset)
{
    if (path == GLASS_PATH_EARPIECE)
        return 1;
    /* Headset path: only a detected mono earbud needs it; unread detection plays stereo. */
    return headset == GLASS_HEADSET_MONO;
}

void glass_downmix_stereo(int16_t *buf, size_t frames)
{
    size_t i;

    for (i = 0; i < frames; i++) {
        int16_t m = (int16_t)(((int32_t)buf[2 * i] + buf[2 * i + 1]) / 2);
        buf[2 * i] = m;
        buf[2 * i + 1] = m;
    }
}

void glass_stereo_to_mono(const int16_t *in, int16_t *out, size_t frames)
{
    size_t i;

    /* Forward order is safe in place: out[i] is written after in[2i] and in[2i+1] are read. */
    for (i = 0; i < frames; i++)
        out[i] = (int16_t)(((int32_t)in[2 * i] + in[2 * i + 1]) / 2);
}

int glass_input_supported(uint32_t rate, int is_pcm16, unsigned int channels)
{
    static const uint32_t rates[] = { 8000, 11025, 16000, 22050, 24000, 32000, 44100, 48000 };
    size_t i;

    if (!is_pcm16 || channels < 1 || channels > 2)
        return 0;
    for (i = 0; i < ARRAY_LEN(rates); i++) {
        if (rates[i] == rate)
            return 1;
    }
    return 0;
}

void glass_input_params(uint32_t rate, struct glass_pcm_params *out)
{
    if (rate > 16000)
        *out = in_mm;
    else if (rate > 8000)
        *out = in_wb;
    else
        *out = in_nb;
}

size_t glass_input_buffer_size(uint32_t rate, unsigned int channels)
{
    struct glass_pcm_params p;
    size_t frames;

    glass_input_params(rate, &p);
    frames = ((size_t)p.period_size * rate) / p.rate;
    frames = (frames + 15) & ~(size_t)15;
    return frames * channels * sizeof(int16_t);
}

size_t glass_startup_mute_bytes(uint32_t rate, size_t frame_size)
{
    return (size_t)rate * frame_size / 20;
}

int glass_pause_for_source(int source)
{
    /* Stock pauses for every source but VOICE_RECOGNITION, the always-on hotword. */
    return source != GLASS_SOURCE_VOICE_RECOGNITION;
}

/* Finds key at the start of a ';'-separated item. Returns a pointer past the key, or NULL. */
static const char *find_key(const char *kvpairs, const char *key)
{
    size_t klen = strlen(key);
    const char *p = kvpairs;

    if (!kvpairs || !klen)
        return NULL;
    while (*p) {
        if (!strncmp(p, key, klen) && (p[klen] == '=' || p[klen] == ';' || p[klen] == '\0'))
            return p + klen;
        p = strchr(p, ';');
        if (!p)
            break;
        p++;
    }
    return NULL;
}

int glass_param_has(const char *kvpairs, const char *key)
{
    return find_key(kvpairs, key) != NULL;
}

static int parse_int(const char *start, const char *end, int *value)
{
    char buf[24];
    char *stop;
    long v;
    size_t len = (size_t)(end - start);

    if (len == 0 || len >= sizeof(buf))
        return -1;
    memcpy(buf, start, len);
    buf[len] = '\0';
    errno = 0;
    v = strtol(buf, &stop, 0);
    if (errno || *stop || v < INT_MIN || v > INT_MAX)
        return -1;
    *value = (int)v;
    return 0;
}

int glass_param_int(const char *kvpairs, const char *key, int *value)
{
    const char *p = find_key(kvpairs, key);
    const char *end;

    if (!p)
        return 0;
    if (*p != '=')
        return -1;
    p++;
    end = strchr(p, ';');
    if (!end)
        end = p + strlen(p);
    return parse_int(p, end, value) ? -1 : 1;
}

int glass_prop_int(const char *text, int def)
{
    int v;

    if (!text || parse_int(text, text + strlen(text), &v))
        return def;
    return v;
}

int glass_presented_frames(uint64_t written, unsigned int buffer_frames, unsigned int avail,
                           uint64_t *presented)
{
    uint64_t queued = avail < buffer_frames ? buffer_frames - avail : 0;

    if (queued > written)
        return -1;
    *presented = written - queued;
    return 0;
}

static int join(char *buf, size_t size, const char *root, const char *path)
{
    int n = snprintf(buf, size, "%s%s", root, path);
    return (n < 0 || (size_t)n >= size) ? -1 : 0;
}

int glass_audio_read(const char *root, const char *path, char *buf, size_t size)
{
    char full[256];
    ssize_t n;
    int fd;

    if (size == 0 || join(full, sizeof(full), root, path))
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

int glass_audio_write(const char *root, const char *path, const char *value)
{
    char full[256];
    size_t len = strlen(value);
    ssize_t n;
    int fd, err = 0;

    if (join(full, sizeof(full), root, path))
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
