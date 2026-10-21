<!-- SPDX-License-Identifier: Apache-2.0 -->
# Audio HAL specification (Glass Explorer Edition, XE24)

> This specification was written by a separate reverse-engineering agent, which studied the
> stock XE24 binaries in a workspace outside this repository (clean room). The HAL in
> `audio/` was implemented from this text, the GPL kernel source and public AOSP code only.
> It is committed as written; `docs/hals.md` says where the HAL differs from section 7.

Clean-room specification for an open `audio.primary.glass_1` HAL. Facts only; each with its source:

- **kernel**: GPL source, branch `glass-omap-xrx13b-stable-3.4.113` of OpenMyGlass/kernel-omap, `file:line`.
- **conf**: stock `/system/etc/audio_policy.conf`, rc files, `build.prop`.
- **blob**: stock XE24 binaries with link-time address of the function or data: `lib/hw/audio.primary.glass_1.so`
  (18712 B, sha256 `88dca0e3…a2fc`), `lib/libtinyalsa.so` (21988 B, sha256 `9176a15c…754b`, a modified tinyalsa
  with an effects layer), `lib/libglassalsa.so` (18072 B, sha256 `82366c52…0794`, DSP effects used by that
  tinyalsa). Function names of `audio.primary.glass_1.so` are not exported; they are referred to by address and
  by the role they play in `audio_hw_device`/`audio_stream` tables.

No code from the binaries is reproduced.

## 1. Hardware and kernel ALSA card

| Item | Value | Source |
|---|---|---|
| Codec | TI TWL6040 (audio power GPIO 127, MCLK 38.4 MHz) | kernel `arch/arm/mach-omap2/board-notle.c:1371-1393, 1298-1311, 2024-2031` |
| Machine driver | `omap-abe-twl6040`, card name **"Notle"**, ABE enabled, DMIC enabled | `board-notle.c:1298-1319`; `sound/soc/omap/omap-abe-twl6040.c:1455-1505` |
| Connected outputs (pdata) | headset (HSOL/HSOR) L+R, handsfree (HFL/HFR) L+R, aux L+R, earpiece (`has_ep`), FM line-in | `board-notle.c:1299-1310` |
| Disconnected | vibrator, headset mic, main mic, sub mic (all analog mics off: `has_hsmic` = 0, `has_mainmic`/`has_submic` unset) | `board-notle.c:1306`; pins disabled in `omap-abe-twl6040.c:543-551` |
| Microphones | digital mics via OMAP DMIC (`dmic-codec`), DMIC0 L/R used by the stock HAL | `board-notle.c:1293-1296`; HAL mixer table (section 4) |
| Jack detection | Notle-specific: headset detected through the USB connector ID resistor (`notle_hs_jack_detect`), also a switch device `h2w` | `omap-abe-twl6040.c:600-605`; `arch/arm/mach-omap2/notle-usb-mux.c:243-279, 663-668` |
| Other ALSA cards | USB audio class driver built in (`CONFIG_SND_USB_AUDIO=y`) | `notle_defconfig:2104` |

### 1.1 PCM device numbers on card 0

Device numbers equal the index in `omap_abe_dai_link[]` (`omap-abe-twl6040.c:719-1151`); back-ends (`no_pcm`)
take a number but create no `/dev/snd` node (`sound/soc/soc-pcm.c:2411-2450`).

| Dev | Name / stream | CPU DAI | Kind |
|---|---|---|---|
| 0 | OMAP ABE Media / Multimedia | MultiMedia1 | FE playback (+ capture) |
| 1 | OMAP ABE Media Capture / Multimedia Capture | MultiMedia2 | FE capture (MM_UL2) |
| 2 | OMAP ABE Voice / Voice | Voice | FE playback + capture (VX_DL / VX_UL) |
| 3 | OMAP ABE Tones Playback | Tones | FE |
| 4 | OMAP ABE Vibra Playback | Vibra | FE |
| 5 | OMAP ABE MODEM | MODEM | FE |
| 6 | OMAP ABE Media LP / Multimedia | MultiMedia1 LP | FE (low-power) |
| 7 | Legacy McBSP | omap-mcbsp.2 | legacy |
| 8 | Legacy McPDM / Headset Playback | mcpdm-legacy | legacy |
| 9 | SPDIF | mcasp-legacy | legacy |
| 10 | Legacy DMIC / DMIC Capture | omap-dmic | legacy |
| 11 | Bluetooth McBSP / Bluetooth Audio | omap-mcbsp.1 | FE (BT voice) |
| 12–25 | PDM_DL1, PDM_UL1, PDM_DL2, PDM_VIB, BT_VX_UL, BT_VX_DL, MM_EXT0_UL, MM_EXT0_DL, MM_EXT1, DMIC0, DMIC1, DMIC2, VXREC, ECHO | — | back-ends, no node |

## 2. Stock HAL module and device declaration (API versions)

| Item | Value | Where |
|---|---|---|
| `hw_module_t.module_api_version` | 0x0002 | blob `audio.primary.glass_1.so`, symbol `HMI` @0x5200 |
| `hal_api_version` | 0 | same |
| `id` / `name` / `author` | `"audio"` / `"Notle audio HW HAL"` / `"The Android Open Source Project"` | same |
| Device open | accepts only the `"audio_hw_if"` name | blob device-open @0x21c4 |
| `audio_hw_device.common.version` | 0x0200 = `AUDIO_DEVICE_API_VERSION_2_0` | blob @0x21c4 (stores 0x200) |
| `get_supported_devices` | returns 0xC00000FF (legacy 1.0 mask style; ignored by 2.0 framework) | blob @0x1214 |
| `set_mode`, `set_voice_volume`, `set_master_volume`, `get_master_volume` | no-ops returning 0 (no in-call support) | blob @0x11e0–0x11f8 region |
| `set_mic_mute` / `get_mic_mute` | stored flag; when set, captured buffers are zero-filled | blob @0x11fc, @0x1204, input read @0x2524 |
| `open_output_stream`, `open_input_stream` | one primary output, one input | blob @0x159c, @0x1774 |

Stock `libaudio` users are the KitKat AudioFlinger with `audio_policy.default.so`. A 2.0 device works with the legacy
audio policy on 4.4.4 and on 6.0.1 (device API 2.0 is still accepted by the 6.0.1 framework; 3.0 is optional).

## 3. Stock audio policy (`/system/etc/audio_policy.conf`, facts as a table)

| Section | Key | Value |
|---|---|---|
| global | attached_output_devices | EARPIECE, SPEAKER |
| global | default_output_device | SPEAKER |
| global | attached_input_devices | BUILTIN_MIC, BACK_MIC |
| primary / outputs / primary | sampling_rates | 44100 |
| | channel_masks | OUT_STEREO |
| | formats | PCM_16_BIT |
| | devices | EARPIECE, SPEAKER, WIRED_HEADSET, WIRED_HEADPHONE, ALL_SCO |
| | flags | PRIMARY |
| primary / inputs / primary | sampling_rates | 8000, 11025, 16000, 22050, 24000, 32000, 44100, 48000 |
| | channel_masks | IN_MONO, IN_STEREO, IN_FRONT_BACK |
| | formats | PCM_16_BIT |
| | devices | BUILTIN_MIC, BLUETOOTH_SCO_HEADSET, WIRED_HEADSET, BACK_MIC |
| usb / outputs / usb_accessory | 44100, OUT_STEREO, PCM_16_BIT, OUT_USB_ACCESSORY | |
| usb / outputs / usb_device | 44100, OUT_STEREO, PCM_16_BIT, OUT_USB_DEVICE | |

There is no `a2dp` module in the stock policy file. The primary output profile says 44100 Hz but the HAL always
reports 48000 Hz for its output (section 5.1).

## 4. Mixer controls

### 4.1 Controls the stock HAL requires

At device open the HAL opens mixer 0 and looks up these controls; if any is missing it fails with "Unable to
locate all mixer controls" (blob @0x21c4, names at .rodata 0x2b52–0x2c13):

`DL1 Mixer Multimedia`, `DL1 Mixer Voice`, `DL1 PDM Switch`, `Headset Left Playback`, `Headset Right Playback`,
`Earphone Playback Switch`, `AUDUL Voice UL Volume`, `Headset Playback Volume`, `Earphone Playback Volume`,
`DMIC1 UL Volume`.

### 4.2 Initial values written at device open

Table in blob `.data` @0x50d4 (name, integer value or enum string), applied in order to every channel of the control:

| Control | Value | Meaning (kernel) |
|---|---|---|
| DL1 Media Playback Volume | 120 | ABE gain, −120 dB + 1 dB/step → 0 dB (`sound/soc/omap/omap-abe-mixer.c:102, 718`) |
| DL1 Voice Playback Volume | 120 | 0 dB (`:108, 724`) |
| SDT DL Volume | 120 | 0 dB (`:131, 787`) |
| Earphone Playback Switch | 1 | TWL6040 earpiece path on |
| Earphone Playback Volume | 15 | TWL6040 EP gain, −24 dB + 2 dB/step, inverted register → +6 dB (`sound/soc/codecs/twl6040.c:624, 853`) |
| Headset Playback Volume | 12 | TWL6040 HS gain, −30 dB + 2 dB/step → −6 dB (`twl6040.c:612, 849`) |
| Capture Preamplifier Volume | 1 | analog mic preamp (unused: analog mics disconnected) (`twl6040.c:594, 839`) |
| Capture Volume | 4 | analog mic gain (unused) (`twl6040.c:600, 841`) |
| DL1 Mixer Multimedia | 1 | ABE DL1 mixer: Multimedia input on |
| DL1 Mixer Voice | 1 | ABE DL1 mixer: Voice input on |
| DL1 Mono Mixer | 0 | off (`omap-abe-mixer.c:811`) |
| DL1 PDM Switch | 1 | DL1 → McPDM → TWL6040 on |
| Sidetone Mixer Playback | 1 | sidetone mixer playback input on |
| DL1 PDM Switch | 1 | (listed twice) |
| BT UL Volume | 120 | 0 dB (`omap-abe-mixer.c:161, 808`) |
| TWL6040 Power Mode | "Low-Power" | machine control (`omap-abe-twl6040.c:402-411`) |
| Headset Power Mode | "High-Performance" | `twl6040.c:856` |
| PLL Selection | "Low-Power" | `twl6040.c:860` |
| MUX_UL10 | "DMic0L" | MM_UL2 (device 1 capture) left ← DMIC0 L (`omap-abe-mixer.c:893, 1108-1126`) |
| MUX_UL11 | "DMic0R" | MM_UL2 right ← DMIC0 R |
| DMIC1 UL Volume | 146 | ABE DMIC1 gain (both channels) → +26 dB (`omap-abe-mixer.c:158, 792`) |
| MUX_VX0 | "DMic0L" | voice uplink (device 2 capture) left ← DMIC0 L (`:897`) |
| MUX_VX1 | "DMic0R" | voice uplink right ← DMIC0 R (`:899`) |
| Voice Capture Mixer Capture | 1 | AUDUL mixer: capture input on (`:641, 917`) |

### 4.3 Output path selection

The stock HAL ignores the AudioPolicy routing value for choosing the transducer; it decides from the USB-connector
headset detection (blob select-output @0x12f8, headset read @0x1250):

1. Read `/sys/devices/platform/usb_mux.0/headset` (opened once at device open, `lseek(0)` then read): `"Mono…"` →
   output type 2, `"Stereo…"` → 3, anything else (`None`, `Unknown`) → 1. The kernel attribute measures the ID-pin
   resistor each read: about 500 kΩ = Stereo, 1 MΩ = Mono (`notle-usb-mux.c:375-406, 543-570`).
2. Output type 1 (no headset): `Earphone Playback Switch` = 1, `Headset Left Playback` = 0,
   `Headset Right Playback` = 0. This is the built-in **bone conduction transducer on the TWL6040 earpiece (EP)
   output**.
3. Output type 2 or 3 (mono or stereo earbud on the USB connector): `Earphone Playback Switch` = 0,
   `Headset Left Playback` = 1, `Headset Right Playback` = 1 (headset HSOL/HSOR via the USB mux).
4. Then always write `"audio"` (6 bytes incl. NUL) to `/sys/devices/platform/usb_mux.0/mux_mode`, and `"1"`
   (2 bytes) to `/sys/devices/platform/usb_mux.0/id_monitor`. On output standby it writes `"0"` to `id_monitor`
   (blob @0x1468). Kernel behaviour: `mux_mode` accepts `floating|usb|tty|audio`; the USB mode wins while USB is
   online or forced, and TTY when TTY priority is set (`notle-usb-mux.c:140-196, 417-447`). `id_monitor` = 1 polls the
   ID line periodically and reports jack changes to ALSA jack and the `h2w` switch (`:403-415, 460-478`).

The HAL does this on every output start (first write after standby) and on every `routing` change on the output.
The Handsfree (HF) outputs and `Handsfree Playback Volume` are never touched by the stock HAL.

### 4.4 Capture gains

At each input start and input standby the HAL writes `DMIC1 UL Volume` (both values) ← dmic gain and
`AUDUL Voice UL Volume` ← voice gain (blob @0x121c). Defaults 146 and 120, overridden at `open_input_stream` by
system properties `audio.dmic1gain` and `audio.voicegain` (integer) (blob @0x1774). `adev_set_parameters`
`voice_volume=<n>` also writes `AUDUL Voice UL Volume` ← n and stores n (blob @0x1bac).

## 5. Streams

### 5.1 Output (primary)

| Item | Value | Source |
|---|---|---|
| Card / device | card 0, **device 0** (Multimedia FE) when the output rate is above 16000 Hz (always, by default) | blob open_output @0x159c, write @0x1c4c |
| Alternate | device 2 (Voice FE) with the 8 kHz or 16 kHz voice configs if property `audio.out.rate` ≤ 16000 | same |
| Reported rate / format / channels | 48000 Hz (unless `audio.out.rate` sets another), PCM 16-bit, stereo | same |
| pcm_config_mm | channels 2, rate 48000, period_size 14784, period_count 2, format S16_LE | blob `.data` `pcm_config_mm` @0x5008 |
| Open flags | `PCM_OUT | PCM_MMAP | PCM_NOIRQ`, written with `pcm_mmap_write` | blob @0x1c4c |
| start_threshold | 2112 frames | same |
| avail_min / fill limit | normal: avail_min 1056, keep ≤ 4224 frames queued; deep-buffer: avail_min 14784, ≤ 29568 queued; the writer sleeps (≥ 5 ms) while more than the limit is queued (uses `pcm_get_htimestamp`) | same |
| Deep-buffer condition | a device flag set and no active input (switches avail_min with `pcm_set_avail_min`) | same |
| `get_latency` | 88 ms normally (4224 frames at 48 kHz); 1232 ms if property `persist.lab.lp_aud` is non-zero | blob @0x1180 |
| `get_buffer_size` | 1056 frames normally, 14784 frames with `persist.lab.lp_aud` (aligned to 16), × frame size | blob @0x1534 |
| Error path | on write error: sleep for the buffer duration, then 50 ms | blob @0x1c4c |
| `out_set_parameters` | key `routing` only: stores the device; a change in the AUX_DIGITAL/DGTL_DOCK bits (0x1400) forces standby; re-runs output selection (4.3) | blob @0x1a9c |
| Starting output while input is open with source VOICE_COMMUNICATION (7) or DEFAULT (1) | puts the input in standby so that it restarts | blob @0x1c4c |

### 5.2 Input

| Item | Value | Source |
|---|---|---|
| Card / device | card 0, **device 2** (Voice FE → VX_UL ← MUX_VX0/1 ← DMIC0 L/R) | blob open_input @0x1774 (device field = 2), read @0x2524 |
| Accepted requests | rate ∈ {8000, 11025, 16000, 22050, 24000, 32000, 44100, 48000}, format PCM 16-bit, 1 or 2 channels; else `-EINVAL` | blob @0x1114 |
| Config choice (by property `audio.in.rate`, else requested rate) | > 16000: `pcm_config_mm_vx` 2 ch, 48000 Hz, period 1056, count 4; 8001–16000: `pcm_config_wb_vx` 2 ch, 16000, period 384, count 2; ≤ 8000: `pcm_config_nb_vx` 2 ch, 8000, period 192, count 2 | blob `.data` @0x5280, @0x5418, @0x534c |
| Open flags | `PCM_IN` (read, non-mmap) | blob @0x2524 |
| Resampling | Android `create_resampler` (speex, quality 4) when requested rate ≠ config rate | blob @0x1774, @0x2524 |
| Start-up muting | the first 50 ms (rate × frame size / 20 bytes) after each start are zero-filled | blob @0x2524 |
| `in_set_parameters` | keys `input_source` (stored, non-zero) and `routing` (stored, non-zero); either change puts the input in standby | blob @0x19cc |
| `get_input_buffer_size` | derived from the selected config (period based, rounded to 16 frames) | blob @0x1fd0 |
| Mic mute | zero-fills read buffers | blob @0x2524 |

### 5.3 Device parameters

| Call | Keys | Behaviour | Source |
|---|---|---|---|
| `adev_set_parameters` | `voice_volume` | writes `AUDUL Voice UL Volume` | blob @0x1bac |
| `adev_get_parameters` | `voice_volume`, `output_type` | returns the stored value / 1, 2, 3 (headset type, re-read from sysfs first) | blob @0x1ebc |

No BT SCO handling exists in the HAL (device 11 is never opened, no SCO mixer path), and no voice call path.

## 6. Processing inside the stock libtinyalsa (effects layer)

The stock `libtinyalsa.so` is not plain tinyalsa: `pcm_open` copies a **204-byte extended `pcm_config`**
(the standard 9 fields plus HAL-filled extras: [9] device mode, [10] input source, [14] output type; card, device
and flags are stored at byte offsets 0x40/0x44/0x48 by the library) and calls `effects_open`; `pcm_write`,
`pcm_mmap_write` and `pcm_read` run effects from `libglassalsa.so` (blob `libtinyalsa.so` `pcm_open` @0x2910,
`effects_open` @0x3840, `effects_write` @0x3dd0, `effects_read` @0x3c60, `effects_close` @0x3b80). The extra exports
`pcm_set_output_type`, `pcm_set_phone_volume`, `pcm_set_calibration` set those extra fields.

Behaviour (observed in those functions):

- Effects are bypassed for card 0 device 11 (BT) and card 1 device 4 (blob @0x32a0), when property
  `audio.effects.off` ≠ 0, and for non-16-bit formats.
- Playback, card 0: if output type ≠ 3 (not a stereo earbud) and the stream is stereo, it is down-mixed to mono
  ((L+R)/2 on both channels) before output.
- Playback, card 0: a "bone conduction transducer" effect (`bct_effects`) runs when `persist.audio.effects.bct` ≠ 0
  (default "3"): gain/limiter and filtering for the transducer. An echo-reference path (`echo_bct`) runs when
  `persist.audio.effects.echo` (default "3") has the playback bit and the extended-config echo flag is set.
- Capture: microphone processing (`mic_effects`) with a feature bit mask from `persist.audio.effects.mic`
  (default "63"): FIR pre-filter (`init_fir4`), wind reduction, noise reduction, comfort noise, and multi-channel
  lateralization for 4-channel capture. A noise-floor stage runs on card 1 with input source 6 when
  `persist.audio.effects.clip` ≠ 0 (default "3").
- FIR coefficients are built in; on startup `libglassalsa` optionally reads overrides from
  `/data/local/tmp/fir001.0`, `fir001.1`, `fir001.2` (text, "%d %d %d %d" lines) (strings at `libglassalsa.so`
  .rodata 0x3de8–0x3e29). The built-in tables are proprietary and are not described here.
- Glasshub pause: with `persist.audio.hammer` (default "1") ≥ 1, when capture starts on card 0 with an input source
  other than 6 (VOICE_RECOGNITION) — or any source if hammer ≥ 2 — write `"1"` to
  `/sys/devices/platform/omap_i2c.4/i2c-4/4-0035/pause_for_audio`; on close write `"0"` (blob @0x3c60, @0x3b80;
  rc `init.omap4430.rc:364-365` gives the file to `media`). Kernel: sets/clears the glasshub `FLAG_PAUSE_FOR_AUDIO`
  pause flag (`drivers/input/misc/glasshub.c:1306-1318`). Purpose (inference): stop the eye-sensor MCU during
  recording.
- Engineering builds only (`ro.build.type` = eng): `audio.effects.inject`, `audio.effects.sniff`,
  `audio.effects.timer` record or inject WAV files under `/data/local/tmp/`.
- Card 1 only: `audio.effects.in.chan.sh` / `audio.effects.out.chan.sh` set a channel expansion shift.

## 7. Recommended open design

- `audio.primary.glass_1`, device API 2.0, legacy `audio_hw_device` (builds against 4.4.4 and 6.0.1 headers), plain
  AOSP `external/tinyalsa` (no extended config). Keep the stock policy's device list but declare the primary output at
  48000 Hz (what the hardware path runs at) to avoid an AudioFlinger resample to 44.1 kHz that the HAL then
  ignores.
- Playback: card 0 device 0, S16_LE stereo 48 kHz. Simple path: non-mmap `pcm_write`, period 1056 frames × 4
  (88 ms) for normal output, optional deep-buffer second output later. Down-mix to mono in the HAL for the bone
  conduction transducer and mono earbuds.
- Routing: apply the table in section 4.2 at open; select EP (transducer) vs HS (earbud) from
  `usb_mux.0/headset`, also react to `routing` (WIRED_HEADSET/HEADPHONE vs EARPIECE/SPEAKER). Write
  `mux_mode` ← "audio" and `id_monitor` ← 1 only when a headset is detected (the stock HAL writes them on every start;
  the kernel ignores "audio" while USB is online). Never touch HF (no speaker fitted per the stock HAL) and never
  change TWL6040 power or voltage settings beyond the stock enum values above (rule 6).
- Capture: card 0 device 2, 2 ch, 48/16/8 kHz configs above, DMIC0 L/R through `MUX_VX0/1`, gains 146/120 with the
  same property overrides; resample in the HAL; zero the first 50 ms; optional mono down-mix for 1-channel requests.
  Use the AOSP pre-processing effects (`libwebrtc_audio_preprocessing` is in the stock image) instead of the
  proprietary `libglassalsa` DSP.
- Write `pause_for_audio` ("1" on capture start, "0" on stop) to keep stock behaviour with the glasshub.
- No in-call, no SCO in the first version (stock has none). BT SCO via device 11 can be studied later.

## 8. Open questions that need a device

1. Confirm that EP is the bone conduction transducer and that `Earphone Playback Volume` 15 (+6 dB) is a safe level
   (Glass sits against the temple; keep the stock value as the maximum).
2. Confirm DMIC0 L/R are the two microphones and their orientation (which is "front/back").
3. Check that playback via plain tinyalsa (no mmap) on device 0 works with the ABE firmware as loaded by this kernel.
4. Sound quality without the stock BCT equalisation; decide whether an open EQ is needed.
5. Behaviour of `mux_mode` "audio" with the USB cable connected (should stay in USB mode).
