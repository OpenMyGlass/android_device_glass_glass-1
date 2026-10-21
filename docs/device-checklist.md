<!-- SPDX-License-Identifier: Apache-2.0 -->
# Device checklist

A full AOSP build has not been run yet. Once this tree builds, check these on a real Glass:

- wpa_supplicant starts and Wi-Fi associates with the empty `wifi/wpa_supplicant_overlay.conf`.
- `/data/misc/audio` is created 0664 per the facts. Without an execute bit it cannot be entered. Check audio HAL access and whether 0770 is needed.
- The `/storage/sdcard0` symlink created in `on init` is hidden by Android 6.0's `/storage` bind mount. Check apps' storage paths.
- Unverified facts in `docs/hardware-facts.md`: the mode of `/dev/ttyO1` and of `/dev/glasshub` (the stock files set the owner only), the owner of `/dev/si114x`, and the mode of `glu_outlier` (the stock chmod names `glu_outliner`).
- `vendor/bin/pvrsrvinit` (blob) versus OMAP4-AOSP's own `pvrsrvinit` service and the blob `gralloc.omap4430.so`: check which one runs and whether the GPU comes up.
- GPU module: `BoardConfig.mk` builds `pvrsrvkm_sgx540_120.ko` from DDK `1.9@2166536`, the build of the XE24 user-space libraries, with glass-kernel's options (glass-kernel `docs/gpu-module.md` compares it with the stock module). Check `cat /proc/modules` lists `pvrsrvkm_sgx540_120`, `dmesg | grep SGXInit` shows no `(FAIL)` line after `pvrsrvinit` (the module logs `(FAIL) SGXInit: Mismatch in client-side and KM driver build options` or `(FAIL) SGXInit: Incompatible driver DDK revision` on a mismatch), and SurfaceFlinger starts. The module only loads into the kernel it was built with.

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
- Requesting any light other than the backlight fails quietly: the HAL returns `-EINVAL` and `LightsService` ignores that light, with no crash in logcat. Not run.

## GPS (`docs/hals.md`, No GPS HAL)

- GPS stays off: after a cold boot, after a reboot straight from stock firmware, and after suspend/resume, `cat /sys/class/gps/gps_elton/{onoff,reset,awake}` reads 0, 1, 0 (hibernate), and battery current with the screen off matches a build without any GPS use. Not run.
- No `gps_elton` writes: `dmesg` has no `Setting on_off` or `Setting reset` lines from the driver, `logcat` shows no GPS HAL loading, and nothing opens `/dev/ttyO3` (`ls -l /proc/*/fd 2>/dev/null | grep ttyO3` is empty). Not run.

## Sensors HAL (`docs/hals.md`, `docs/specs/sensors.md`)

All not run.

- On both the 4.4 and the 6.0 build, `sensors.glass_1` loads: `logcat -s sensors.glass_1` shows no "no IIO device named mpu9150" or "no input device named ltr506_als", and `dumpsys sensorservice` lists the six sensors plus the framework's virtual orientation, rotation vector, game rotation vector, gravity and linear acceleration. Not run.
- Raw mode (spec §9.1): with `dmp_on` 0, enabling accelerometer, gyroscope and magnetometer together gives 32-byte records with format byte 0x0E, and logcat shows no "record with data format" warnings. Repeat with each sensor alone (16-byte records). Not run.
- Compass alone: enabling only the magnetometer delivers events at its rate (the HAL keeps the accel engine on for that). If none arrive, the data-ready interrupt needs something else. Not run.
- Axes (spec §9.2): Glass upright and still gives about +9.8 m/s² on the axis Android expects for "up" after the sysfs matrices (body = M × chip); a nod and a head turn give gyro signs that match the right-hand rule; the compass heading in an app matches a phone's. Not run.
- Matrices: `cat /sys/bus/iio/devices/iio:device0/{gyro,accl,compass}_matrix` reads `0,1,0,0,0,1,1,0,0`, the same, and `1,0,0,0,0,-1,0,1,0`; logcat has no "not a signed permutation" error. Not run.
- Clock (spec §9.3): event timestamps track `SystemClock.elapsedRealtimeNanos()` within a few milliseconds, before and after a suspend/resume, for motion and light events; logcat "robust clock offset moved" lines show how far the 32 kHz counter drifts from `CLOCK_BOOTTIME`. On 4.4, check whether any app compares sensor stamps with `System.nanoTime()`. Not run.
- Rates: at SENSOR_DELAY_FASTEST the gyro and accel deliver about 200 Hz and the magnetometer about 100 Hz, with no zero-triple magnetometer events; at SENSOR_DELAY_NORMAL about 5 Hz. Not run.
- Magnetometer calibration: after waving the head through nods and turns, logcat shows "hard-iron bias ... uT, field ... uT" with a field of 25-65 µT, `/data/misc/sensors/glass_magcal.txt` appears (directory 0770 system system), and after a reboot "loaded hard-iron bias" appears. Not run.
- Gyro bias: Glass left on a table for a few seconds, with the gyro at 10 Hz or more (SENSOR_DELAY_UI or faster), gives GYROSCOPE events near 0 rad/s and GYROSCOPE_UNCALIBRATED events whose bias field equals the offset. At SENSOR_DELAY_NORMAL the bias is never estimated. Not run.
- Light (spec §9.4): covering and lighting the sensor changes LIGHT events; compare the values with a phone lux meter to decide on a lux factor. Enabling the light sensor gives one event at once (from `als_adc`) even when the light does not change; check that its value is on the same scale as the following events, in particular on part ID 0x90, where the driver reports channel 1 in events. Not run.
- Input numbers (spec §9.5): `cat /sys/class/input/input*/name` shows which inputN are `ltr506_als` and `ltr506_ps` on the new build; the HAL finds them by name, but the `input2`/`input3` chowns in `init.omap4430.rc` follow stock and may point at other devices. Not run.
- Power (spec §9.6): with every sensor off, `power_state` reads 0 and the battery current drops compared with a sensor running; `als_enable` reads 0 with the light sensor off. Not run.
- IIO device number: `/sys/bus/iio/devices/iio:device0/name` is `mpu9150`. If the device has another number, the `iio:device0` paths in `ueventd.omap4430.rc` and `init.omap4430.rc` need it. Not run.
- Batching requests: an app registering with a max report latency still gets events (SensorManager retries in continuous mode after `batch` returns `-EINVAL`). Not run.

## Audio HAL (`docs/hals.md`, `docs/specs/audio.md` section 8)

All not run.

- On both the 4.4 and the 6.0 build, `audio.primary.glass_1` loads: `logcat -s audio.primary.glass_1` shows "opened: card 0, playback device 0, capture device 2" and no "missing mixer control" line. Not run.
- `tinymix` on card 0 shows the values of spec 4.2 after boot, among them `Earphone Playback Volume` 12 (0 dB, 6 dB below stock), `Headset Playback Volume` 12, and the handsfree controls untouched. Not run.
- With nothing plugged in, a notification sound plays on the bone conduction transducer (the TWL6040 EP output), at a level no louder than stock XE24, peaks included (spec 8, question 1). The default `Earphone Playback Volume` is 12 (0 dB), 6 dB below the stock 15, because the stock limiter stage is missing. Measure this build against stock at the same volume step; raising the default to 15 (the hard cap) is the owner's decision after that. Not run.
- Playback through plain tinyalsa (`pcm_write`, no mmap) on device 0 works with the ABE firmware as this kernel loads it, without underruns in logcat (spec 8, question 3). Not run.
- Media playback sounds acceptable on the transducer without the stock BCT equalisation; note whether an open EQ is needed (spec 8, question 4). Not run.
- With a stereo earbud on the USB port, `cat /sys/devices/platform/usb_mux.0/headset` reads `Stereo`, playback is stereo on the earbud and silent on the transducer; with a mono earbud it reads `Mono` and plays a mono down-mix. Not run.
- Plugging and unplugging an earbud during playback moves the sound within about a second (`id_monitor` polling, `h2w` switch, routing change). Not run.
- After playing on an earbud and then removing it, `cat /sys/devices/platform/usb_mux.0/mux_mode` stays `audio` (the HAL never writes it back; stock does the same). Check that the transducer still plays, and that plugging the USB cable in afterwards switches the mux to `usb` and the device enumerates (spec 8, question 5). Not run.
- With a voice-recognition input open (the "ok glass" hotword), a second app that starts recording gets no stream: the HAL has one input, so the second `open_input_stream` fails with `-ENOSYS`. Check how each Android version handles it (error to the app, or the hotword input closed first). Not run.
- Recording (Camera video, a recorder app at 44100 and 48000 Hz, mono and stereo) captures both DMIC0 microphones at a sensible level with no click at the start; check which channel is which and whether it is front/back (spec 8, question 2). Not run.
- 8000 and 16000 Hz capture (VoIP-type apps) works through the voice configs and the resampler. Not run.
- While recording with a source other than voice recognition, `cat /sys/devices/platform/omap_i2c.4/i2c-4/4-0035/pause_for_audio` reads 1, and 0 after the recording stops. The "ok glass" hotword (voice recognition) leaves it at 0. Not run.
- Bluetooth A2DP headphones play through `audio.a2dp.default`, and screen casting with `audio.r_submix.default` carries sound. Not run.
