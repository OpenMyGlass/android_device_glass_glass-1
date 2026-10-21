<!-- SPDX-License-Identifier: Apache-2.0 -->
# Device checklist

A full AOSP build has not been run yet. Once this tree builds, check these on a real Glass:

- wpa_supplicant starts and Wi-Fi associates with the empty `wifi/wpa_supplicant_overlay.conf`.
- `/data/misc/audio` is created 0664 per the facts. Without an execute bit it cannot be entered. Check audio HAL access and whether 0770 is needed.
- The `/storage/sdcard0` symlink created in `on init` is hidden by Android 6.0's `/storage` bind mount. Check apps' storage paths.
- Unverified facts in `docs/hardware-facts.md`: the mode of `/dev/ttyO1` and of `/dev/glasshub` (the stock files set the owner only), the owner of `/dev/si114x`, and the mode of `glu_outlier` (the stock chmod names `glu_outliner`).
- `vendor/bin/pvrsrvinit` (blob) versus OMAP4-AOSP's own `pvrsrvinit` service and the blob `gralloc.omap4430.so`: check which one runs and whether the GPU comes up.
- GPU module: `BoardConfig.mk` builds `pvrsrvkm_sgx540_120.ko` from DDK `1.9@2166536`, the build of the XE24 user-space libraries, with glass-kernel's options (glass-kernel `docs/gpu-module.md` compares it with the stock module). Check `cat /proc/modules` lists `pvrsrvkm_sgx540_120`, `dmesg | grep SGXInit` shows no `(FAIL)` line after `pvrsrvinit` (the module logs `(FAIL) SGXInit: Mismatch in client-side and KM driver build options` or `(FAIL) SGXInit: Incompatible driver DDK revision` on a mismatch), and SurfaceFlinger starts. The module only loads into the kernel it was built with.
