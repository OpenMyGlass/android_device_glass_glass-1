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
- **Dex pre-optimisation.** `WITH_DEXPREOPT_PIC` is a 5.0+ variable and is dropped.
- **ART only.** See the next section.

## ART only

- **Product.** `full_glass_1.mk` sets `PRODUCT_RUNTIMES := runtime_libart_default` after it
  inherits `aosp_base.mk`.
  - `core_minimal.mk` lists `runtime_libdvm_default` and `runtime_libart`.
  - `inherit-product` (`build/core/product.mk`) stores the inherited product as a tag in each
    variable, so a later `:=` replaces the inherited runtimes.
  - `build/core/product_config.mk` then includes only `runtime_libart_default.mk`. It sets
    `persist.sys.dalvik.vm.lib=libart.so` and adds `core-libart`, `libart`, `dex2oat` and
    `oatdump` (through `runtime_libart.mk`).
  - `libdvm`, `dexopt` and `core.jar` are not installed.
- **No pre-optimisation.** `DISABLE_DEXPREOPT := true`. 4.4's `core/dex_preopt.mk` only runs the
  host `dexopt`, which writes Dalvik `.odex` files, so `WITH_DEXPREOPT` would only add files ART
  ignores. Every APK and jar keeps `classes.dex`. `boot.oat` and the apps are compiled on the
  device at first boot (`/data/dalvik-cache`).
- **Fallbacks.** The forks pinned by the manifest patch the two places that fall back to Dalvik:
  - `libnativehelper`'s `JniInvocation` falls back to `libart.so`;
  - `frameworks/native`'s installd defaults to `libart.so` and always runs `dex2oat`.

  `SystemServer` writes `persist.sys.dalvik.vm.lib` back from `VMRuntime.vmLibrary()`, which is
  now always `libart.so`. The runtime picker in Settings' developer options removes itself when
  only one of `/system/lib/libdvm.so` and `/system/lib/libart.so` exists
  (`DevelopmentSettings.filterRuntimeOptions`), so Settings needs no patch.
- **dex2oat filters.** `system.prop` sets `persist.glass.d2o.sys=speed` (code under `/system`)
  and `persist.glass.d2o.usr=space` (user apps). installd passes them as
  `--runtime-arg -compiler-filter:<filter>`. A line `<package> <filter>` in
  `/data/system/glass-dex2oat-filters` overrides them for one package: for example
  `com.example interpret-only` for an app that dex2oat fails on; for system APKs the key is the
  file name (`Settings`), since installd only sees the path. The boot image keeps dex2oat's
  own default for images, `everything`. 4.4's dex2oat compiles dex files of 900 methods or fewer
  with `speed` unless the filter is `interpret-only`, so `space` only changes larger apps, and
  compiles the runtime starts itself (after `/data/dalvik-cache` is wiped, or for a
  `DexClassLoader`) use the runtime's default filter.

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
- **Storage.** 4.4 needs the emulated sdcard set up by init, which 6.0's vold does itself.
  `rootdir/init.omap4430.rc` sets it up as XE24 did: `/mnt/shell/emulated`, `/storage/emulated`,
  the `EXTERNAL_STORAGE` and `EMULATED_STORAGE_*` variables, the legacy symlinks, and
  `service sdcard`. The service uses 4.4's option syntax
  (`-u 1023 -g 1023 -l /data/media /mnt/shell/emulated`, as AOSP's hammerhead), not XE24's
  older positional form. installd creates `/data/media` and its per-user directories (4.4's
  init.rc does not). `storage_list.xml` lists only the emulated volume: Glass has no SD card or
  USB host storage. AOSP hammerhead also sets `ro.crypto.fuse_sdcard`, which only matters for
  encryption; Glass's fstab has no `encryptable` entry, so it is left out. `docs/hardware-facts.md`
  lists these rows for this branch.
- **HALs.** `power/` and `lights/` already build for API 19 (`docs/hals.md`; CI job `hals`, 4.4.4
  target).

## Lean items

The plan's Task 4 rows that are one makefile line or one value. Each can be reverted alone.
None is measured yet; plan Task 7 measures them on a device.

| Item | Where | Revert |
|---|---|---|
| Apps and services not built: WAPPushManager; Browser, Calculator, Calendar, Camera2, Contacts, DeskClock, Email, Exchange2, Gallery2, Music, QuickSearchBox, OneTimeInitializer, VideoEditor; Launcher2; the input methods (LatinIME, OpenWnn, PinyinIME and dictionaries); live wallpapers and WallpaperCropper; the dreams (BasicDreams, PhotoTable) | `GLASS_REMOVED_PACKAGES` in `BoardConfig.mk` | delete a name |
| Not voice or SMS capable, no mobile network | `config_voice_capable`, `config_sms_capable`, `networkAttributes` and `radioAttributes` (Wi-Fi and Bluetooth only, as AOSP's Wi-Fi-only grouper) in `overlay/.../values/config.xml` | set true / drop the arrays |
| No screen savers | `config_dreamsSupported` in the same overlay | set true |
| Heap 5m/48m/128m for the 640x360 display (was phone-xhdpi-1024's 8m/96m/256m; XE24 used 5m/72m/192m) | `device.mk`, `phone-hdpi-512-dalvik-heap.mk` | inherit the xhdpi-1024 file |
| Cortex-A9 code generation (`-mcpu=cortex-a9`, `--no-fix-cortex-a8`) | OpenMyGlass/android_build `glass-4.4`, `armv7-a-neon.mk`, pinned by OpenMyGlass/manifest from its first merged version; the variant comes from `hardware/ti/omap4` | pin omap4-aosp's build in the manifest |

Why a filter. `inherit-product` only adds packages, so `BoardConfig.mk` filters the product's
`PRODUCT_PACKAGES`. `build/core/config.mk` reads it after product configuration
(`envsetup.mk`) and before `main.mk` collects the product's modules.

What stays:
- AOSP's minimal `Home` (from `core_base.mk`) as the home screen until the Glass launcher (R5)
  exists. 4.4's ActivityManager finishes booting when the first activity, the home screen, goes
  idle, so some home app has to be there.
- TeleService and TelephonyProvider: 4.4 Settings runs About > Status in the `com.android.phone`
  process and calls `PhoneFactory.getDefaultPhone()`, so without TeleService that screen
  crashes. AOSP's Wi-Fi-only 4.4 tablets kept it too.
- PrintSpooler: without it, system_server's print service waits 10 s on each boot for a
  spooler that never binds.
- Settings, SystemUI, Keyguard, Bluetooth, Provision, CertInstaller, KeyChain, FusedLocation,
  DocumentsUI, DownloadProviderUi, PackageInstaller, PicoTts, and the providers.

Not done here:
- `ro.config.low_ram`: the owner decides after measurement.
- zRAM size and KSM: kernel config, glass-kernel.
- Logger buffer sizes: a kernel source change, glass-kernel.
- `preloaded-classes` and `preloaded_drawables` trimming: generated on a device.
- A minimal SystemUI.
- No input method is built (input comes from the phone), but `permissions/hardware.xml` still
  declares `android.software.input_methods`. Typing on the device (a Wi-Fi password, for
  example) needs `adb shell input text`. Whether to drop the feature is open.
- Wi-Fi sleep policy and the Bluetooth sniff and timeout values: they need measurement first.
  `wifi.supplicant_scan_interval=60` and `bluetooth.enable_timeout_ms` are already in
  `system.prop`.

## Device checks

All not run (no hardware). Use `fastboot boot` only; never flash `bootloader`, `xloader` or
`fpga`.
- `boot.img` from this branch boots with `fastboot boot`, and `uname -r` shows the glass-kernel
  `tuned-stable` version (3.4.113).
- `/system/lib/modules/pvrsrvkm_sgx540_120.ko` loads (`cat /proc/modules`), and
  `dmesg | grep SGXInit` has no `(FAIL)` line after `pvrsrvinit`.
- `ls -l /system/vendor/lib/libbltsville*` shows the files and symlinks from `hardware/ti/omap4`,
  and nothing in `logcat` fails to load them.
- First boot completes, and `getprop persist.sys.dalvik.vm.lib` gives `libart.so`.
- `/data/dalvik-cache` holds `system@framework@boot.art` and `system@framework@boot.oat`.
- `ls /system/lib/libdvm.so /system/bin/dexopt` finds neither, and `logcat` has no
  JniInvocation `Falling back` line.
- Developer options show no "Select runtime" entry.
- Installing an APK logs `installd: Running /system/bin/dex2oat ... filter=space`; `dumpsys meminfo` works.
- Time from `fastboot boot` to `sys.boot_completed=1` on the first boot, and the size of
  `/data/dalvik-cache` (plan Task 3).
- Lean: `pm list packages` has none of `GLASS_REMOVED_PACKAGES`; Settings > About > Status opens;
  Settings shows no "Mobile networks" entry; `dumpsys telephony.registry` works; an app calling
  `TelephonyManager.getDeviceId()` gets null, not a crash; the home screen is AOSP's `Home`;
  SystemUI shows the low-battery warning; Wi-Fi and Bluetooth reconnect after screen-off;
  `readelf -A /system/lib/libc.so` shows `Tag_CPU_name: "Cortex-A9"`.
- Storage: `getprop | grep -i storage` and `echo $EXTERNAL_STORAGE` (adb shell) show
  `/storage/emulated/legacy`; `ps | grep sdcard` shows the daemon; a file written to `/sdcard`
  appears in `/data/media/0`; an app with `WRITE_EXTERNAL_STORAGE` can write there.
- SurfaceFlinger starts on the PowerVR driver: `logcat | grep -i egl` names
  `/vendor/lib/egl/libEGL_POWERVR_SGX540_120.so`.
