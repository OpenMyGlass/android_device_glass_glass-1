<!-- SPDX-License-Identifier: Apache-2.0 -->
# Android 4.4.4 (branch `android-4.4.4`)

This branch is the device tree for Track A of the glass-rom roadmap: Android 4.4.4, ART only
([plan](https://github.com/OpenMyGlass/glass-rom/blob/main/docs/plans/2026-10-21-android-lean-kitkat-art.md)).
It starts from `android-6.0.1_r31` at `bb727d5`. The default branch stays Android 6.0.1 (Track B).
The tree is built from [OpenMyGlass/manifest](https://github.com/OpenMyGlass/manifest), which pins
every project, this branch included. The base is AOSP `android-4.4.4_r2.0.1`, plus omap4-aosp's
`aosp-4.4` OMAP4 projects and android-security's patched `aosp-4.4` projects; why is in the
manifest's `docs/kitkat-base.md`.

## What differs from the 6.0.1 branch

- **Kernel and SGX module.** The 6.0.1 branch builds the kernel inline (`TARGET_KERNEL_SOURCE`)
  and the SGX module through `SGX_MODULES`. This branch builds neither. It installs the
  `zImage` and `pvrsrvkm_sgx540_120.ko` that glass-kernel builds (variant `tuned-stable`,
  DDK `1.9@2166536`), from `GLASS_KERNEL_DIR` (default `vendor/glass/kernel`):
  - `zImage` goes to `kernel`, so it ends up in `boot.img`;
  - the module goes to `/system/lib/modules`.

  glass-kernel applies its patches and config fragment, which an inline build would not. The
  module only loads into the kernel it was built with, so both must come from the same
  glass-kernel build. The manifest's `tools/fetch-kernel.sh` runs that build at a pinned commit.
  Without `GLASS_KERNEL_DIR/zImage`, product configuration stops with an error. The exception is
  `GLASS_NO_KERNEL=true`, which also sets `TARGET_NO_KERNEL`: the build then makes a system image
  only, with no boot or recovery image. `tools/fetch-pvr.sh`, `tools/pvr.env` and
  `tests/test_pvr.py` are gone from this branch for the same reason.

  omap4-aosp's build fork still runs `core/tasks/kernel.mk`, which builds a kernel from
  `TARGET_KERNEL_SOURCE` (default `kernel/glass/glass-1`) when that directory exists.
  `BoardConfig.mk` points it at a path that never exists, so the task only prints its "prebuilt
  kernel" deprecation warnings.

  `tuned-stable` is 3.4.113 plus glass-kernel's tuning (interactive governor values, touchpad
  input boost). OPPs, voltages and thermal limits are unchanged (glass-kernel
  `docs/tuning.md`), but it has not run on hardware either. For the first boot test, building
  with `GLASS_KERNEL_VARIANT=stock` in the manifest's `tools/kernel.env` separates kernel problems
  from Android ones. That is a decision for whoever runs the first boot.
- **pvrsrvinit link order.** `Android.mk` makes the link of hardware/ti/omap4's `pvrsrvinit`
  wait for the two PowerVR libraries it links from `/system/vendor/lib`. Only its phony target
  depended on them, so a parallel build could link first.
- **bltsville.** With `TI_OMAP_USE_BLTSVILLE := true`, omap4-aosp's `hardware/ti/omap4`
  installs these into `/system/vendor/lib`:
  - `libbltsville_ticpu.2.1.0.0.so` and its licence file, shipped as a TI binary in that
    repository. Its SHA-256 `3d02c453…7279` equals XE24's, and the licence file's
    `04a18143…297c` too;
  - `libbltsville_gc2d.2.2.2.0.so`, built from source (`bltsville/gcbv`, `VER_FILEVERSION_STR`
    `2.2.2.0`).

  So the three `libbltsville_*` lines are dropped from `proprietary-files.txt`. Extracting them
  as well would give two rules for the same output file.
- **Dex pre-optimisation.** `WITH_DEXPREOPT_PIC` is a 5.0+ variable and is dropped. Whether
  to pre-optimise at all is the ART-only change (its own pull request).

## 4.4 versus 6.0, for the files shared with the 6.0.1 branch

- **init.** `rootdir/*.rc` uses only keywords that 4.4's init knows (`system/core/init/keywords.h`
  at `android-4.4.4_r2.0.1`); `tests/test_kitkat.py` checks this against that list. 6.0-only
  keywords such as `writepid`, `bootchart_init`, `verity_load_state` and `init_user0` are not used.
- **fstab.** Every fs_mgr flag used (`wait`, `check`, `zramsize=`) is in 4.4's
  `system/core/fs_mgr/fs_mgr.c`. No 5.0+ flag (`forceencrypt`, `formattable`) is used.
- **SELinux.** 4.4's init enforces unless `ro.boot.selinux` is `permissive` or `disabled`
  (`selinux_is_enforcing` in `init.c`), and it disables SELinux when the kernel has none.
  `androidboot.selinux=permissive` stays on the command line, so both branches behave alike.
  omap4-aosp's `BoardConfigCommon.mk` adds its `hardware/ti/omap4/sepolicy` to
  `BOARD_SEPOLICY_DIRS`.
- **EGL.** 4.4's `frameworks/native/opengl/libs/EGL/Loader.cpp` no longer reads `egl.cfg`. It
  looks in `/vendor/lib/egl`, then `/system/lib/egl`, for `libEGL_*.so` and skips
  `libGLES_android.so`. So `vendor/lib/egl/libEGL_POWERVR_SGX540_120.so` is found without a
  config file, as on 6.0.
- **Storage.** 4.4 needs the emulated sdcard set up by init (the `sdcard` service and the
  `EXTERNAL_STORAGE` and `EMULATED_STORAGE_*` variables), which 6.0's vold does itself. This is
  not in this branch yet; see "Open items".
- **HALs.** `power/` and `lights/` already build for API 19 (`docs/hals.md`; CI job `hals`, 4.4.4
  target).

## Open items

Each is its own pull request into this branch:
1. ART only: `PRODUCT_RUNTIMES`, `DISABLE_DEXPREOPT`, and the dex2oat filter properties.
2. The 4.4 emulated sdcard in `rootdir/init.omap4430.rc` and `docs/hardware-facts.md`.
3. The lean items of the plan's Task 4 that are one property or one makefile line.

## Device checks

All not run (no hardware). Use `fastboot boot` only; never flash `bootloader`, `xloader` or
`fpga`.
- `boot.img` from this branch boots with `fastboot boot`, and `uname -r` shows the glass-kernel
  `tuned-stable` version (3.4.113).
- `/system/lib/modules/pvrsrvkm_sgx540_120.ko` loads (`cat /proc/modules`), and
  `dmesg | grep SGXInit` has no `(FAIL)` line after `pvrsrvinit`.
- `ls -l /system/vendor/lib/libbltsville*` shows the files and symlinks from `hardware/ti/omap4`,
  and nothing in `logcat` fails to load them.
- SurfaceFlinger starts on the PowerVR driver: `logcat | grep -i egl` names
  `/vendor/lib/egl/libEGL_POWERVR_SGX540_120.so`.
