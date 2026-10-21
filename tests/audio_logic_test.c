/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Host tests for audio/audio_logic.c. Built and run by tests/test_audio_logic.py with:
 * cc -DGLASS_HOST_TEST audio/audio_logic.c tests/mock_tinyalsa.c tests/audio_logic_test.c.
 * argv[1] is an empty scratch directory that stands in for sysfs.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "../audio/audio_logic.h"
#include "mock_tinyalsa.h"

static int failures;
static const char *tmp;

#define CHECK(cond) do { \
        if (!(cond)) { \
            fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

/* Kernel enum texts: twl6040.c (power modes, headset mux), omap-abe-mixer.c (UL muxes). */
static const char *const power_texts[] = { "Low-Power", "High-Performance", NULL };
static const char *const hs_texts[] = { "Off", "HS DAC", "Line-In amp", NULL };
static const char *const ul_texts[] = { "None", "DMic0L", "DMic0R", "DMic1L", "DMic1R", NULL };

#define INT_CTL(n, v, lo, hi) { (n), (v), (lo), (hi), NULL, { -1, -1 }, 0 }
#define ENUM_CTL(n, t) { (n), 1, 0, 0, (t), { -1, -1 }, 0 }

/* The card's controls the HAL touches, with the channel counts and ranges of the kernel. */
static struct mixer_ctl notle_ctls[] = {
    INT_CTL("DL1 Media Playback Volume", 1, 0, 149),
    INT_CTL("DL1 Voice Playback Volume", 1, 0, 149),
    INT_CTL("SDT DL Volume", 1, 0, 149),
    INT_CTL("Earphone Playback Switch", 1, 0, 1),
    INT_CTL("Earphone Playback Volume", 1, 0, 15),
    INT_CTL("Headset Playback Volume", 2, 0, 15),
    INT_CTL("Capture Preamplifier Volume", 2, 0, 1),
    INT_CTL("Capture Volume", 2, 0, 4),
    INT_CTL("DL1 Mixer Multimedia", 1, 0, 1),
    INT_CTL("DL1 Mixer Voice", 1, 0, 1),
    INT_CTL("DL1 Mono Mixer", 1, 0, 1),
    INT_CTL("DL1 PDM Switch", 1, 0, 1),
    INT_CTL("Sidetone Mixer Playback", 1, 0, 1),
    INT_CTL("BT UL Volume", 2, 0, 149),
    ENUM_CTL("TWL6040 Power Mode", power_texts),
    ENUM_CTL("Headset Power Mode", power_texts),
    ENUM_CTL("PLL Selection", power_texts),
    ENUM_CTL("MUX_UL10", ul_texts),
    ENUM_CTL("MUX_UL11", ul_texts),
    INT_CTL("DMIC1 UL Volume", 2, 0, 149),
    ENUM_CTL("MUX_VX0", ul_texts),
    ENUM_CTL("MUX_VX1", ul_texts),
    INT_CTL("Voice Capture Mixer Capture", 1, 0, 1),
    INT_CTL("AUDUL Voice UL Volume", 1, 0, 149),
    ENUM_CTL("Headset Left Playback", hs_texts),
    ENUM_CTL("Headset Right Playback", hs_texts),
    INT_CTL("Handsfree Playback Volume", 2, 0, 29),
};

static struct mixer notle = { notle_ctls, sizeof(notle_ctls) / sizeof(notle_ctls[0]) };

static struct mixer_ctl *ctl(const char *name)
{
    struct mixer_ctl *c = mixer_get_ctl_by_name(&notle, name);
    if (!c) {
        fprintf(stderr, "no mock control %s\n", name);
        exit(2);
    }
    return c;
}

static int val(const char *name, unsigned int i)
{
    return ctl(name)->values[i];
}

static void reset_mixer(void)
{
    size_t i;
    for (i = 0; i < notle.count; i++) {
        notle_ctls[i].values[0] = notle_ctls[i].values[1] = -1;
        notle_ctls[i].sets = 0;
    }
}

static void test_tables(void)
{
    size_t i, j;

    /* Every control the HAL names is one the kernel card has. */
    CHECK(glass_mixer_missing(&notle) == 0);
    for (i = 0; i < glass_init_ctls_count; i++)
        CHECK(mixer_get_ctl_by_name(&notle, glass_init_ctls[i].name) != NULL);
    /* No control appears twice in the init table. */
    for (i = 0; i < glass_init_ctls_count; i++)
        for (j = i + 1; j < glass_init_ctls_count; j++)
            CHECK(strcmp(glass_init_ctls[i].name, glass_init_ctls[j].name) != 0);
    /* Rule 6: no table touches the handsfree outputs or goes above the stock volumes. */
    for (i = 0; i < glass_init_ctls_count; i++) {
        const struct glass_ctl *c = &glass_init_ctls[i];
        CHECK(strncmp(c->name, "Handsfree", 9) != 0);
        if (!strcmp(c->name, "Earphone Playback Volume"))
            CHECK(c->value == 15 && c->value <= GLASS_EARPHONE_VOLUME_MAX);
        if (!strcmp(c->name, "Headset Playback Volume"))
            CHECK(c->value == 12 && c->value <= GLASS_HEADSET_VOLUME_MAX);
    }
}

static void test_mixer_apply(void)
{
    struct mixer_ctl missing[] = { INT_CTL("DL1 Mixer Multimedia", 1, 0, 1) };
    struct mixer small = { missing, 1 };

    reset_mixer();
    CHECK(glass_mixer_apply(&notle, glass_init_ctls, glass_init_ctls_count) == 0);
    /* Stock values (spec 4.2), on every channel. */
    CHECK(val("DL1 Media Playback Volume", 0) == 120);
    CHECK(val("Earphone Playback Volume", 0) == 15);
    CHECK(val("Headset Playback Volume", 0) == 12 && val("Headset Playback Volume", 1) == 12);
    CHECK(val("DMIC1 UL Volume", 0) == 146 && val("DMIC1 UL Volume", 1) == 146);
    CHECK(val("BT UL Volume", 1) == 120);
    CHECK(val("Capture Volume", 1) == 4);
    CHECK(val("DL1 PDM Switch", 0) == 1);
    CHECK(val("DL1 Mono Mixer", 0) == 0);
    CHECK(val("TWL6040 Power Mode", 0) == 0);  /* Low-Power */
    CHECK(val("Headset Power Mode", 0) == 1);  /* High-Performance */
    CHECK(val("PLL Selection", 0) == 0);       /* Low-Power */
    CHECK(val("MUX_UL10", 0) == 1 && val("MUX_UL11", 0) == 2);
    CHECK(val("MUX_VX0", 0) == 1 && val("MUX_VX1", 0) == 2);
    CHECK(val("Voice Capture Mixer Capture", 0) == 1);
    CHECK(ctl("Handsfree Playback Volume")->sets == 0);
    CHECK(ctl("AUDUL Voice UL Volume")->sets == 0);

    CHECK(glass_mixer_apply(&notle, glass_headset_ctls, glass_headset_ctls_count) == 0);
    CHECK(val("Earphone Playback Switch", 0) == 0);
    CHECK(val("Headset Left Playback", 0) == 1 && val("Headset Right Playback", 0) == 1);
    CHECK(glass_mixer_apply(&notle, glass_earpiece_ctls, glass_earpiece_ctls_count) == 0);
    CHECK(val("Earphone Playback Switch", 0) == 1);
    CHECK(val("Headset Left Playback", 0) == 0 && val("Headset Right Playback", 0) == 0);

    /* The volume cap holds whatever value is asked for. */
    CHECK(glass_mixer_set_int(&notle, "Earphone Playback Volume", 15) == 0);
    CHECK(glass_mixer_set_int(&notle, "Earphone Playback Volume", 99) == 0);
    CHECK(val("Earphone Playback Volume", 0) == GLASS_EARPHONE_VOLUME_MAX);
    CHECK(glass_mixer_set_int(&notle, "Headset Playback Volume", 15) == 0);
    CHECK(val("Headset Playback Volume", 0) == 12 && val("Headset Playback Volume", 1) == 12);
    CHECK(glass_mixer_set_int(&notle, "Earphone Playback Volume", 3) == 0);
    CHECK(val("Earphone Playback Volume", 0) == 3);

    /* Gains go to both DMIC channels; out-of-range values fail. */
    CHECK(glass_mixer_set_int(&notle, GLASS_CTL_DMIC_GAIN, 140) == 0);
    CHECK(val("DMIC1 UL Volume", 0) == 140 && val("DMIC1 UL Volume", 1) == 140);
    CHECK(glass_mixer_set_int(&notle, GLASS_CTL_DMIC_GAIN, 500) == -EINVAL);
    CHECK(glass_mixer_set_int(&notle, "No Such Control", 1) == -ENOENT);

    /* A card without the Glass controls is refused; failures are counted. */
    CHECK(glass_mixer_missing(&small) == (int)glass_required_ctls_count - 1);
    CHECK(glass_mixer_apply(&small, glass_earpiece_ctls, glass_earpiece_ctls_count) == 3);
}

static void test_path(void)
{
    const uint32_t ep = GLASS_DEV_OUT_EARPIECE, spk = GLASS_DEV_OUT_SPEAKER;
    const uint32_t hs = GLASS_DEV_OUT_WIRED_HEADSET, hp = GLASS_DEV_OUT_WIRED_HEADPHONE;

    CHECK(glass_parse_headset("None") == GLASS_HEADSET_NONE);
    CHECK(glass_parse_headset("Unknown") == GLASS_HEADSET_NONE);
    CHECK(glass_parse_headset("Impossible") == GLASS_HEADSET_NONE);
    CHECK(glass_parse_headset("") == GLASS_HEADSET_NONE);
    CHECK(glass_parse_headset("Mono") == GLASS_HEADSET_MONO);
    CHECK(glass_parse_headset("Stereo") == GLASS_HEADSET_STEREO);
    CHECK(glass_parse_headset(NULL) == GLASS_HEADSET_UNREAD);

    /* Detection wins, whatever AudioPolicy routed. */
    CHECK(glass_select_path(GLASS_HEADSET_NONE, hs) == GLASS_PATH_EARPIECE);
    CHECK(glass_select_path(GLASS_HEADSET_NONE, spk) == GLASS_PATH_EARPIECE);
    CHECK(glass_select_path(GLASS_HEADSET_MONO, spk) == GLASS_PATH_HEADSET);
    CHECK(glass_select_path(GLASS_HEADSET_STEREO, ep) == GLASS_PATH_HEADSET);
    /* Without detection, the routing decides. */
    CHECK(glass_select_path(GLASS_HEADSET_UNREAD, hs) == GLASS_PATH_HEADSET);
    CHECK(glass_select_path(GLASS_HEADSET_UNREAD, hp | spk) == GLASS_PATH_HEADSET);
    CHECK(glass_select_path(GLASS_HEADSET_UNREAD, spk) == GLASS_PATH_EARPIECE);
    CHECK(glass_select_path(GLASS_HEADSET_UNREAD, ep) == GLASS_PATH_EARPIECE);
    CHECK(glass_select_path(GLASS_HEADSET_UNREAD, 0) == GLASS_PATH_EARPIECE);

    CHECK(glass_downmix_needed(GLASS_PATH_EARPIECE, GLASS_HEADSET_NONE));
    CHECK(glass_downmix_needed(GLASS_PATH_EARPIECE, GLASS_HEADSET_UNREAD));
    CHECK(glass_downmix_needed(GLASS_PATH_HEADSET, GLASS_HEADSET_MONO));
    CHECK(!glass_downmix_needed(GLASS_PATH_HEADSET, GLASS_HEADSET_STEREO));
    CHECK(!glass_downmix_needed(GLASS_PATH_HEADSET, GLASS_HEADSET_UNREAD));
}

static void test_downmix(void)
{
    int16_t buf[] = { 100, 300, -32768, -32768, 32767, 32767, 32767, -32768, -3, 0 };
    int16_t mono[5];

    glass_stereo_to_mono(buf, mono, 5);
    CHECK(mono[0] == 200 && mono[1] == -32768 && mono[2] == 32767 && mono[3] == 0);
    CHECK(mono[4] == -1);

    glass_downmix_stereo(buf, 5);
    CHECK(buf[0] == 200 && buf[1] == 200);
    CHECK(buf[2] == -32768 && buf[3] == -32768);
    CHECK(buf[4] == 32767 && buf[5] == 32767);
    CHECK(buf[6] == 0 && buf[7] == 0);
    CHECK(buf[8] == -1 && buf[9] == -1);

    /* In place, as in_read does it. */
    {
        int16_t st[] = { 2, 4, 6, 8, 10, 12 };
        glass_stereo_to_mono(st, st, 3);
        CHECK(st[0] == 3 && st[1] == 7 && st[2] == 11);
    }
}

static void test_input(void)
{
    static const uint32_t ok[] = { 8000, 11025, 16000, 22050, 24000, 32000, 44100, 48000 };
    struct glass_pcm_params p;
    size_t i;

    for (i = 0; i < sizeof(ok) / sizeof(ok[0]); i++) {
        CHECK(glass_input_supported(ok[i], 1, 1));
        CHECK(glass_input_supported(ok[i], 1, 2));
    }
    CHECK(!glass_input_supported(96000, 1, 2));
    CHECK(!glass_input_supported(12000, 1, 2));
    CHECK(!glass_input_supported(48000, 0, 2));
    CHECK(!glass_input_supported(48000, 1, 0));
    CHECK(!glass_input_supported(48000, 1, 4));

    glass_input_params(48000, &p);
    CHECK(p.channels == 2 && p.rate == 48000 && p.period_size == 1056 && p.period_count == 4);
    glass_input_params(16001, &p);
    CHECK(p.rate == 48000);
    glass_input_params(16000, &p);
    CHECK(p.channels == 2 && p.rate == 16000 && p.period_size == 384 && p.period_count == 2);
    glass_input_params(11025, &p);
    CHECK(p.rate == 16000);
    glass_input_params(8001, &p);
    CHECK(p.rate == 16000);
    glass_input_params(8000, &p);
    CHECK(p.channels == 2 && p.rate == 8000 && p.period_size == 192 && p.period_count == 2);

    /* One period at the requested rate, rounded up to 16 frames. */
    CHECK(glass_input_buffer_size(48000, 2) == 1056 * 4);
    CHECK(glass_input_buffer_size(48000, 1) == 1056 * 2);
    CHECK(glass_input_buffer_size(44100, 1) == 976 * 2);   /* 970.2 -> 976 */
    CHECK(glass_input_buffer_size(16000, 1) == 384 * 2);
    CHECK(glass_input_buffer_size(11025, 2) == 272 * 4);   /* 264.6 -> 272 */
    CHECK(glass_input_buffer_size(8000, 1) == 192 * 2);
    for (i = 0; i < sizeof(ok) / sizeof(ok[0]); i++)
        CHECK(glass_input_buffer_size(ok[i], 1) % 32 == 0);

    CHECK(glass_startup_mute_bytes(48000, 4) == 9600);
    CHECK(glass_startup_mute_bytes(16000, 2) == 1600);
    CHECK(glass_startup_mute_bytes(8000, 2) == 800);

    CHECK(glass_pause_for_source(1));  /* MIC */
    CHECK(glass_pause_for_source(5));  /* CAMCORDER */
    CHECK(glass_pause_for_source(7));  /* VOICE_COMMUNICATION */
    CHECK(glass_pause_for_source(0));  /* DEFAULT */
    CHECK(!glass_pause_for_source(GLASS_SOURCE_VOICE_RECOGNITION));

    CHECK(glass_out_params.channels == 2 && glass_out_params.rate == 48000);
    CHECK(glass_out_params.period_size == 1056 && glass_out_params.period_count == 4);
    CHECK(glass_out_params.period_size % 16 == 0);
}

static void test_params(void)
{
    int v = -7;

    CHECK(glass_param_int("routing=4", "routing", &v) == 1 && v == 4);
    CHECK(glass_param_int("input_source=6;routing=2147483652", "routing", &v) == -1);
    CHECK(glass_param_int("input_source=6;routing=-2147483644", "routing", &v) == 1);
    CHECK(v == -2147483644);
    CHECK(glass_param_int("a=1;routing=0x8;b=2", "routing", &v) == 1 && v == 8);
    CHECK(glass_param_int("input_source=6;routing=4", "input_source", &v) == 1 && v == 6);
    CHECK(glass_param_int("myrouting=4", "routing", &v) == 0);
    CHECK(glass_param_int("routingx=4", "routing", &v) == 0);
    CHECK(glass_param_int("", "routing", &v) == 0);
    CHECK(glass_param_int(NULL, "routing", &v) == 0);
    CHECK(glass_param_int("routing=", "routing", &v) == -1);
    CHECK(glass_param_int("routing=abc", "routing", &v) == -1);
    CHECK(glass_param_int("routing", "routing", &v) == -1);
    CHECK(glass_param_int("voice_volume=130", "voice_volume", &v) == 1 && v == 130);

    CHECK(glass_param_has("voice_volume;output_type", "output_type"));
    CHECK(glass_param_has("voice_volume;output_type", "voice_volume"));
    CHECK(glass_param_has("output_type", "output_type"));
    CHECK(!glass_param_has("output_types", "output_type"));
    CHECK(!glass_param_has("", "output_type"));

    CHECK(glass_prop_int("140", 146) == 140);
    CHECK(glass_prop_int("", 146) == 146);
    CHECK(glass_prop_int(NULL, 146) == 146);
    CHECK(glass_prop_int("12x", 146) == 146);
    CHECK(glass_prop_int("-3", 0) == -3);
}

static void test_presented(void)
{
    uint64_t f = 0;

    CHECK(glass_presented_frames(10000, 4224, 4224, &f) == 0 && f == 10000);
    CHECK(glass_presented_frames(10000, 4224, 1000, &f) == 0 && f == 10000 - 3224);
    CHECK(glass_presented_frames(100, 4224, 0, &f) == -1);
    CHECK(glass_presented_frames(100, 4224, 9000, &f) == 0 && f == 100);
}

static void test_sysfs(void)
{
    char root[512], dir[600], buf[64];
    FILE *f;

    snprintf(root, sizeof(root), "%s/sys", tmp);
    mkdir(root, 0755);
    snprintf(dir, sizeof(dir), "%s/sys/devices", tmp);
    mkdir(dir, 0755);
    snprintf(dir, sizeof(dir), "%s/sys/devices/platform", tmp);
    mkdir(dir, 0755);
    snprintf(dir, sizeof(dir), "%s/sys/devices/platform/usb_mux.0", tmp);
    mkdir(dir, 0755);
    snprintf(root, sizeof(root), "%s", tmp);

    CHECK(glass_audio_read(root, GLASS_HEADSET_PATH, buf, sizeof(buf)) == -ENOENT);
    snprintf(dir, sizeof(dir), "%s%s", root, GLASS_HEADSET_PATH);
    f = fopen(dir, "w");
    CHECK(f != NULL);
    if (!f)
        return;
    fputs("Stereo\n", f);
    fclose(f);
    CHECK(glass_audio_read(root, GLASS_HEADSET_PATH, buf, sizeof(buf)) == 6);
    CHECK(glass_parse_headset(buf) == GLASS_HEADSET_STEREO);

    /* The writable attributes: the kernel takes "audio" and "1"/"0" without a newline. */
    snprintf(dir, sizeof(dir), "%s%s", root, GLASS_MUX_MODE_PATH);
    fclose(fopen(dir, "w"));
    CHECK(glass_audio_write(root, GLASS_MUX_MODE_PATH, "audio") == 0);
    CHECK(glass_audio_read(root, GLASS_MUX_MODE_PATH, buf, sizeof(buf)) == 5);
    CHECK(!strcmp(buf, "audio"));
    CHECK(glass_audio_write(root, GLASS_PAUSE_FOR_AUDIO_PATH, "1") == -ENOENT);
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s <scratch dir>\n", argv[0]);
        return 2;
    }
    tmp = argv[1];
    test_tables();
    test_mixer_apply();
    test_path();
    test_downmix();
    test_input();
    test_params();
    test_presented();
    test_sysfs();
    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("audio logic: all checks passed\n");
    return 0;
}
