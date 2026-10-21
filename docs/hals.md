<!-- SPDX-License-Identifier: Apache-2.0 -->
# Open HALs

Each HAL lives in its own directory with an `Android.mk`, is named `<id>.glass_1` and uses the
legacy `hardware/libhardware` C interface, so it builds against both android-6.0.1_r31 and
android-5.1.1_r37. libhardware tries `ro.hardware` (omap4430), then `ro.product.board`
(glass_1), then `ro.board.platform` (omap4), so `<id>.glass_1.so` wins over `<id>.omap4.so`.

Kernel references are to [kernel-omap](https://github.com/OpenMyGlass/kernel-omap) branch
`glass-omap-xrx13b` at `989d925` (the XE24 source). The files cited here are the same on
`glass-omap-xrx13b-stable-3.4.113`. "Stock" means the XE24 ramdisk (`init.rc`, `init.omap4430.rc`).

## CI

`.github/workflows/hals.yml` (job `hals`, one run per release tag):
1. `tools/fetch-aosp-headers.sh <tag> <dir>` fetches each project pinned for the tag in
   `tools/aosp-headers.lock` (shallow) and fails unless it is the pinned commit.
2. `tools/build-hals.py` builds every HAL directory listed in the workflow's `HALS` with the
   runner's NDK (`$ANDROID_NDK_LATEST_HOME`), `armv7a-linux-androideabi21-clang`, `-Wall -Werror`
   plus the module's `LOCAL_CFLAGS`. It reads the sources and libraries from `Android.mk`, then checks
   that `HAL_MODULE_INFO_SYM` is exported, that the library needs nothing beyond libc, libm, libdl
   and its `LOCAL_SHARED_LIBRARIES`, and that every undefined symbol comes from the NDK stub of a
   library it links or from `tools/hal-symbols.allow`.

A new HAL adds its directory to `HALS`, any new AOSP project to the lock file (both tags, peeled
tag commit from `git ls-remote <url> refs/tags/<tag>^{}`), and its non-NDK symbols to the allowlist.

Host tests: `tests/test_hal_logic.py` builds `tests/hal_logic_test.c` with the pure-logic files
(`power/power_logic.c`, `lights/lights_logic.c`) and runs it against a scratch directory that stands
in for sysfs. `tests/test_build_hals.py` tests the `Android.mk` reader and the symbol checks.

### Interface differences between 5.1 and 6.0

`hardware/power.h` and `hardware/lights.h` are the same apart from one comment: from 6.0,
`POWER_HINT_INTERACTION`'s data is the expected duration in milliseconds (unused here). No `#if` is needed.

## Power HAL (`power/`, `power.glass_1`)

Replaces `power.omap4` from hardware/ti/omap4. That module set its own governor values at init
(`go_hispeed_load` 50, `hispeed_freq` the second OPP, `min_sample_time` 60000, `above_hispeed_delay`
100000) and capped `scaling_max_freq` to the second OPP (600 MHz on Glass) with the screen off.
Neither is Glass's stock behaviour. This HAL goes back to stock.

| Behaviour | Path | Value | Source |
|---|---|---|---|
| Governor | `cpu0/cpufreq/scaling_governor` | interactive (not written) | `notle_defconfig:532` `CPU_FREQ_DEFAULT_GOV_INTERACTIVE=y` |
| init | `cpufreq/interactive/timer_rate` | 20000 | `cpufreq_interactive.c:88` |
| init | `cpufreq/interactive/min_sample_time` | 80000 | `cpufreq_interactive.c:82` |
| init | `cpufreq/interactive/go_hispeed_load` | 99 | `cpufreq_interactive.c:69` |
| init | `cpufreq/interactive/target_loads` | 90 | `cpufreq_interactive.c:73` |
| init | `cpufreq/interactive/above_hispeed_delay` | 20000 | `cpufreq_interactive.c:95` |
| init | `cpufreq/interactive/boostpulse_duration` | 80000 | `cpufreq_interactive.c:101` |
| `POWER_HINT_INTERACTION` | `cpufreq/interactive/boostpulse` | `1` | attribute at `cpufreq_interactive.c:845` (mode 0200) |
| `POWER_HINT_VSYNC` (data non-NULL) | `cpufreq/interactive/boostpulse` | `1` | same |
| `setInteractive(0)` | `cpu0/cpufreq/scaling_max_freq` | none: cap disabled | see below |

Paths are under `/sys/devices/system/cpu/`. The init values are the kernel's defaults, which is what
stock XE24 runs: its `init.omap4430.rc` has its own `timer_rate`, `min_sample_time` and `hispeed_freq`
lines commented out, and stock `init.rc` only sets their owners and modes. They also match the `stock` profile in
glass-kernel's `docs/tuning.md`. Writing them makes the governor's starting point independent of the
kernel build and of whatever ran before the framework. `hispeed_freq` is not written: the kernel sets
it to the policy maximum when the governor starts (`cpufreq_interactive.c:927`). The HAL writes no
frequency, voltage or thermal value. If the governor is not interactive, the writes fail and are logged.

`boostpulse` is opened once and kept open. If it is missing, the HAL logs once and tries again on the
next hint. If a write fails, it closes the file and reopens it next time.

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

Replaces `lights.omap4` in `PRODUCT_PACKAGES`. OMAP4-AOSP's hardware/ti/omap4 has no lights module, so before this
nothing provided a lights HAL. The stock `lights.omap4.so` is not in `proprietary-files.txt` and is not used.

| Light | Path | Value | Source |
|---|---|---|---|
| backlight | `/sys/devices/platform/omapdss/manager2/panel-notle-dpi/brightness` | 0-255 | `panel-notle-dpi.c:49` `MAX_BRIGHTNESS 0xFF`; kobject `panel-notle-dpi` under the DSS manager at `:2218` |
| every other id | - | `open` returns `-EINVAL` | `notle_defconfig:2423` `CONFIG_NEW_LEDS is not set` |

- Conversion: the color's RGB is turned into perceived brightness, `(77 R + 150 G + 29 B) >> 8`, the
  usual AOSP weighting (white gives 255). That value is scaled to the panel's 0..255, which is the
  same range, and written as a decimal number. 0 turns the backlight off (`panel-notle-dpi.c:980-995`).
  Flash and brightness mode are ignored: the backlight cannot blink, and auto-brightness runs in the framework.
- The driver maps the written value through `range_expand` (`panel-notle-dpi.c:934-941`) and caps
  the result at `brightness_limit`. Its default is the internal 255 (`:2177`, applied at `:1517-1518`),
  so with the default limit any value from 223 up gives the same light. The HAL does not write
  `brightness_limit` or `brightness_multiplier`. Stock writes 100 at boot and 255 in charger mode (see
  `docs/hardware-facts.md`).
- Permissions: `rootdir/init.omap4430.rc` already chowns `brightness` to system, and the attribute is
  0644 (`panel-notle-dpi.c:1271`), so the system server can write it. No rootdir change was needed.
- Other LEDs on the board are not Android lights: the two `notleboard::status` GPIO LEDs
  (`board-notle.c:419-430`) need the LED class, which is not built. `glasshub` `led_drive`
  (`glasshub.c:1057-1068`) is the IR LED of the wink sensor, and `ps_led` (`ltr506als.c:1098`) is the
  proximity sensor's LED. Both belong to the sensors.
