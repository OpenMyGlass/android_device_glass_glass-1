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
(`power/power_logic.c`, `lights/lights_logic.c`), and `tests/sensors_logic_test.c` with
`sensors/sensors_logic.c`, and runs each against a scratch directory that stands in for sysfs.
`tests/test_build_hals.py` tests the `Android.mk` reader and the symbol checks.
`tests/test_audio_logic.py` builds `tests/audio_logic_test.c` with `audio/audio_logic.c` and
`tests/mock_tinyalsa.c`, a fake tinyalsa mixer with the kernel's control names, channel counts and
ranges, and checks the policy file and the sysfs owners against `docs/hardware-facts.md`.

### Differences between 4.4 and 6.0

- `hardware/audio.h`: both have device API 2.0, which the audio HAL declares. 6.0 adds API 3.0
  (audio patches and ports) and longer `open_output_stream` (`address`) and `open_input_stream`
  (`flags`, `address`, `source`) signatures, which the HAL selects with
  `#ifdef AUDIO_DEVICE_API_VERSION_3_0`. `system/audio.h` is in system/core at 4.4 and in
  system/media at 6.0; `tools/build-hals.py` has both include roots, and also
  `system/media/audio_utils/include` for the resampler. tinyalsa's `asoundlib.h` and
  audio_utils' `resampler.h` have everything the HAL uses at both tags.
- `hardware/lights.h` is the same.
- `hardware/power.h`: 4.4 has power module API 0.1 and 0.2 (`init`, `setInteractive`, `powerHint`
  with `POWER_HINT_VSYNC`, `_INTERACTION`, `_VIDEO_ENCODE`, `_VIDEO_DECODE`). 6.0 adds API 0.3
  (`setFeature`, `POWER_FEATURE_DOUBLE_TAP_TO_WAKE`), `POWER_HINT_LOW_POWER`, and gives
  `POWER_HINT_INTERACTION` a duration in milliseconds as data. The power HAL declares API 0.2 and
  uses none of the 6.0 additions, so no `#if` is needed.
- `hardware/sensors.h`: both have device API 1.0 and 1.1 (`batch`, `flush`,
  `META_DATA_FLUSH_COMPLETE`), the same `sensors_event_t`, and the types the sensors HAL uses
  (including the uncalibrated ones, 14 and 16). 6.0 adds device API 1.2 to 1.4, and `sensor_t` gains
  `stringType`, `requiredPermission`, `maxDelay` and `flags` in place of four of the six reserved
  pointers. The sensors HAL fills those only under `#ifdef SENSORS_DEVICE_API_VERSION_1_3`, which only
  the 6.0 header defines.
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

## Sensors HAL (`sensors/`, `sensors.glass_1`)

Replaces the stock `sensors.glass_1.so`, a shim over InvenSense's proprietary MPL
(`libinvensense_hal.so`, `libmllite.so`, `libmplmpu.so`) and a DMP firmware image. That blob is
dropped from `proprietary-files.txt`. The facts come from `docs/specs/sensors.md` (a clean-room spec
written by a separate reverse-engineering agent, cited below as "spec §n") and from the GPL kernel
source. No MPL code, DMP image or InvenSense library is used: the driver's raw mode gives
accelerometer, gyroscope and compass data without firmware (spec §4.6). Kernel line numbers in
this section, like the spec's, are from `glass-omap-xrx13b-stable-3.4.113`.

Interface: `sensors_module_t` with module API 0.1 and a legacy `sensors_poll_device_1` at device
API 1.1, the level of the stock HAL (spec §2). Both 4.4's and 6.0's SensorService take it (6.0 logs
"Upgrade sensor HAL to version 1_3" and derives the reporting mode from the type).

| Handle | Type | Source | Range | Resolution | Min delay | Power (mA) |
|---|---|---|---|---|---|---|
| 1 | ACCELEROMETER | MPU-9150 accel, IIO raw | 19.6133 m/s² (±2 g) | 0.000599 m/s² | 5 ms | 0.0 (stock value) |
| 2 | MAGNETIC_FIELD | AK8975 behind the MPU, IIO raw, hard-iron calibrated | 1200 µT | 0.3 µT | 10 ms | 10.0 |
| 3 | GYROSCOPE | MPU-9150 gyro, IIO raw, bias at rest removed | 34.9066 rad/s (±2000 °/s) | 0.0010653 rad/s | 5 ms | 5.5 |
| 4 | LIGHT | `ltr506_als` input device | 64000 | 1.0 | on change | 0.75 |
| 5 | MAGNETIC_FIELD_UNCALIBRATED | as 2, with the bias it removes | as 2 | | 10 ms | 10.0 |
| 6 | GYROSCOPE_UNCALIBRATED | as 3, with the bias it removes | as 3 | | 5 ms | 5.5 |

The slowest continuous rate is 4 Hz (`maxDelay` 250 ms on 6.0), the kernel's minimum FIFO rate.
`fifoReservedEventCount` and `fifoMaxEventCount` are 0: there is no hardware batching. Orientation,
rotation vector, game rotation vector, gravity and linear acceleration are not listed: SensorService
on 4.4 and 6.0 registers its own virtual sensors when the HAL has an accelerometer, a gyroscope and a
magnetometer (spec §7). The stock vendor sensors GLU and TAP need the DMP image and are left out (owner
decision below). `permissions/hardware.xml` already declares accelerometer, compass, gyroscope and
light.

Code: `sensors/sensors_logic.c` holds the parts the host tests cover (the sensor table and its
checks, the IIO configuration and the sysfs write sequence, scan-type parsing, record layout and
parsing, matrices, unit and clock conversions, the calibrations, the calibration file format, and
finding the devices by name). `sensors/sensors.c` is the HAL glue: files, threads, events.

### Data path

- One reader thread polls `/dev/iio:deviceN`, the ALS event node and a wake pipe, and puts events in
  a 256-entry queue (`sns_queue_*`). When it is full, the oldest data event is dropped, with a log
  line; a `META_DATA_FLUSH_COMPLETE` is never dropped, because SensorService holds a connection's
  events until the flush it asked for arrives. `poll()` waits on the queue. `flush()` queues
  `META_DATA_FLUSH_COMPLETE` behind the events already there, and returns `-EINVAL` for a sensor
  that is not enabled (`-ENOMEM` if the queue holds nothing but flushes). `batch()` accepts only a zero timeout, as stock
  (spec §2); 4.4's and 6.0's `SystemSensorManager` then retry in continuous mode ("Try continuous
  mode if batching fails").
- The IIO device is found by its `name`, `mpu9150` (`board-notle.c:1742`), not assumed to be
  `iio:device0`. Its trigger is the one named `<name>-dev<N>` (`inv_mpu_trigger.c:67`), looked up
  among `trigger*/name`; if none matches, `trigger0`'s name is used, as stock does (spec §5.1).
- Enable sequence (`sns_plan_iio`, spec §4.6): `power_state` 1, `buffer/enable` 0, `dmp_on` 0 only
  if `firmware_loaded` is 1, `gyro_enable`, `accl_enable`, `compass_enable`, `in_anglvel_scale` 3
  and `in_accel_scale` 0 (index form: ±2000 °/s and ±2 g, the probe defaults), `sampling_frequency`,
  every `scan_elements/*_en` (quaternion off, all three axes of each enabled sensor, timestamp on),
  `trigger/current_trigger`, `buffer/length` 480, `buffer/enable` 1. With nothing enabled:
  `buffer/enable` 0 and `power_state` 0. Right after every `buffer/enable` 0 the HAL reads the
  device node until it is empty, so no record of the old layout is parsed with the new one. A
  failed required write switches the IIO side off and fails the `activate` or `setDelay`/`batch`;
  the HAL then restores the previous enabled set or rate and re-applies it, so its state and the
  driver's agree.
- Rate: the fastest requested rate among enabled motion sensors, rounded up to whole Hz, clamped to
  4-200 Hz (100 Hz for the compass alone). With the compass on, a rate above 100 Hz becomes 200 Hz:
  the driver reads the compass every `10 × rate / 1000` samples (`inv_mpu_core.c:655-659`), which at
  101-199 Hz is every sample, faster than the AK8975 converts. Sensors may get events faster than
  asked; the HAL does not decimate.
- Records are parsed by the enabled scan elements: sizes from `scan_elements/*_type`
  (`le:s16/16>>0`, falling back to the spec §4.4 types), each element aligned to its size and the
  timestamp last, as `iio_sw_buffer_preenable` computes it. The format byte before the timestamp
  must equal the enabled set (0x02 gyro, 0x04 accel, 0x08 compass); other records are dropped and
  counted. An all-zero compass triple means "no new sample" (spec §4.4) and is skipped.
- Mounting: the HAL reads `gyro_matrix`, `accl_matrix` and `compass_matrix` and applies body = M ×
  chip (spec §4.3). A matrix that is not a signed permutation is replaced by the identity, with an
  error in the log.
- Units (spec §4.2), with the scales read back after power-on (they read `-EINVAL` while the chip
  sleeps, `mpu_read_raw`): gyro raw × FSR/32768 × π/180 rad/s, accel raw × FSR/32768 × 9.80665 m/s²,
  compass raw × `in_magn_scale`/2³⁰ µT.

### Timestamps

Android 5 and later want `CLOCK_BOOTTIME`. IIO records carry the OMAP 32 kHz "robust clock"
(spec §4.5), which keeps counting in suspend but has another zero. After every IIO
reconfiguration, and every 10 s while data flow, the HAL reads `temperature` three times between two
`clock_gettime(CLOCK_BOOTTIME)` calls; the attribute prints the robust clock
(`inv_mpu_core.c:1164`). The narrowest bracket (under 20 ms) gives the offset: its midpoint minus
the robust value. Until one is measured, the newest record of the first read is taken as "now". The
light sensor's evdev is switched to `CLOCK_MONOTONIC` with `EVIOCSCLOCKID` (`evdev.c:764`; this
kernel has no boottime option there), or left on `CLOCK_REALTIME` if that fails, and converted with
both clocks read at once. Every sensor's stamps are clamped to never be after now and to increase
strictly.

### Calibration

- Magnetometer (hard iron): samples at least 5 µT from every kept one go into a ring of 48. Every 8
  new points, with at least 24, a linear least-squares sphere fit runs. Its center becomes the bias
  only if the radius is 15-100 µT (the geomagnetic field is about 25-65 µT), the RMS residual at
  most max(1.5 µT, 10 % of the radius), and the points not flat: their spread along the thinnest
  direction must be at least 12 % of the radius (a head turning about one axis traces a circle,
  which fixes no center). A new fit reports accuracy HIGH; until then UNRELIABLE.
  The bias is saved as text (`glass-magcal 1 <bx> <by> <bz> <radius>`) to
  `/data/misc/sensors/glass_magcal.txt` through a temporary file and `rename`, at most every
  5 minutes, and when the compass turns off or the HAL closes. A saved bias loads as accuracy MEDIUM.
  The file is rejected if malformed or out of range. `init.omap4430.rc` creates the directory
  (0770 system system, `docs/hardware-facts.md` Directories).
- Gyroscope (`GLASS_GYRO_BIAS_AT_REST` in `sensors/sensors.c`, on): in each 2 s window of at least 20
  samples, if every axis' standard deviation is at most 0.004 rad/s (about 4 LSB) and the mean is
  under 0.35 rad/s, the mean becomes the bias. GYROSCOPE reports raw minus bias (accuracy MEDIUM
  until a first bias, then HIGH); GYROSCOPE_UNCALIBRATED reports raw and the bias. The bias is not
  saved. The framework's fusion also estimates its own gyro drift. With 20 samples per 2 s window it
  needs a gyro rate of 10 Hz or more: at SENSOR_DELAY_NORMAL (5 Hz) with nothing faster enabled it
  never runs, and GYROSCOPE stays raw (accuracy MEDIUM).
- Accelerometer: none.

### Light

The ALS input device is found by name (`ltr506_als`, `ltr506als.c:1777`) under `/sys/class/input`,
which gives both its `inputN` (for `als_enable`) and its `eventM` node; input2/input3 are not
assumed (spec §6). `als_enable` is written on activate and deactivate, through
`/sys/class/input/inputN/als_enable` or, if that fails, the I2C device's
`/sys/devices/platform/omap_i2c.4/i2c-4/4-003a/als_enable` (same handler). The value is reported as
lux without scaling, as stock (spec §6): no lux factor is known yet. One event per `SYN_REPORT` that
follows an `ABS_MISC`.

The input core drops an `ABS_MISC` value equal to the previous one (`input.c:198-202`), so an
unchanged light level would give no event after `activate`. The HAL therefore queues one event on
activate, from the input device's `als_adc` (ADC counts, the events' unit, mode 0666,
`ltr506als.c:1100-1113`), or, if that read fails, from the last value the input device reported
(`EVIOCGABS(ABS_MISC)`, when non-zero). On the part ID that needs the proximity workaround (0x90,
`ltr506als.c:1860-1861`) the driver reports channel 1 in events (`ltr506als.c:527-529`) while
`als_adc` reads the combined value, so that first event may differ in scale from the next ones (device
checklist).

### Permissions

The system server (uid system, groups including input, 1004, per ZygoteInit `--setgroups` at both
tags) opens everything:
- `/dev/iio:device0` 0664 system (ueventd); every IIO attribute the HAL writes is chowned to system
  0664 in `init.omap4430.rc` (`power_state`, `buffer/*`, `dmp_on`, the three `*_enable`,
  `in_anglvel_scale`, `in_accel_scale`, `sampling_frequency`, `scan_elements/*_en`,
  `trigger/current_trigger`); the ones it only reads (matrices, `*_scale`, `firmware_loaded`,
  `temperature`, `scan_elements/*_type`, `name`, `trigger0/name`) are readable.
- `/dev/input/event*` is 0660 root input (AOSP `ueventd.rc`).
- `als_enable` is 0666 in the driver (`ltr506als.c:1186`), so the hard-coded input2 chowns in
  `init.omap4430.rc` are not needed for it (they stay, as stock facts).
- New: `mkdir /data/misc/sensors 0770 system system` `on boot`.

The paths in `init.omap4430.rc` still say `iio:device0`; if the IIO device ever gets another
number, those chowns need the new path (device checklist).

### Proximity

Left out, as on stock: the `ltr506_ps` device reports raw ADC 0..4095 (`ltr506als.c:428-437`) and
would need a near/far threshold measured on a device, and Glass has no use for a face-down screen
switch. `permissions/hardware.xml` does not declare it.

### Departures from the spec

- Uncalibrated magnetometer and gyroscope are listed (spec §7 "optional"): they share the hardware
  and cost nothing.
- `fifoMaxEventCount` is 0, not the stock 100: nothing batches.
- Compass alone keeps the accel engine on but out of the FIFO (`accl_enable` 1, accel scan elements
  0). The spec's sequence does not cover the compass alone; the compass is read from the MPU's
  data-ready interrupt, and whether that runs with both engines in standby is not known. To check on
  a device.
- Rate: rounded up instead of truncated (stock `1e9 / delay`), and moved to 200 Hz when the compass is
  on and the rate is above 100 Hz (see Data path).
- The format byte must equal the enabled set, where stock only rejects the top four bits. It drops
  records of an earlier layout of the same size after a reconfiguration.
- `in_timestamp_en` gets `1` (stock writes "480", spec §5.1; any non-zero enables). `dmp_event_int_on`,
  `dmp_output_rate` and the GLU/TAP attributes are not written: no DMP.
- ALS: enabled only while the light sensor is active (stock enables it at construction and keeps it
  on, spec §6). Events carry `CLOCK_BOOTTIME`, not the stock `CLOCK_REALTIME`, and IIO events too
  (spec §4.5 recommends this). Activating the light sensor queues one event with the current value
  (see Light); stock waits for the next change.
- Names and vendors are the parts' ("MPU-9150 Accelerometer", "InvenSense"; "AK8975
  Magnetometer", "AKM"; "LTR-506ALS Light", "LiteOn"), not the stock "MPL ..." names. Power figures
  are the stock ones, including 0.0 mA for the accelerometer.
- The HAL writes `power_state` 0 and `buffer/enable` 0 at open, to start from a known state.

### Owner decisions

- GLU (head-wake "look up") and TAP: they need the proprietary DMP image (spec §5.4), which this tree
  must not ship. Options: leave them out (current), let a user load their own image at runtime, or
  write a software look-up detector on the accelerometer later.
- Light: a lux factor needs a measurement against a lux meter (spec §9 question 4).
- Gyro bias at rest is on; set `GLASS_GYRO_BIAS_AT_REST` to 0 to report raw gyro data. It needs a
  gyro rate of 10 Hz or more and never runs at SENSOR_DELAY_NORMAL. Options: keep it as is, lower
  `SNS_GYRO_REST_MIN_SAMPLES` (a 5 Hz window has 10 samples, a noisier estimate), or save the bias
  like the compass's so a slow client gets the last one found.

### Sensors: what a 4.4 branch needs

Nothing in the sources: the same `sensors/` builds against 4.4 headers (`sensor_t` without the 6.0
fields), and CI builds it at API level 19. 4.4 sets no time base for sensor timestamps (the
`elapsedRealtimeNanos` rule came with 5.0); `CLOCK_BOOTTIME` is a valid choice there and stays
consistent across suspend, but a 4.4 app that compares stamps with `System.nanoTime()`
(`CLOCK_MONOTONIC`) sees them drift apart after a suspend (device checklist). The 4.4 tree needs the
`PRODUCT_PACKAGES` entry, the `/data/misc/sensors` mkdir in `init.omap4430.rc`, and the existing IIO
chowns, which the stock 4.4 ramdisk already has. On 4.4, `SensorService` on a 1.1 HAL uses `batch`
and `flush` the same way (the stock HAL was 1.1 on 4.4). If a 4.4 build still installs the stock MPL
libraries for another reason, they stay unused.

## Audio HAL (`audio/`, `audio.primary.glass_1`)

Written from [docs/specs/audio.md](specs/audio.md) (a clean-room specification of the stock HAL,
cited below as "spec") and the GPL kernel. It replaces the stock `audio.primary.glass_1.so` and
`audio_policy.conf`, which are no longer in `proprietary-files.txt`. The stock `libtinyalsa.so` and
`libglassalsa.so` are not used: the HAL links AOSP's own `libtinyalsa` and `libaudioutils`. It is
new code in the usual layout of AOSP tinyalsa HALs (as in device/samsung/tuna/audio); no code is
copied from tuna or any other HAL, so there is no third-party copyright notice to carry.

- `audio_hw.c`: the `audio_hw_device`, device API 2.0 (`AUDIO_DEVICE_API_VERSION_2_0`), module API
  0.1. One primary output and one input; no voice call (`set_mode` and `set_voice_volume` do
  nothing) and no Bluetooth SCO, as on stock (spec 2, 5.3). Master volume is left to AudioFlinger.
- `audio_logic.c`: the mixer tables, path choice, parameter parsing, pcm parameters, down-mix and
  sysfs access, tested on the host (see CI).
- `audio_policy.conf`: our own file in AOSP format from the facts of spec section 3, installed
  to `/system/etc`.

| What | Value | Source |
|---|---|---|
| Card | 0, "Notle" (`omap-abe-twl6040`); the HAL refuses to open unless the 10 controls of spec 4.1 exist | spec 1, 4.1 |
| Mixer at open | the table of spec 4.2 in order, every channel; `DL1 PDM Switch` once, not twice; then the earpiece path | spec 4.2; `omap-abe-mixer.c`, `twl6040.c` |
| Playback | device 0 (ABE Multimedia FE), `PCM_OUT \| PCM_MONOTONIC`, S16_LE stereo 48000 Hz, 4 periods of 1056 frames (88 ms), `pcm_write` | spec 5.1, 7 |
| Earpiece path (no earbud) | `Earphone Playback Switch` 1, `Headset Left/Right Playback` "Off": the bone conduction transducer on the TWL6040 EP output | spec 4.3 |
| Headset path (earbud on USB) | `Earphone Playback Switch` 0, `Headset Left/Right Playback` "HS DAC" | spec 4.3; `twl6040.c:639-641` |
| Earbud detection | read `/sys/devices/platform/usb_mux.0/headset` at each output start and routing change: `Mono`, `Stereo`, else none | spec 4.3; `notle-usb-mux.c:543-570` |
| Mux | `mux_mode` ← `audio` when an earbud is detected | spec 4.3, 7; `notle-usb-mux.c:417-447` |
| Plug monitoring | `id_monitor` ← `1` at output start, `0` at standby | spec 4.3; `notle-usb-mux.c:460-478` |
| Down-mix | (L+R)/2 on both channels for the transducer and a mono earbud; a stereo earbud gets stereo | spec 6, 7 |
| Capture | device 2 (ABE Voice FE, VX_UL ← `MUX_VX0/1` ← DMIC0 L/R), `PCM_IN`, always 2 channels: 48000 Hz (1056 x 4) above 16 kHz, 16000 Hz (384 x 2) from 8001, 8000 Hz (192 x 2) below; `audio.in.rate` picks the config instead of the requested rate | spec 5.2 |
| Accepted capture | 8000, 11025, 16000, 22050, 24000, 32000, 44100, 48000 Hz; 16-bit; 1 or 2 channels (mono is (L+R)/2) | spec 5.2 |
| Resampling | audio_utils `create_resampler` (speex inside libaudioutils), quality 4, when the requested rate differs from the config | spec 5.2; system/media `audio_utils/resampler.c` at both tags |
| Capture gains | at each capture start, `DMIC1 UL Volume` (both channels) ← `audio.dmic1gain` or 146, `AUDUL Voice UL Volume` ← `audio.voicegain` or 120; `voice_volume=<n>` in `set_parameters` sets the second | spec 4.4, 5.3 |
| Start-up mute | the first 50 ms after each capture start are zeros; mic mute zeros everything | spec 5.2 |
| Glasshub pause | `pause_for_audio` ← `1` when capture starts with any source but `VOICE_RECOGNITION`, `0` at standby | spec 6 (`persist.audio.hammer` default 1); `glasshub.c:1251-1318` |
| `get_parameters` | `voice_volume`, `output_type` (1 none, 2 mono, 3 stereo, read again from sysfs) | spec 5.3 |

Rule 6: the HAL writes no TWL6040 power or voltage setting other than the stock enum values of spec
4.2 (`TWL6040 Power Mode` and `PLL Selection` "Low-Power", `Headset Power Mode`
"High-Performance"). It never touches the handsfree (HF) outputs. `Earphone Playback Volume` is 15
(+6 dB, also the control's maximum) and `Headset Playback Volume` 12 (−6 dB), the stock values;
`glass_mixer_set_int` lowers any higher value to those (`GLASS_EARPHONE_VOLUME_MAX`,
`GLASS_HEADSET_VOLUME_MAX`), and the host tests check both. Loudness is AudioFlinger's software
volume only.

Permissions: the HAL runs in mediaserver (uid media, group audio) on 4.4 and 6.0. `mux_mode` and
`pause_for_audio` are chowned to media and 0664 in `init.omap4430.rc` (`docs/hardware-facts.md`,
Sysfs, checked by `tests/test_audio_logic.py`). `id_monitor` is `S_IWUGO | S_IRUGO` in the kernel
(`notle-usb-mux.c:573`) and `headset` is 0444, so neither needs an init line. `/dev/snd/*` gets AOSP's
`ueventd.rc` default (0660 system:audio). SELinux is permissive. No rootdir change was needed.

### Where the HAL differs from the spec's section 7

- Output path: the earbud detection decides when `headset` can be read, as on stock, and
  AudioPolicy's routing (`WIRED_HEADSET`/`WIRED_HEADPHONE` vs the rest) decides only when it cannot.
  The kernel reports plugging to the `h2w` switch from the same measurement, so the two agree
  once the switch has reported.
- `id_monitor` is written `1` at every output start, as stock does, not only when an earbud is
  detected (section 7 suggests the latter for both files). Without it the kernel does not poll, the
  `h2w` switch does not report an earbud plugged in during playback, and the path would only follow
  at the next start. `mux_mode` is written only when an earbud is detected, as section 7 says.
- Not implemented: the deep-buffer output and `persist.lab.lp_aud` (spec 5.1), the voice-FE output
  for `audio.out.rate` ≤ 16000, and putting the input in standby when the output starts (spec 5.1,
  last row), which served the stock echo reference. The stock `libglassalsa` processing (spec 6) has
  no open replacement yet: no bone-conduction equalisation, wind or noise reduction. AOSP
  pre-processing effects can be attached by the framework (`add_audio_effect` accepts them) if an
  `audio_effects.conf` with a `pre_processing` section is installed; this tree installs none yet.

### Audio policy

`audio/audio_policy.conf` keeps the stock devices: attached EARPIECE and SPEAKER (default SPEAKER),
BUILTIN_MIC and BACK_MIC; primary output devices EARPIECE, SPEAKER, WIRED_HEADSET, WIRED_HEADPHONE
and ALL_SCO; primary input 8000-48000 Hz, IN_MONO, IN_STEREO and IN_FRONT_BACK from BUILTIN_MIC,
BLUETOOTH_SCO_HEADSET, WIRED_HEADSET and BACK_MIC; the usb module's two 44100 Hz outputs. Changes:
- the primary output is 48000 Hz, not 44100, since the HAL plays at 48000 (spec 7);
- `a2dp` (AOSP `audio.a2dp.default`, 44100 Hz, ALL_A2DP) and `r_submix` (AOSP
  `audio.r_submix.default`, 48000 Hz, for screen casting) modules are added; stock has neither.

The format is the same for 4.4's legacy `audio_policy.default` and 6.0's AudioPolicyManager, which
both read `/system/etc/audio_policy.conf` with cutils' config parser (`#` comments are allowed).

### What a 4.4 branch needs

Power, lights and sensors build unchanged on a 4.4 tree: same sources, same `Android.mk`. For
audio, see the audio section above.
- Power: nothing beyond the tree's `PRODUCT_PACKAGES` line. 4.4's `init.rc` already gives system the
  governor files (above), and 4.4's PowerManagerService uses power API 0.2. `hardware/ti/omap4` on a 4.4
  tree may add its own `power.omap4`; it stays unused for the same lookup-order reason.
- Lights: nothing beyond `PRODUCT_PACKAGES` and the `brightness` chown in `init.omap4430.rc`, which the
  stock 4.4 ramdisk already has. 4.4's LightsService handles a NULL device for absent lights as 6.0's does.
- Sensors: nothing beyond `PRODUCT_PACKAGES` and the `/data/misc/sensors` mkdir in
  `init.omap4430.rc` (see "Sensors: what a 4.4 branch needs" above).
- SELinux: 4.4 on this tree is permissive too. Enforcing would need policy for system_server to write
  `sysfs` on both trees.
