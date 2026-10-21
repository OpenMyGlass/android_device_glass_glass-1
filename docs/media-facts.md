<!-- SPDX-License-Identifier: Apache-2.0 -->
# Glass Explorer Edition media facts

`media_codecs.xml` and `media_profiles.xml` carry these facts. `tests/test_media.py` checks both
files against the tables below, in order. The XML files are written from AOSP's
`device/samsung/tuna` files (Galaxy Nexus, OMAP4 with the same Ducati codecs, Apache-2.0) at commit
`8c9a241892d74ecb09c1c9a5eef0848c354f3fb6` (tag android-4.3.1_r1); the values are Glass's.

Sources:

- `XE24 system`: `/system/etc/media_codecs.xml` and `/system/etc/media_profiles.xml` of the XE24
  factory image (Android 4.4), read as facts. Nothing is copied from them.
- `AOSP 6.0`: platform/frameworks/av at android-6.0.1_r31 (commit
  `1919e501387a5f470e7370c3de0f15aa1305cc7d`), `media/libstagefright/data/`.
- `AOSP 4.4`: platform/frameworks/av at android-4.4.4_r2 (commit
  `66809dc49bec6cb6345c74d48d319c2a63c17ac6`), `media/libstagefright/omx/SoftOMXPlugin.cpp`
  and `MediaCodecList.cpp`.
- `OMAP4-AOSP`: hardware/ti/omap4 branch `stable` (commit
  `57eec4a73e2939d86d0782cb476c8b4a046422b7`), `domx/omx_core/src/OMX_Core.c`, the roles each
  Ducati component registers.

Lists inside a cell are comma-separated, in file order.

## Hardware codecs

All of them take both quirks: `requires-allocate-on-input-ports` and
`requires-allocate-on-output-ports`. Glass ships no `.secure` decoder and no `VC1E` encoder in its
list, although OMAP4-AOSP builds both.

| Kind | Name | Types | Quirks | Source |
|---|---|---|---|---|
| decoder | OMX.TI.DUCATI1.VIDEO.DECODER | video/mp4v-es, video/3gpp, video/avc, video/mpeg2, video/wmv9 | requires-allocate-on-input-ports, requires-allocate-on-output-ports | XE24 system; OMAP4-AOSP (roles mpeg4, avc, h263, wmv, mpeg2) |
| encoder | OMX.TI.DUCATI1.VIDEO.MPEG4E | video/mp4v-es, video/3gpp | requires-allocate-on-input-ports, requires-allocate-on-output-ports | XE24 system; OMAP4-AOSP (roles mpeg4, h263) |
| encoder | OMX.TI.DUCATI1.VIDEO.H264E | video/avc | requires-allocate-on-input-ports, requires-allocate-on-output-ports | XE24 system; OMAP4-AOSP (role avc) |
| encoder | OMX.TI.DUCATI1.VIDEO.H264SVCE | video/svc | requires-allocate-on-input-ports, requires-allocate-on-output-ports | XE24 system; OMAP4-AOSP (role svc) |

`video/wmv9` and `video/svc` are not MIME types any AOSP extractor or recorder produces, so on
AOSP they are listed but never picked. They stay because the stock list has them.

## Software codec lists (6.0)

On 6.0 the software (`OMX.google.*`) codecs come from the release's own lists, which `device.mk`
copies from `frameworks/av/media/libstagefright/data/` to `/system/etc/`. The TI video codecs come
before the software video codecs (the audio list, which has no TI codec, comes first), so they are
preferred.

| Include | Position | Source |
|---|---|---|
| media_codecs_google_audio.xml | before Decoders | AOSP 6.0 |
| media_codecs_google_video.xml | after Encoders | AOSP 6.0 |

`media_codecs_google_telephony.xml` (the GSM decoder) is left out: Glass has no telephony and the
stock list has no GSM decoder. `media_codecs_google_video_le.xml` (the low-memory variant) is not
used. No `media_codecs_performance.xml` is shipped; CTS on 6.0 would ask for one, measured on a
device.

## XE24 software codecs (for a 4.4 branch)

The stock 4.4 list names these after the hardware codecs. A 4.4 branch lists them itself, since 4.4
has neither the `Include` element nor the `media_codecs_google_*.xml` files (AOSP 4.4
`MediaCodecList.cpp` reads only `/etc/media_codecs.xml`).

| Kind | Name | Type | Source |
|---|---|---|---|
| decoder | OMX.google.mp3.decoder | audio/mpeg | XE24 system |
| decoder | OMX.google.amrnb.decoder | audio/3gpp | XE24 system |
| decoder | OMX.google.amrwb.decoder | audio/amr-wb | XE24 system |
| decoder | OMX.google.aac.decoder | audio/mp4a-latm | XE24 system |
| decoder | OMX.google.g711.alaw.decoder | audio/g711-alaw | XE24 system |
| decoder | OMX.google.g711.mlaw.decoder | audio/g711-mlaw | XE24 system |
| decoder | OMX.google.vorbis.decoder | audio/vorbis | XE24 system |
| decoder | OMX.google.mpeg4.decoder | video/mp4v-es | XE24 system |
| decoder | OMX.google.h263.decoder | video/3gpp | XE24 system |
| decoder | OMX.google.h264.decoder | video/avc | XE24 system |
| decoder | OMX.google.vpx.decoder | video/x-vnd.on2.vp8 | XE24 system (AOSP 4.4 names it OMX.google.vp8.decoder) |
| encoder | OMX.google.amrnb.encoder | audio/3gpp | XE24 system |
| encoder | OMX.google.amrwb.encoder | audio/amr-wb | XE24 system |
| encoder | OMX.google.aac.encoder | audio/mp4a-latm | XE24 system |
| encoder | OMX.google.flac.encoder | audio/flac | XE24 system |

`OMX.google.vpx.decoder` is the pre-4.4 name of the VP8 decoder. AOSP 4.4's `SoftOMXPlugin.cpp`
registers only `OMX.google.vp8.decoder`, so a 4.4 branch should use that name, or VP8 playback
finds no decoder.

## Camcorder profiles

One camera, `cameraId="0"`. Every profile is `fileFormat="mp4"`, `duration="30"`, video codec
h264 and audio codec aac.

| Quality | Video bit rate | Width | Height | Frame rate | Audio bit rate | Sample rate | Channels | Source |
|---|---|---|---|---|---|---|---|---|
| qcif | 384000 | 176 | 144 | 30 | 12200 | 8000 | 2 | XE24 system |
| cif | 2000000 | 352 | 288 | 30 | 12200 | 8000 | 2 | XE24 system |
| 480p | 8000000 | 720 | 480 | 30 | 12200 | 8000 | 2 | XE24 system |
| 720p | 12000000 | 1280 | 720 | 30 | 96000 | 48000 | 2 | XE24 system |
| timelapseqcif | 384000 | 176 | 144 | 30 | 12200 | 8000 | 2 | XE24 system |
| timelapsecif | 2000000 | 352 | 288 | 30 | 12200 | 8000 | 2 | XE24 system |
| timelapse480p | 8000000 | 720 | 480 | 30 | 12200 | 8000 | 2 | XE24 system |
| timelapse720p | 12000000 | 1280 | 720 | 30 | 96000 | 48000 | 2 | XE24 system |

There is no 1080p profile (tuna has one). The framework derives `high` and `low` from these
(720p and qcif). The audio of the timelapse profiles is ignored.

## Image settings

| Element | Value | Source |
|---|---|---|
| ImageEncoding quality | 95 | XE24 system |
| ImageEncoding quality | 80 | XE24 system |
| ImageEncoding quality | 70 | XE24 system |
| ImageDecoding memCap | 20000000 | XE24 system |

## Media settings

The elements after `CamcorderProfiles`, in order. Attributes are listed as `name=value`.

| Element | Attributes | Source |
|---|---|---|
| EncoderOutputFileFormat | name=3gp | XE24 system |
| EncoderOutputFileFormat | name=mp4 | XE24 system |
| VideoEncoderCap | name=h264 enabled=true minBitRate=64000 maxBitRate=20000000 minFrameWidth=96 maxFrameWidth=1920 minFrameHeight=96 maxFrameHeight=1920 minFrameRate=15 maxFrameRate=30 | XE24 system |
| VideoEncoderCap | name=h263 enabled=true minBitRate=64000 maxBitRate=20000000 minFrameWidth=96 maxFrameWidth=1920 minFrameHeight=96 maxFrameHeight=1920 minFrameRate=15 maxFrameRate=30 | XE24 system |
| VideoEncoderCap | name=m4v enabled=true minBitRate=64000 maxBitRate=20000000 minFrameWidth=96 maxFrameWidth=1920 minFrameHeight=96 maxFrameHeight=1920 minFrameRate=15 maxFrameRate=30 | XE24 system |
| AudioEncoderCap | name=aac enabled=true minBitRate=8000 maxBitRate=96000 minSampleRate=8000 maxSampleRate=48000 minChannels=2 maxChannels=2 | XE24 system |
| AudioEncoderCap | name=amrwb enabled=true minBitRate=6600 maxBitRate=23050 minSampleRate=16000 maxSampleRate=16000 minChannels=2 maxChannels=2 | XE24 system |
| AudioEncoderCap | name=amrnb enabled=true minBitRate=5525 maxBitRate=12200 minSampleRate=8000 maxSampleRate=8000 minChannels=2 maxChannels=2 | XE24 system |
| VideoDecoderCap | name=wmv enabled=false | XE24 system |
| AudioDecoderCap | name=wma enabled=false | XE24 system |
| VideoEditorCap | maxInputFrameWidth=1920 maxInputFrameHeight=1080 maxOutputFrameWidth=1920 maxOutputFrameHeight=1080 maxPrefetchYUVFrames=10 | XE24 system |
| ExportVideoProfile | name=h264 profile=1 level=4096 | XE24 system |
| ExportVideoProfile | name=h263 profile=1 level=1 | XE24 system |
| ExportVideoProfile | name=m4v profile=1 level=128 | XE24 system |

Odd values kept as stock has them, to check on a device (`docs/device-checklist.md`): the AMR
encoders declare two channels although AMR is mono, the qcif, cif and 480p profiles record AAC at
12.2 kbit/s and 8 kHz in stereo, and `maxFrameHeight` is 1920.

## 4.4 and 6.0

- `media_profiles.xml`: the same file works on both. 4.4 and 6.0 parse the same elements.
- `media_codecs.xml`: this tree's file is for 6.0 (`Include` arrived in 5.0). A 4.4 branch writes
  the hardware codecs above followed by the XE24 software codec table, with
  `OMX.google.vp8.decoder` for VP8, and drops the two `media_codecs_google_*.xml` copies from
  `device.mk`.
