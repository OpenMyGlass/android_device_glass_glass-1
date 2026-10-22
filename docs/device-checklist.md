<!-- SPDX-License-Identifier: Apache-2.0 -->
# Device checklist

No image from this branch has been tested on a device. Check these on a real Glass, booting with `fastboot boot` only (4.4.4-specific checks are also in `docs/kitkat.md`):

- wpa_supplicant starts and Wi-Fi associates with the empty `wifi/wpa_supplicant_overlay.conf`.
- `/data/misc/audio` is created 0664 per the facts. Without an execute bit it cannot be entered. Check audio HAL access and whether 0770 is needed.
- Storage: on this branch init sets up the 4.4 emulated sdcard (`docs/kitkat.md`, Storage). Check apps' storage paths. Not run.
- Unverified facts in `docs/hardware-facts.md`: the mode of `/dev/ttyO1` and of `/dev/glasshub` (the stock files set the owner only), the owner of `/dev/si114x`, and the mode of `glu_outlier` (the stock chmod names `glu_outliner`).
- `vendor/bin/pvrsrvinit` (blob) versus OMAP4-AOSP's own `pvrsrvinit` service and the blob `gralloc.omap4430.so`: check which one runs and whether the GPU comes up.
- GPU module (4.4.4 branch): `device.mk` installs the `pvrsrvkm_sgx540_120.ko` that glass-kernel builds from DDK `1.9@2166536` with the kernel it loads into (glass-kernel `docs/gpu-module.md` compares it with the stock module). Check `cat /proc/modules` lists `pvrsrvkm_sgx540_120`, `dmesg | grep SGXInit` shows no `(FAIL)` line after `pvrsrvinit` (the module logs `(FAIL) SGXInit: Mismatch in client-side and KM driver build options` or `(FAIL) SGXInit: Incompatible driver DDK revision` on a mismatch), and SurfaceFlinger starts. Not run.

## Power and lights HALs (`docs/hals.md`)

All not run.

- On both the 4.4 and the 6.0 build, `power.glass_1` loads (not `power.omap4`, which is also installed): `logcat -s power.glass_1` shows "interactive governor set to the stock XE24 values" and no `cannot write` errors. Not run.
- After boot, `cat /sys/devices/system/cpu/cpufreq/interactive/{timer_rate,min_sample_time,go_hispeed_load,target_loads,above_hispeed_delay,boostpulse_duration}` reads 20000, 80000, 99, 90, 20000, 80000, and `scaling_governor` is `interactive`. Not run.
- With the interaction boost disabled (the default), a touchpad swipe does not write `boostpulse`: `scaling_cur_freq` follows load only, as on stock. Not run.
- Only if the owner enables `GLASS_BOOSTPULSE_ENABLED`: a swipe raises `scaling_cur_freq` for about 80 ms, no `cannot open .../boostpulse` error appears, and `cpufreq/stats/time_in_state` at 1008000 grows compared with the disabled build. Not run.
- Screen off and on: `scaling_max_freq` stays 1008000 (the cap is disabled), and the device still suspends and resumes. Not run.
- On both builds, `lights.glass_1` loads, and the settings brightness slider changes `/sys/devices/platform/omapdss/manager2/panel-notle-dpi/brightness` (0-255) and the visible brightness. Not run.
- Screen off writes 0 and turns the backlight off. Screen on restores the previous value. Not run.
- With the default `brightness_limit`, writing 223 and then 255 to `brightness` gives the same visible brightness, as the driver code predicts. If so, the owner decides whether the slider should map to 0-223. Not run.
- No light other than the backlight is requested without an error in logcat: `LightsService` handles the `-EINVAL`. Not run.

## GPS (`docs/hals.md`, No GPS HAL)

- GPS stays off: after a cold boot, after a reboot straight from stock firmware, and after suspend/resume, `cat /sys/class/gps/gps_elton/{onoff,reset,awake}` reads 0, 1, 0 (hibernate), and battery current with the screen off matches a build without any GPS use. Not run.
- No `gps_elton` writes: `dmesg` has no `Setting on_off` or `Setting reset` lines from the driver, `logcat` shows no GPS HAL loading, and nothing opens `/dev/ttyO3` (`ls -l /proc/*/fd 2>/dev/null | grep ttyO3` is empty). Not run.
- `adb shell pm list features` has `android.hardware.location` and `android.hardware.location.network` but not `android.hardware.location.gps`, and `/data/gps` does not exist. Not run.

## Media (`docs/media-facts.md`)

- Media codecs: `MediaCodecList` (from a test app, or `logcat` of mediaserver at boot) lists, among decoders, `OMX.TI.DUCATI1.VIDEO.DECODER` first and then the `OMX.google.*` decoders, and among encoders the three TI encoders and then the `OMX.google.*` encoders (the order of `media_codecs.xml`). An H.264 720p file plays on `OMX.TI.DUCATI1.VIDEO.DECODER`, and a VP8 file plays on `OMX.google.vp8.decoder`. Not run.
- Media profiles kept from stock with odd values: recording with the qcif, cif or 480p profile (AAC at 12.2 kbit/s, 8 kHz, stereo) and an AMR recording (declared two channels) work, or fail in a way that says which value to change. Not run.
