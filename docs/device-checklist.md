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
- No light other than the backlight is requested without an error in logcat: `LightsService` handles the `-EINVAL`. Not run.
