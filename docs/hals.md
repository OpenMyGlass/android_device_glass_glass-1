<!-- SPDX-License-Identifier: Apache-2.0 -->
# Open HALs

Each HAL lives in its own directory with an `Android.mk`, is named `<id>.glass_1` and uses the
legacy `hardware/libhardware` C interface, limited to what 4.4 has, so it builds and runs on both
targets: android-4.4.4_r2 (KitKat, the near-term target) and android-6.0.1_r31. libhardware
(`hardware.c`, the same `variant_keys` in 4.4 and 6.0) tries `ro.hardware` (omap4430), then
`ro.product.board` (glass_1), then `ro.board.platform` (omap4), so `<id>.glass_1.so` wins over
`<id>.omap4.so`.

Kernel references are to [kernel-omap](https://github.com/OpenMyGlass/kernel-omap) branch
`glass-omap-xrx13b` at `989d925` (the XE24 source). The files cited here are the same on
`glass-omap-xrx13b-stable-3.4.113`. "Stock" means the XE24 factory image: its ramdisk (`init.rc`,
`init.omap4430.rc`) and `/system/lib/hw`, which has `lights.omap4.so` but no power HAL.

## CI

`.github/workflows/hals.yml` (job `hals`, one run per target: android-4.4.4_r2 at API level 19,
android-6.0.1_r31 at API level 23):
1. `tools/fetch-ndk.sh` downloads the NDK pinned in `tools/ndk.env` (r25c, the newest NDK that still
   supports API 19) and checks its SHA-256 and SHA-1. CI caches the zip.
2. `tools/fetch-aosp-headers.sh <tag> <dir>` fetches each project pinned for the tag in
   `tools/aosp-headers.lock` (shallow) and fails unless it is the pinned commit.
3. `tools/build-hals.py --api <level>` builds every HAL directory listed in the workflow's `HALS`
   with `armv7a-linux-androideabi<level>-clang`, `-Wall -Werror` plus the module's `LOCAL_CFLAGS`. It reads the sources and libraries from `Android.mk`, then checks
   that `HAL_MODULE_INFO_SYM` is exported, that the library needs nothing beyond libc, libm, libdl
   and its `LOCAL_SHARED_LIBRARIES`, and that every undefined symbol comes from the NDK stub of a
   library it links, at the job's API level, or from `tools/hal-symbols.allow`.

A new HAL adds its directory to `HALS`, any new AOSP project to the lock file (both tags, peeled
tag commit from `git ls-remote <url> refs/tags/<tag>^{}`), and its non-NDK symbols to the allowlist.

Host tests: `tests/test_hal_logic.py` builds `tests/hal_logic_test.c` with the pure-logic files
(`power/power_logic.c`, `lights/lights_logic.c`) and runs it against a scratch directory that stands
in for sysfs. `tests/test_build_hals.py` tests the `Android.mk` reader and the symbol checks.

### Differences between 4.4 and 6.0

- `hardware/lights.h` is the same.
- `hardware/power.h`: 4.4 has power module API 0.1 and 0.2 (`init`, `setInteractive`, `powerHint`
  with `POWER_HINT_VSYNC`, `_INTERACTION`, `_VIDEO_ENCODE`, `_VIDEO_DECODE`). 6.0 adds API 0.3
  (`setFeature`, `POWER_FEATURE_DOUBLE_TAP_TO_WAKE`), `POWER_HINT_LOW_POWER`, and gives
  `POWER_HINT_INTERACTION` a duration in milliseconds as data. The power HAL declares API 0.2 and
  uses none of the 6.0 additions, so no `#if` is needed.
- Build: 4.4's `log/uio.h` needs `HAVE_SYS_UIO_H`, which the 4.4 build gets from its force-included
  `build/core/combo/include/arch/linux-arm/AndroidConfig.h` (line 114). `tools/build-hals.py` defines it
  for both targets; 6.0 ignores it.
- Makefiles: `LOCAL_MODULE_RELATIVE_PATH` arrived in 5.0 (it is in `build/core/base_rules.mk` at
  android-5.0.0_r1, not at android-4.4.4_r2). The HALs use `LOCAL_MODULE_PATH :=
  $(TARGET_OUT_SHARED_LIBRARIES)/hw`, which both builds accept (the device is 32-bit only).

## Power HAL (`power/`, `power.glass_1`)

Stock XE24 has no power HAL: the governor runs on the kernel defaults, and nothing writes
`boostpulse` or caps the frequency with the screen off. This HAL keeps that behaviour by default. Its
two extra behaviours, the interaction boost and the screen-off cap, are built in but switched off by
compile-time constants in `power/power_logic.h` (both are owner decisions, below).

`power.omap4` from hardware/ti/omap4 is replaced as the HAL that runs, but it stays installed:
`hardware/ti/omap4/omap4.mk`, which `device.mk` inherits, adds it to `PRODUCT_PACKAGES`, and an
inherited product's packages cannot be filtered out cleanly from `device.mk`. It is never loaded,
because of the lookup order above. If it were loaded, it would set its own governor values
(`go_hispeed_load` 50, `hispeed_freq` the second OPP, `min_sample_time` 60000,
`above_hispeed_delay` 100000) and cap `scaling_max_freq` to the second OPP (600 MHz) with the screen
off. None of that is stock behaviour.

| Behaviour | Path | Value | Source |
|---|---|---|---|
| Governor | `cpu0/cpufreq/scaling_governor` | interactive (not written) | `notle_defconfig:532` `CPU_FREQ_DEFAULT_GOV_INTERACTIVE=y` |
| init | `cpufreq/interactive/timer_rate` | 20000 | `cpufreq_interactive.c:88` |
| init | `cpufreq/interactive/min_sample_time` | 80000 | `cpufreq_interactive.c:82` |
| init | `cpufreq/interactive/go_hispeed_load` | 99 | `cpufreq_interactive.c:69` |
| init | `cpufreq/interactive/target_loads` | 90 | `cpufreq_interactive.c:73` |
| init | `cpufreq/interactive/above_hispeed_delay` | 20000 | `cpufreq_interactive.c:95` |
| init | `cpufreq/interactive/boostpulse_duration` | 80000 | `cpufreq_interactive.c:101` |
| `POWER_HINT_INTERACTION` | `cpufreq/interactive/boostpulse` | none: boost disabled (`1` when enabled) | attribute at `cpufreq_interactive.c:845` (mode 0200); see below |
| `POWER_HINT_VSYNC` (data non-NULL) | `cpufreq/interactive/boostpulse` | none: boost disabled (`1` when enabled) | same |
| `setInteractive(0)` | `cpu0/cpufreq/scaling_max_freq` | none: cap disabled | see below |

Paths are under `/sys/devices/system/cpu/`. "Stock" applies to the six init values only: they are the
kernel's defaults, which is what stock XE24 runs. Its `init.omap4430.rc` has its own `timer_rate`,
`min_sample_time` and `hispeed_freq` lines commented out, and stock `init.rc` only sets owners and
modes. They also match the `stock` profile in glass-kernel's `docs/tuning.md`. `init` runs on every
system_server start, so the defaults are written again after a framework restart, replacing any value
set at runtime (for example by glass-kernel's `tuning/omg-tune.sh`). `hispeed_freq` is not written:
the kernel sets it to the policy maximum when the governor starts (`cpufreq_interactive.c:927`). The
HAL writes no frequency, voltage or thermal value. If the governor is not interactive, the writes fail
and are logged.

Permissions: AOSP's own `init.rc`, in its `on boot` section, chowns every interactive tunable
(including `boostpulse`) and `cpu0/cpufreq/scaling_max_freq` to system and makes them 0660 (`boostpulse`
keeps its 0200). That is system/core `rootdir/init.rc` lines 305-331 at android-4.4.4_r2 and 441-467 at
android-6.0.1_r31. The power HAL runs in system_server (uid system). SELinux is permissive
(`androidboot.selinux=permissive` in `BoardConfig.mk`), so no policy is needed yet.

### Interaction boost (owner decision)

Disabled: `GLASS_BOOSTPULSE_ENABLED` in `power/power_logic.h` is 0, and `POWER_HINT_INTERACTION` and
`POWER_HINT_VSYNC` do nothing. Stock has no power HAL, so nothing pulses `boostpulse` on XE24. A pulse
makes the governor jump to `hispeed_freq` (the policy maximum, 1008 MHz) for `boostpulse_duration`
(80 ms) on every touch, which adds time at the top OPP. That is new behaviour under rule 6, so it is
left to the owner. Set the constant to 1 to turn it on. `boostpulse` is then opened once and kept open.
If it is missing, the HAL logs once and tries again on the next hint. If a write fails, it closes the
file and reopens it next time. The host tests cover both settings.

The stock XE24 ramdisk also writes `cpufreq/interactive/boost_timeout` and
`cpu0/cpufreq/boost_cpufreq` at boot. Neither attribute exists in the GPL kernel source, and this HAL
does not use them.

### Screen-off cap (owner decision)

The mechanism is implemented and disabled: `GLASS_SCREEN_OFF_MAX_FREQ` in `power/power_logic.h` is
`NULL`. Neither stock XE24 nor glass-kernel's tuning gives a screen-off value. The `cool` profile's
800000 is an unmeasured runtime profile, not a screen-off cap. Setting the constant to a kHz string
turns the cap on. With the screen off, the HAL then:
- checks the value against `scaling_available_frequencies` (`omap-cpufreq.c:584`). The OMAP4430 MPU
  OPPs are 300000, 600000, 800000 and 1008000 (`opp4xxx_data.c:226-232`; 1200000 at line 234 is
  disabled);
- saves `scaling_max_freq` and writes the cap, but only if that lowers the maximum;
- with the screen back on, restores the saved value, unless someone changed the maximum in the meantime.

## Lights HAL (`lights/`, `lights.glass_1`)

Replaces `lights.omap4` in `device.mk`'s `PRODUCT_PACKAGES`. OMAP4-AOSP's hardware/ti/omap4 has no
lights module, and its `omap4.mk` does not add one, so before this the name built nothing and no
lights HAL was installed. The stock `lights.omap4.so` is not in `proprietary-files.txt` and is not used.

| Light | Path | Value | Source |
|---|---|---|---|
| backlight | `/sys/devices/platform/omapdss/manager2/panel-notle-dpi/brightness` | 0-255 | the scale of `range_expand`/`range_reduce` (`panel-notle-dpi.c:934-949`), which convert it to and from the internal value; kobject `panel-notle-dpi` under the DSS manager at `:2218` |
| every other id | - | `open` returns `-EINVAL` | `notle_defconfig:2423` `CONFIG_NEW_LEDS is not set` |

- Conversion: the color's RGB is turned into perceived brightness, `(77 R + 150 G + 29 B) >> 8`, the
  usual AOSP weighting (white gives 255). That value is scaled to the panel's 0..255, which is the
  same range, and written as a decimal number. 0 turns the backlight off (`panel-notle-dpi.c:980-995`).
  Flash and brightness mode are ignored: the backlight cannot blink, and auto-brightness runs in the framework.
- The driver maps the written value through `range_expand` (`panel-notle-dpi.c:934-941`) and caps
  the result at `brightness_limit`, whose default is `MAX_BRIGHTNESS` (0xFF internal, `:49`, set at
  `:2177`, applied at `:1517-1518`). With the default limit, any value from 223 to 255 gives the same
  light. The HAL does not write `brightness_limit` or `brightness_multiplier`. The stock ramdisk writes
  100 to `brightness` at boot and 255 to `brightness` in charger mode (`docs/hardware-facts.md`,
  Sysfs writes).
- Permissions: `rootdir/init.omap4430.rc` already chowns `brightness` to system, and the attribute is
  0644 (`panel-notle-dpi.c:1271`), so the system server can write it. SELinux is permissive. No
  rootdir change was needed.
- Other LEDs on the board are not Android lights: the two `notleboard::status` GPIO LEDs
  (`board-notle.c:419-430`) need the LED class, which is not built. `glasshub` `led_drive`
  (`glasshub.c:1057-1068`) is the IR LED of the wink sensor, and `ps_led` (`ltr506als.c:1098`) is the
  proximity sensor's LED. Both belong to the sensors.

## No GPS HAL

There is no GPS HAL, by the owner's decision. Location will come from the phone through the
OpenMyGlass companion (a location provider, later work), not from the on-board receiver.

The on-board receiver is a CSR SiRFstarIV GSD4e (`drivers/char/gps_elton.c:1-5`) on OMAP UART4,
`/dev/ttyO3` (`board-notle.c:1143-1155`, only TX and RX muxed), with three GPIOs: ON_OFF (139),
RESET_N (140) and AWAKE (138) on EVT2 and later boards (`board-notle.h:75-80`). The kernel driver
exposes them as `/sys/class/gps/gps_elton/{onoff,reset,awake}`, plus `functional`, which reads 1
only when the board id is `V1_5_PROTO` (`board-notle.c:2080-2082`; `V1_5_PROTO` is the newest
board id the board file supports, `board-notle.h:116-118`). The receiver's facts are recorded in
[`docs/specs/gps.md`](specs/gps.md), which is not implemented.

How the receiver stays off:
- At boot the board file requests ON_OFF as an output driven low (`board-notle.c:2053-2054`) and
  RESET_N as an output driven high, out of reset (`board-notle.c:2063-2064`). The GSD4e leaves
  hibernate only on an ON_OFF pulse (`docs/specs/gps.md`, 4.1), and nothing pulses it, so it stays
  in hibernate. The driver does nothing on suspend or resume besides logging
  (`gps_elton.c:116-128`).
- No HAL or init file in this tree may write `onoff` or `reset`. `rootdir/` neither chowns nor
  chmods the `gps_elton` files. The driver itself creates all four attributes mode 0666
  (`gps_elton.c:109-114`), so any process can write `onoff` and `reset`; this tree leaves that kernel
  default alone and adds no writer.
- `/dev/ttyO3` keeps its stock owner (system, 0660, `docs/hardware-facts.md`), with nothing opening
  it. `/data/gps` and `/data/gps/nvm` are still created (stock facts in `docs/hardware-facts.md`)
  but nothing uses them.
- `proprietary-files.txt` no longer lists the stock `lib/hw/gps.omap4.so` (the CSR location library)
  or `vendor/firmware/NVM2`. NVM2 is the receiver's ROM patch, which only that library downloads to
  the chip (`docs/specs/gps.md`, 4.3, where the CSR library's configuration names it): nothing in
  this tree or the kernel source does. With no `gps.*.so`, the framework finds no GPS HAL and
  reports GPS as unsupported.

`permissions/hardware.xml` still declares `android.hardware.location.gps`, and
`power_profile.xml` keeps `gps.on`. Whether to drop the feature until the phone-backed provider
exists is an owner decision.

## What a 4.4 branch needs

Both HALs build unchanged on a 4.4 tree: same sources, same `Android.mk`.
- Power: nothing beyond the tree's `PRODUCT_PACKAGES` line. 4.4's `init.rc` already gives system the
  governor files (above), and 4.4's PowerManagerService uses power API 0.2. `hardware/ti/omap4` on a 4.4
  tree may add its own `power.omap4`; it stays unused for the same lookup-order reason.
- Lights: nothing beyond `PRODUCT_PACKAGES` and the `brightness` chown in `init.omap4430.rc`, which the
  stock 4.4 ramdisk already has. 4.4's LightsService handles a NULL device for absent lights as 6.0's does.
- SELinux: 4.4 on this tree is permissive too. Enforcing would need policy for system_server to write
  `sysfs` on both trees.
