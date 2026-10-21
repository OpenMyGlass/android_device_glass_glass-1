/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Glass audio HAL logic: mixer tables, path selection, parameter parsing, pcm parameters,
 * down-mix and sysfs access. It has no Android headers, so tests/test_audio_logic.py builds
 * and runs it on the host against tests/mock_tinyalsa.c. Sources of every value:
 * docs/specs/audio.md (section numbers below) and docs/hals.md.
 */
#ifndef GLASS_AUDIO_LOGIC_H
#define GLASS_AUDIO_LOGIC_H

#include <stddef.h>
#include <stdint.h>

#include "glass_tinyalsa.h"

/* ALSA card "Notle" (omap-abe-twl6040) and its front-end PCM devices (spec 1.1). */
#define GLASS_CARD 0
#define GLASS_OUT_DEVICE 0 /* OMAP ABE Media, MultiMedia1 FE */
#define GLASS_IN_DEVICE 2  /* OMAP ABE Voice, VX_UL <- MUX_VX0/1 <- DMIC0 L/R */

/* Sysfs (spec 4.3 and 6; kernel notle-usb-mux.c and glasshub.c). */
#define GLASS_USB_MUX "/sys/devices/platform/usb_mux.0/"
#define GLASS_HEADSET_PATH GLASS_USB_MUX "headset"       /* 0444, measures the ID pin */
#define GLASS_MUX_MODE_PATH GLASS_USB_MUX "mux_mode"     /* media 0664 (init.omap4430.rc) */
#define GLASS_ID_MONITOR_PATH GLASS_USB_MUX "id_monitor" /* kernel mode 0666 */
#define GLASS_PAUSE_FOR_AUDIO_PATH \
    "/sys/devices/platform/omap_i2c.4/i2c-4/4-0035/pause_for_audio" /* media 0664 */

/* The highest values the HAL ever writes: the stock ones (spec 4.2, rule 6). */
#define GLASS_EARPHONE_VOLUME_MAX 15 /* TWL6040 EP, +6 dB */
#define GLASS_HEADSET_VOLUME_MAX 12  /* TWL6040 HS, -6 dB */

/* Capture gain defaults and the properties that override them (spec 4.4). */
#define GLASS_DMIC_GAIN_DEFAULT 146  /* DMIC1 UL Volume, +26 dB */
#define GLASS_VOICE_GAIN_DEFAULT 120 /* AUDUL Voice UL Volume, 0 dB */
#define GLASS_PROP_DMIC_GAIN "audio.dmic1gain"
#define GLASS_PROP_VOICE_GAIN "audio.voicegain"
#define GLASS_PROP_IN_RATE "audio.in.rate"

#define GLASS_CTL_DMIC_GAIN "DMIC1 UL Volume"
#define GLASS_CTL_VOICE_GAIN "AUDUL Voice UL Volume"

/*
 * Output devices from system/audio.h, which audio_hw.c checks with static asserts.
 * The same values in 4.4 and 6.0.
 */
#define GLASS_DEV_OUT_EARPIECE 0x1u
#define GLASS_DEV_OUT_SPEAKER 0x2u
#define GLASS_DEV_OUT_WIRED_HEADSET 0x4u
#define GLASS_DEV_OUT_WIRED_HEADPHONE 0x8u
#define GLASS_SOURCE_VOICE_RECOGNITION 6 /* AUDIO_SOURCE_VOICE_RECOGNITION */

/* What usb_mux.0/headset reports. The numbers are the stock "output_type" values (5.3). */
enum glass_headset {
    GLASS_HEADSET_UNREAD = 0, /* the file could not be read */
    GLASS_HEADSET_NONE = 1,   /* "None", "Unknown" or anything else */
    GLASS_HEADSET_MONO = 2,   /* "Mono": about 1 MOhm on the ID pin */
    GLASS_HEADSET_STEREO = 3, /* "Stereo": about 500 kOhm */
};

enum glass_path {
    GLASS_PATH_EARPIECE, /* TWL6040 EP output: the bone conduction transducer */
    GLASS_PATH_HEADSET,  /* TWL6040 HSOL/HSOR through the USB mux: an earbud */
};

/* One mixer control setting: an enum string if enum_str is set, else value on every channel. */
struct glass_ctl {
    const char *name;
    const char *enum_str;
    int value;
};

/* Controls that must exist for the HAL to open (spec 4.1). */
extern const char *const glass_required_ctls[];
extern const size_t glass_required_ctls_count;
/* Written once when the device opens (spec 4.2). */
extern const struct glass_ctl glass_init_ctls[];
extern const size_t glass_init_ctls_count;
/* Written on every output start and routing change (spec 4.3). */
extern const struct glass_ctl glass_earpiece_ctls[];
extern const size_t glass_earpiece_ctls_count;
extern const struct glass_ctl glass_headset_ctls[];
extern const size_t glass_headset_ctls_count;

/* pcm parameters, converted to a struct pcm_config by audio_hw.c. Always S16_LE. */
struct glass_pcm_params {
    unsigned int channels;
    unsigned int rate;
    unsigned int period_size;
    unsigned int period_count;
};

/* The primary output: device 0, stereo, 48 kHz, 4 periods of 1056 frames (88 ms). */
extern const struct glass_pcm_params glass_out_params;

/* Number of required controls missing from mixer. */
int glass_mixer_missing(struct mixer *mixer);

/*
 * Applies a table. Integer values go to every channel. An Earphone or Headset Playback
 * Volume above its GLASS_*_VOLUME_MAX is lowered to the maximum. Returns the number of
 * controls that are missing or could not be set.
 */
int glass_mixer_apply(struct mixer *mixer, const struct glass_ctl *ctls, size_t count);

/* Sets an integer control on every channel. Returns 0 or a negative errno. */
int glass_mixer_set_int(struct mixer *mixer, const char *name, int value);

/* Parses the text of usb_mux.0/headset. NULL gives GLASS_HEADSET_UNREAD. */
enum glass_headset glass_parse_headset(const char *text);

/*
 * Chooses the transducer. The headset detection decides when it could be read, as on stock
 * (a mono or stereo earbud gives the headset path, nothing gives the earpiece). Only when
 * it could not be read does the routing from AudioPolicy decide: WIRED_HEADSET or
 * WIRED_HEADPHONE gives the headset path, anything else the earpiece.
 */
enum glass_path glass_select_path(enum glass_headset headset, uint32_t routing);

/* 1 if stereo output must be down-mixed to mono: everything but a stereo earbud. */
int glass_downmix_needed(enum glass_path path, enum glass_headset headset);

/* In place: both channels of each interleaved stereo frame become (L + R) / 2. */
void glass_downmix_stereo(int16_t *buf, size_t frames);

/* Stereo to mono, out[i] = (L + R) / 2. out may be the same buffer as in. */
void glass_stereo_to_mono(const int16_t *in, int16_t *out, size_t frames);

/* 1 if a capture request is one the HAL accepts (spec 5.2), else 0. */
int glass_input_supported(uint32_t rate, int is_pcm16, unsigned int channels);

/*
 * The capture config for a rate (spec 5.2): above 16000 Hz 48 kHz (1056 x 4), from 8001 to
 * 16000 Hz 16 kHz (384 x 2), up to 8000 Hz 8 kHz (192 x 2). Always 2 channels.
 */
void glass_input_params(uint32_t rate, struct glass_pcm_params *out);

/*
 * Bytes of one read the framework should make: one period of the capture config, converted
 * to the requested rate, rounded up to 16 frames.
 */
size_t glass_input_buffer_size(uint32_t rate, unsigned int channels);

/* Bytes zero-filled after each capture start: 50 ms (spec 5.2). */
size_t glass_startup_mute_bytes(uint32_t rate, size_t frame_size);

/* 1 if starting capture with this source pauses the glasshub (spec 6), else 0. */
int glass_pause_for_source(int source);

/*
 * Reads integer key from "k1=v1;k2=v2". Returns 1 and sets *value if found, 0 if the key is
 * absent, -1 if its value is not an integer.
 */
int glass_param_int(const char *kvpairs, const char *key, int *value);

/* 1 if key appears in "k1;k2=v2" (with or without a value), else 0. */
int glass_param_has(const char *kvpairs, const char *key);

/* Integer from a property value, or def if text is NULL, empty or not an integer. */
int glass_prop_int(const char *text, int def);

/*
 * Frames presented: written minus what is still queued (buffer_frames - avail). Returns 0,
 * or -1 if more is queued than written.
 */
int glass_presented_frames(uint64_t written, unsigned int buffer_frames, unsigned int avail,
                           uint64_t *presented);

/* Reads a small sysfs file under root, trailing whitespace removed. Length or -errno. */
int glass_audio_read(const char *root, const char *path, char *buf, size_t size);

/* Writes value to a sysfs file under root. 0 or -errno. */
int glass_audio_write(const char *root, const char *path, const char *value);

#endif
