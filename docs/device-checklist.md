<!-- SPDX-License-Identifier: Apache-2.0 -->
# Device checklist

A full AOSP build has not been run yet. Once this tree builds, check these on a real Glass:

- wpa_supplicant starts and Wi-Fi associates with the empty `wifi/wpa_supplicant_overlay.conf`.
- `/data/misc/audio` is created 0664 per the facts. Without an execute bit it cannot be entered. Check audio HAL access and whether 0770 is needed.
- The `/storage/sdcard0` symlink created in `on init` is hidden by Android 6.0's `/storage` bind mount. Check apps' storage paths.
- Unverified facts in `docs/hardware-facts.md`: the mode of `/dev/ttyO1` and of `/dev/glasshub` (the stock files set the owner only), the owner of `/dev/si114x`, and the mode of `glu_outlier` (the stock chmod names `glu_outliner`).
- `vendor/bin/pvrsrvinit` (blob) versus OMAP4-AOSP's own `pvrsrvinit` service and the blob `gralloc.omap4430.so`: check which one runs and whether the GPU comes up.
- PowerVR DDK mismatch: the XE24 user-space and stock module are DDK `1.9@2166536`, while OMAP4-AOSP `pvr-source` (which BoardConfig.mk builds) is `1.9@2291151`. The built module may be refused by the blob libraries (`PVRSRV_ERROR_DDK_VERSION_MISMATCH`). This is phase R2 in glass-rom's roadmap.

## Power and lights HALs (`docs/hals.md`)

All not run.

- `power.glass_1` loads: `logcat -s power.glass_1` shows "interactive governor set to the stock XE24 values" and no `cannot write` errors. Not run.
- After boot, `cat /sys/devices/system/cpu/cpufreq/interactive/{timer_rate,min_sample_time,go_hispeed_load,target_loads,above_hispeed_delay,boostpulse_duration}` reads 20000, 80000, 99, 90, 20000, 80000, and `scaling_governor` is `interactive`. Not run.
- Touchpad input sends `POWER_HINT_INTERACTION`: with `adb shell cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq` in a loop, a swipe raises the frequency, and no `cannot open .../boostpulse` error appears. Not run.
- Screen off and on: `scaling_max_freq` stays 1008000 (the cap is disabled), and the device still suspends and resumes. Not run.
- `lights.glass_1` loads, and the settings brightness slider changes `/sys/devices/platform/omapdss/manager2/panel-notle-dpi/brightness` (0-255) and the visible brightness. Not run.
- Screen off writes 0 and turns the backlight off. Screen on restores the previous value. Not run.
- Brightness from 223 to 255 looks the same with the default `brightness_limit`; decide whether to map the slider to 0-223. Not run.
- No light other than the backlight is requested without an error in logcat: `LightsService` handles the `-EINVAL`. Not run.
