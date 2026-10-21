<!-- SPDX-License-Identifier: Apache-2.0 -->
# Sensors HAL specification (Glass Explorer Edition, XE24)

> **Provenance.** This specification was written by a separate reverse-engineering agent under
> the clean-room rule: it states facts (paths, values, sequences, where each was observed) and
> contains no code from the stock binaries. The implementer of `sensors/` read only this file, the
> GPL kernel source and public AOSP code. It is copied here unchanged apart from this note; where
> the implementation departs from it, `docs/hals.md` (Sensors HAL, "Departures from the spec")
> says so and why.

Clean-room specification for an open `sensors.glass_1` HAL. It lists interfaces, paths, values and
sequences only. Every fact says where it was observed:

- **kernel**: GPL source, branch `glass-omap-xrx13b-stable-3.4.113` of OpenMyGlass/kernel-omap, `file:line`.
- **rc**: stock XE24 ramdisk `init.omap4430.rc` / `ueventd.omap4430.rc`.
- **blob**: stock XE24 binary, with the function's link-time address (from the exported dynamic symbol table)
  or a file offset. Stock binaries (from `/system`): `lib/hw/sensors.glass_1.so` (5360 B, sha256 `e1d0c82a…0794`),
  `lib/libinvensense_hal.so` (46968 B, sha256 `7bd56545…9cad`), `lib/libmllite.so` (91100 B, sha256
  `1ea9b285…da6d`), `lib/libmplmpu.so` (171088 B, sha256 `ea630db6…8da5`), `lib/libglass_sensorsutils.so`
  (13516 B, sha256 `dccca355…6e13`).

Nothing below is code from those binaries. Where the text says "the stock HAL does X" it describes behaviour.

## 1. Hardware summary

| Part | Bus / address | Kernel driver | Userspace node | Source |
|---|---|---|---|---|
| InvenSense MPU-9150 (gyro + accel + AK8975 compass on the MPU aux bus) | I2C bus 4, 0x68; compass at aux address 0x0C | `drivers/staging/iio/imu/inv_mpu/` (`inv-mpu-iio`), `CONFIG_INV_MPU_IIO=y` | `/dev/iio:device0`, `/sys/bus/iio/devices/iio:device0/`, `trigger0` | kernel `arch/arm/mach-omap2/board-notle.c:1608-1622, 1742-1745`; `notle_defconfig:2621` |
| LiteOn LTR-506ALS ambient light + proximity | I2C bus 4, 0x3a | `drivers/input/misc/ltr506als.c`, `CONFIG_INPUT_LTR506ALS=y` | input devices `ltr506_als` and `ltr506_ps`; sysfs under `4-003a` and under each input device | kernel `board-notle.c:1625-1660, 1746-1751`; `notle_defconfig:1513` |

Notes:

- The legacy InvenSense misc driver (`/dev/mpu`, `/dev/mpuirq`, `/dev/accelirq`, `/dev/compassirq`,
  `/dev/timerirq`) is **not built** (`CONFIG_INV_MPU is not set`, `notle_defconfig:1226`), although
  `ueventd.omap4430.rc:14-18` still lists those nodes. Only the IIO driver exists. An open HAL must not depend on
  the misc nodes.
- MPU interrupt: GPIO "MPU9000_INT" (board-specific number via `notle_get_gpio(GPIO_MPU9000_INT_INDEX)`), set as
  a wake source (`board-notle.c:2113-2143`). LTR-506 interrupt: GPIO `PROX_INT` (`board-notle.c:1807-1826`).
- `init.omap4430.rc:36-37` also opens `/dev/si114x` for a "gaze service"; that is not part of the sensors HAL
  (the eye-facing sensor belongs to the glasshub MCU at 4-0035; out of scope here).

## 2. Stock HAL module and device declaration (API versions)

| Item | Value | Where |
|---|---|---|
| `hw_module_t.tag` | `HARDWARE_MODULE_TAG` | blob `sensors.glass_1.so`, symbol `HMI` @0x200c |
| `module_api_version` | 0x0001 (sensors module API 0.1) | same |
| `hal_api_version` | 0 | same |
| `id` / `name` / `author` | `"sensors"` / `"Invensense module"` / `"Invensense Inc."` | same |
| Device `common.version` | 0x01010001 = `SENSORS_DEVICE_API_VERSION_1_1` (KitKat level; `batch` and `flush` present) | blob `sensors.glass_1.so`, device-open function @0x0b18 (constant at 0x0b74) |
| `batch()` | accepts only `timeout == 0` (else `-EINVAL`); the `SENSORS_BATCH_DRY_RUN` flag returns 0 without action; otherwise behaves as `setDelay` | blob `sensors.glass_1.so` @0x0c48 |
| Vendor extension | one extra function pointer after `flush` (first `reserved_procs` slot) is a Glass `setParameters(handle, const char*)`; only handle 7 (GLU) is accepted, others return `-EINVAL`; the shim returns `-ENOSYS` for handle 9 | blob `sensors.glass_1.so` @0x0c72; `libinvensense_hal.so` `MPLSensor::setParameters` @0x71c0 |

`sensor_t` in the stock build is 68 bytes (KitKat layout with `fifoReservedEventCount`, `fifoMaxEventCount`,
6 reserved pointers); the shim allocates a 680-byte list (10 entries) (blob `sensors.glass_1.so` @0x0a28).

For the open HAL: declaring device API 1.0 or 1.1 against KitKat/Marshmallow legacy headers is possible; on
6.0.1 a 1.1 device without `stringType`/`requiredPermission` fills still works because those fields are
zero-initialised. Note the KitKat vs. Marshmallow `sensor_t` size difference when building one source for both.

## 3. Sensor list exposed by the stock HAL

The stock HAL exposes 10 sensors. Template values come from a static table (blob `libinvensense_hal.so`,
`.data` at link address 0xc004, 9 × 68 B; light entry at 0xc268), then per-chip values are overwritten by
`MPLSensor::fillGyro` @0x69f4, `fillAccel` @0x6928, `CompassSensor::fillList` @0x8e7c, `fillOrientation` @0x6ae8,
`fillRV` @0x6ab0, `fillGravity` @0x6b24, `fillLinearAccel` @0x6b60, called from `MPLSensor::populateSensorList`
@0x6b94. The chip name used to choose the values is read by `inv_get_chip_name` (libmllite) from the IIO
`name` attribute; on Glass the kernel names the device `mpu9150` (board info `I2C_BOARD_INFO("mpu9150", 0x68)`).

Final values for the MPU9150 branch:

| handle | name | vendor | ver | type | maxRange | resolution | power (mA) | minDelay (µs) | fifoMax |
|---|---|---|---|---|---|---|---|---|---|
| 0 | MPL Gyroscope | Invensense | 1 | 4 GYROSCOPE | 34.906586 rad/s | 0.0010652636 | 5.5 | 1000 | 100 |
| 1 | MPL Accelerometer | Invensense | 1 | 1 ACCELEROMETER | 19.6133 m/s² | 0.0392266 | 0.0 | 1000 | 100 |
| 2 | MPL Magnetic Field | Invensense | 1 | 2 MAGNETIC_FIELD | 9830.0 µT | 0.285 | 10.0 | 10000 | 100 |
| 3 | MPL Orientation | Invensense | 1 | 3 ORIENTATION | 360.0 | 0.00001 | 15.5 (sum of 0–2) | 5000 | 100 |
| 4 | MPL Rotation Vector | Invensense | 1 | 11 ROTATION_VECTOR | 1.0 | 0.00001 | 15.5 | 5000 | 100 |
| 5 | MPL Linear Acceleration | Invensense | 1 | 10 LINEAR_ACCELERATION | 19.6133 | 0.0392266 | 15.5 | 5000 | 100 |
| 6 | MPL Gravity | Invensense | 1 | 9 GRAVITY | 9.81 | 0.00001 | 15.5 | 5000 | 100 |
| 7 | MPL GLU | Invensense | 1 | 0x110000 (vendor) | 100.0 | 1.0 | 1.1 | 0 | 0 |
| 8 | MPL TAP | Invensense | 1 | 0x110001 (vendor) | 100.0 | 1.0 | 1.1 | 0 | 0 |
| 9 | LTR-506ALS Light sensor | LiteOn | 1 | 5 LIGHT | 64000.0 | 1.0 | 0.75 | 0 | 100 |

Other chip branches in the same functions (not used on Glass): MPU6050 uses minDelay 5000 µs for gyro/accel;
MPU3050 gyro power 6.1 mA; BMA250 accel resolution 0.038344, power 0.139 mA. The 9830.0 µT "maxRange" of the
compass is the kernel's AK8975 scale constant, not a physical range (AK8975 full scale is about ±1229 µT); an
open HAL should publish a physical range (for example 1200 µT) instead.

Fields `fifoReservedEventCount` = 0 everywhere (no hardware batching).

## 4. Kernel interface: InvenSense IIO driver (`inv-mpu-iio`)

### 4.1 Device and attributes

- IIO device `iio:device0`, name `mpu9150`, trigger `trigger0`; character device `/dev/iio:device0`
  (kernel `inv_mpu_core.c:1899-1990`; rc `init.omap4430.rc:44-140` sets owner `system` and mode 0664 on the nodes).
- All driver attributes appear flat in `/sys/bus/iio/devices/iio:device0/` (the `"mpu"` group name at
  `inv_mpu_core.c:1590` is not used as a subdirectory: the 3.4 staging IIO core copies `info->attrs` into its own
  unnamed group, `drivers/staging/iio/industrialio-core.c:670-715`).
- Attribute set for MPU9150 (`inv_mpu_core.c:1528-1578`): `gyro_enable`, `temperature`, `power_state`,
  `reg_dump`, `self_test`, `key`, `gyro_matrix`, `sampling_frequency`, `sampling_frequency_available`
  (`"10 20 50 100 200 500"`), `accl_enable`, `accl_matrix`, `firmware_loaded`, `glu_*` (10 attributes),
  `dmp_on`, `dmp_int_on`, `dmp_event_int_on`, `dmp_output_rate`, `orientation_on`, `quaternion_on`,
  `display_orientation_on`, `tap_on`, `tap_time`, `tap_min_count`, `tap_threshold`, `event_glu`,
  `event_orientation`, `event_display_orientation`, `event_tap`, `compass_matrix`, `compass_enable`, and the binary
  attribute `dmp_firmware` (size 4096, `inv_mpu_core.c:1849-1858`).
- Channel attributes (from the channel table `inv_mpu_core.c:1425-1444` and core naming): `in_anglvel_{x,y,z}_raw`,
  `in_anglvel_{x,y,z}_calibbias`, `in_anglvel_scale`; `in_accel_{x,y,z}_raw`, `in_accel_{x,y,z}_calibbias`,
  `in_accel_{x,y,z}_offset`, `in_accel_scale`; `in_magn_{x,y,z}_raw`, `in_magn_scale`; `in_quaternion_{r,x,y,z}_raw`.
  `*_raw` returns the last sample cached by the buffer path, not a fresh register read (`inv_mpu_core.c:363-385`).
- The stock rc also chowns `lpa_mode`, `lpa_freq`, `accl_bias`, `glu_outliner` (sic) which do **not** exist in this
  kernel; harmless.
- Writes to most configuration attributes return `-EPERM` while `power_state` is 0 or while the buffer is enabled
  (`check_enable`, `inv_mpu_core.c:546-549`, used by `mpu_write_raw`, `inv_fifo_rate_store`, `inv_attr_store`).
  So: configure, then enable the buffer; disable the buffer before reconfiguring.

### 4.2 Values, scales and units

| Attribute | Read | Write | Source |
|---|---|---|---|
| `in_anglvel_scale` | full-scale in °/s: 250, 500, 1000 or 2000 | an **index** 0..3 (0=250, 1=500, 2=1000, 3=2000 °/s) | `inv_mpu_core.c:387-398, 466-488, 564-569` |
| `in_accel_scale` | full-scale in g: 2, 4, 8 or 16 | an **index** 0..3 | `inv_mpu_core.c:399-404, 493-504` |
| `in_magn_scale` | 9830 × 2¹⁵ = 322109440 for AK8975, i.e. µT per LSB in Q30 (322109440 / 2³⁰ = 0.29999 µT/LSB) | ignored for AK8975 | `inv_mpu_core.c:323-339`, `inv_mpu_iio.h:370` |
| `sampling_frequency` | current FIFO rate in Hz | integer Hz, 4..1000; sets SMPLRT_DIV = 1000/rate − 1 and a DLPF chosen as the largest of 188/98/42/20/10/5 Hz not above rate/2 | `inv_mpu_core.c:602-675` |
| `power_state` | 1 = on | 1/0 | `inv_mpu_core.c:689-700`, `set_power_itg` `:230-262` |
| `gyro_matrix`, `accl_matrix`, `compass_matrix` | nine comma-separated signed integers, row-major | read-only | `inv_mpu_core.c:1049-1070` |

Power-on defaults after probe (`inv_init_config`, `inv_mpu_core.c:270-317`): gyro ±2000 °/s, DLPF 42 Hz, FIFO
rate 50 Hz, accel ±2 g, gyro and accel enabled, compass not enabled, chip put to sleep at the end of probe
(`power_state` = 0).

Conversion to Android units (raw values are already normalised by the kernel: accel samples are multiplied by
the silicon-revision factor `multi`, `inv_mpu_ring.c:566-571`, `inv_mpu_misc.c:433-438`; compass samples are
already corrected by the AK8975 fuse-ROM sensitivity, `inv_mpu_ring.c:629-646`):

- gyro rad/s = raw × FSR_dps / 32768 × π/180 (at 2000 °/s: 0.0010652645 rad/s per LSB).
- accel m/s² = raw × FSR_g / 32768 × 9.80665 (at 2 g: 0.000598 m/s² per LSB).
- magnetic µT = raw × in_magn_scale / 2³⁰.

The stock HAL reads `in_anglvel_scale` and `in_accel_scale` once at construction ("%ld" and "%d") to feed the MPL
(defaults 2000 and 2 if unreadable), and passes `in_magn_scale` verbatim as the compass sensitivity
(blob `libinvensense_hal.so` `MPLSensor::MPLSensor` @0x7ea8, `CompassSensor::getSensitivity` @0x8db8,
`MPLSensor::inv_set_device_properties` @0x7de0).

### 4.3 Mounting matrices (board file)

From `board-notle.c:1608-1622` (`mpu9150_notle_data`), exported via sysfs:

| Sensor | Matrix (row-major) | sysfs |
|---|---|---|
| gyro and accel | `0,1,0, 0,0,1, 1,0,0` | `gyro_matrix`, `accl_matrix` |
| compass (AK8975) | `1,0,0, 0,0,-1, 0,1,0` | `compass_matrix` |

Other platform data: `int_config` 0x10, `level_shifter` 1, `secondary_i2c_addr` 0x0C, a 16-byte `key` (exposed by
the `key` attribute; used only by the proprietary MPL).

The stock HAL reads the three matrices from sysfs at start-up and hands them to the MPL
(`MPLSensor::inv_get_sensors_orientation` @0x52e4, `CompassSensor::CompassSensor` @0x8fc0, MPL calls
`inv_set_{gyro,accel,compass}_orientation_and_scale` from `inv_set_device_properties` @0x7de0). It applies no other
rotation. InvenSense's convention is body_vector = M × chip_vector with M row-major (the same convention as the
public InvenSense AOSP code, `hardware/invensense`). The open HAL should apply the sysfs matrices the same way and
verify the result on a device (section 9).

### 4.4 Scan elements and buffer record layout

Scan elements (`/sys/bus/iio/devices/iio:device0/scan_elements/`): `in_quaternion_{r,x,y,z}_en` (index 0–3,
type `le:s32/32>>0`), `in_anglvel_{x,y,z}_en` (4–6, `le:s16/16>>0`), `in_accel_{x,y,z}_en` (7–9, `le:s16/16>>0`),
`in_magn_{x,y,z}_en` (10–12, `le:s16/16>>0`), `in_timestamp_en` (13, `le:s64/64>>0`)
(`inv_mpu_iio.h` enum `inv_mpu_scan`; `inv_mpu_core.c:1384-1444`; type string format
`industrialio-buffer.c:98-103`; endianness is CPU = little).

Record layout written by the driver (`inv_report_gyro_accl_compass`, `inv_mpu_ring.c:541-690`):

1. Quaternion (4 × s32), only if DMP and `quaternion_on`.
2. Gyro x, y, z (s16 each, only the enabled axes).
3. Accel x, y, z (s16).
4. Magnetometer x, y, z (s16).
5. Padding to the next 8-byte boundary. **The last padding byte (offset `ALIGN(n,8) − 1`) carries a
   "data format" bit mask** whenever the record is not quaternion-only: 0x01 quaternion, 0x02 gyro, 0x04 accel,
   0x08 compass (`inv_mpu_ring.c:536-539, 683-685`).
6. Timestamp s64 at offset `ALIGN(n,8)` (`inv_mpu_ring.c:686-687`; `ring->scan_timestamp = true` by default,
   `:835`).

Example with all gyro, accel and magnetometer axes enabled: 18 data bytes, format byte at offset 23
(value 0x0E), timestamp at 24, record size 32 bytes. Enable all three axes of a sensor together: with a partial
axis set the data length can be a multiple of 8, and the format byte then overwrites the last data byte.

Compass specifics (`inv_mpu_ring.c:612-656`):

- The compass is read through the MPU I2C master (slave 0 reads 8 bytes from AK8975 register 0x02; slave 1
  triggers single-measurement mode each sample) (`inv_setup_compass`, `inv_mpu_core.c:1604-1700`).
- At FIFO rates above 100 Hz the compass is read only every `10 × rate / 1000` samples; the records in between
  carry **zeros** in the magnetometer fields. The first record after every FIFO reset also carries zeros. A sample
  is also left at zero when the AK8975 status reports not-ready or overflow.
- The stock HAL treats an all-zero magnetometer triple as "no new compass sample" (blob `libinvensense_hal.so`
  `MPLSensor::readEvents` @0x6500). The open HAL must do the same.

Buffer control (`industrialio-buffer.c`, `inv_mpu_ring.c:807-820`): `buffer/length` (stock HAL writes 480),
`buffer/enable` (1 = start: resets FIFO, enables data-ready interrupt and the selected FIFO sources; 0 = stop),
`trigger/current_trigger` (stock HAL copies `trigger0/name` into it). Reads on `/dev/iio:device0` return whole
records.

Interrupt and timestamp path: a hard IRQ handler stores one timestamp per data-ready interrupt, filling in up to
5 missing slots at the nominal period when interrupts were late (non-DMP mode only); the threaded handler drains
the hardware FIFO and pairs each record with a stored timestamp; on FIFO count mismatch, odd count or more than
500 bytes it flushes both FIFOs (`inv_mpu_ring.c:348-376, 696-790`).

### 4.5 Timestamp clock

`get_time_ns()` returns `read_robust_clock()` (`inv_mpu_core.c:52-55`), implemented in
`arch/arm/plat-omap/counter_32k.c:133-161`: nanoseconds accumulated from the OMAP 32 kHz sync counter
(resolution 30.5 µs), wrap-safe, and it **keeps counting through suspend**. Its zero is the counter start, not
the kernel's CLOCK_MONOTONIC/CLOCK_BOOTTIME zero.

Two attributes print the same clock and can be used to measure the offset to CLOCK_BOOTTIME from userspace:
`iio:device0/temperature` prints `"<temperature> <robust_ns>"` (`inv_mpu_core.c:1137-1166`; works only while
powered) and glasshub `4-0035/current_timestamp` prints `<robust_ns>` (`drivers/input/misc/glasshub.c:2037-2040`).

The stock HAL passes the IIO record timestamp to the MPL unchanged, so its gyro/accel/compass event timestamps are
robust-clock values (blob `MPLSensor::readEvents` @0x6500). Its light events use the `input_event` time
(CLOCK_REALTIME in this kernel) (`LightSensor::readEvents` @0x8828), and its gesture events use
`clock_gettime(CLOCK_REALTIME)` (`MPLSensor::readDmpGestureEvents` @0x7c28). The open HAL should report all
events in CLOCK_BOOTTIME (`elapsedRealtimeNanos`, required since Android 5): add a measured offset to the IIO
timestamps, and for the input device call `EVIOCSCLOCKID` with CLOCK_MONOTONIC (supported:
`include/linux/input.h:160`; this evdev has no CLOCK_BOOTTIME option) and convert.

### 4.6 Raw sensors without DMP firmware

The driver supports a pure raw mode: with `dmp_on` = 0 the FIFO receives gyro and accel register data
directly and compass data comes from the aux I2C master (`reset_fifo_itg`, `inv_mpu_ring.c:162-262`, non-DMP
branch; README "Recommended sysfs entry setup sequence, 1. without DMP firmware",
`drivers/staging/iio/imu/inv_mpu/README:295-300`). No firmware upload is needed for gyro, accelerometer or
magnetometer.

Recommended open enable sequence (all paths relative to `/sys/bus/iio/devices/iio:device0/`):

1. `power_state` ← 1 (driver sleeps 100 ms and powers the enabled engines, `inv_mpu_core.c:230-262`).
2. `buffer/enable` ← 0 (ignore errors).
3. `dmp_on` ← 0 (only if `firmware_loaded` is 1).
4. `gyro_enable`, `accl_enable`, `compass_enable` ← 1/0 as needed (gyro on adds 30 ms settle,
   `inv_mpu_core.c:179-228`; compass enable writes I2C_MST_CTRL, `:1272-1295`).
5. Optionally `in_anglvel_scale` ← 3 and `in_accel_scale` ← 0 (index form), or keep the defaults.
6. `sampling_frequency` ← highest requested rate in Hz (4..1000; the stock HAL clamps any delay below 5 ms to 5 ms,
   i.e. at most 200 Hz).
7. `scan_elements/in_anglvel_{x,y,z}_en`, `in_accel_{x,y,z}_en`, `in_magn_{x,y,z}_en` ← 1/0 to match step 4;
   `scan_elements/in_timestamp_en` ← 1.
8. `trigger/current_trigger` ← contents of `/sys/bus/iio/devices/trigger0/name`; `buffer/length` ← 480.
9. `buffer/enable` ← 1, then `poll()`/`read()` `/dev/iio:device0`.

To change rate or the enabled set: `buffer/enable` ← 0, steps 4–7, `buffer/enable` ← 1. When nothing is
enabled: `buffer/enable` ← 0, `power_state` ← 0.

## 5. Stock HAL behaviour (facts the kernel does not give)

### 5.1 Initialisation (blob `libinvensense_hal.so` `MPLSensor::MPLSensor` @0x7ea8)

Order observed:

1. Build attribute paths from the IIO device directory (`MPLSensor::inv_init_sysfs_attributes` @0x6c30; the
   directory and trigger names come from libmllite `inv_get_sysfs_path`, `inv_get_iio_trigger_path`).
2. Read chip name (`inv_get_chip_name`).
3. `MPLSensor::enable_iio_sysfs` @0x4fa8: `scan_elements/in_timestamp_en` ← "480" (any non-zero enables);
   read `trigger0/name`, write it to `trigger/current_trigger`; `buffer/length` ← 480; open `/dev/iio:device0`
   (path from libmllite `inv_get_iio_device_node`) read-only.
4. `power_state` ← 1 if not already 1 (`MPLSensor::onPower` @0x5514), `buffer/enable` ← 0
   (`MPLSensor::masterEnable` @0x586c).
5. DMP load (`MPLSensor::loadDMP` @0x7364): if `firmware_loaded` reads 0, open `dmp_firmware` and write the DMP image
   (libmllite `inv_load_dmp`).
6. Open `temperature` (kept open, read periodically), read gyro FSR from `in_anglvel_scale` and accel FSR from
   `in_accel_scale`.
7. Start the MPL (`inv_init_mpl`, then enable: quaternion, in-use accel auto-calibration, fast no-motion gyro
   calibration, gyro temperature compensation, HAL outputs, vector compass calibration (sensitivity 3.0),
   compass bias with gyro, heading from gyro, magnetic disturbance detection, 9-axis fusion, no-gyro fusion,
   quaternion accuracy monitor; then `inv_start_mpl`) (`MPLSensor::inv_constructor_default_enable` @0x5174,
   `inv_constructor_init` @0x5284).
8. Load the MPL calibration file (`inv_load_calibration`).
9. Set MPL sample rates to 20 ms, orientation matrices and scales, DMP tap parameters
   (min count 1, threshold 100, time 100) (`inv_set_device_properties` @0x7de0).
10. Create the GLU parameter object and call `enableLookUpGesture(false)` @0x73f4 (section 5.4), which leaves
    `dmp_on` = 1, `dmp_int_on` = 1, `dmp_event_int_on` = 1, accel sampling 200 Hz and the buffer enabled.

The light sensor is opened and enabled at HAL construction (section 6).

### 5.2 Enable / setDelay

- Per-sensor enable is a bit mask over handles 0–8 (`MPLSensor::enable` @0x7a48). Handles 3–6 (fused) need gyro,
  accel and compass; handles 0, 1, 2 need only their own engine (`computeLocalSensorMask` @0x5be2).
- Enabling an engine writes `gyro_enable`/`accl_enable` and the three matching `scan_elements/*_en` files; the
  compass path writes `compass_enable` and `in_magn_{x,y,z}_en` (`enableGyro` @0x58c0, `enableAccel` @0x5a14,
  `CompassSensor::enable` @0x8ba0). Around changes it does `buffer/enable` ← 0 … ← 1 and writes
  `dmp_event_int_on` ← 0 when any data sensor is active, ← 1 when only gestures remain
  (`enableSensors` @0x7544, `enableEventInterrupt` @0x750c). With nothing enabled and DMP off it writes
  `power_state` ← 0.
- When an engine is turned off, the MPL calibration is saved (`storeCalibration` @0x5c3c → `inv_store_calibration`).
- `setDelay` @0x4bc0: delays below 5 000 000 ns are raised to 5 000 000 ns; the HAL keeps the smallest delay of the
  enabled sensors and writes `sampling_frequency` ← 1e9 / delay_ns (integer Hz), and `dmp_output_rate` with the
  same value (`update_delay` @0x6000, `getDmpRate` @0x5f98). The compass rate goes to the same
  `sampling_frequency` file (`CompassSensor::setDelay` @0x8b34).

### 5.3 Reading data

`MPLSensor::readEvents` @0x6500 reads at most 48 bytes (one record) from `/dev/iio:device0`, takes the timestamp
from the last 8 bytes and the format byte from the byte before them, rejects a format byte with any of the top four
bits set ("invalid data_format"), then reads gyro (if 0x02), accel (if 0x04) and compass (if 0x08, ignored when all
zero) in that order. On a gyro sample, if more than 0.5 s passed since the last reading, it reads `temperature`
("%ld %lld": temperature and robust-clock timestamp) and feeds the MPL temperature compensation.

Events are then produced by the MPL (`executeOnData` @0x5da8). The stock HAL thus uses **raw** gyro/accel/compass
from the IIO buffer and **software** fusion in the proprietary `libmplmpu.so` for orientation, rotation vector,
gravity and linear acceleration. It does not parse the DMP quaternion from the buffer (no 0x01 handling).

### 5.4 DMP usage

The DMP is used only for two Glass-specific vendor sensors:

| Handle | Name | Type | Kernel event attribute | Event content (stock) |
|---|---|---|---|---|
| 7 | MPL GLU ("look up" head gesture, used for head-wake) | 0x110000 | `event_glu` (sysfs_notify on DMP int status bit 0x10, `inv_mpu_ring.c:713-719`) | `data[0]` = 1.0 |
| 8 | MPL TAP | 0x110001 | `event_tap` | `data[0..1]` = the integer read from `event_tap` as int64 |

Both are read by `lseek(0)`, `read`, then `poll()` with POLLPRI on the attribute fd (shim
`sensors.glass_1.so` `sensors_poll_context_t` ctor @0x0a28 and `pollEvents` @0x0c8e; `readDmpGestureEvents`
@0x7c28). GLU tuning is written to `glu_outlier_max`, `glu_outlier_min`, `glu_level`, `glu_trigger`,
`glu_roll`, and the mode to `glu_version4_on` (`writeLookupGestureParams` @0x7108, `setLookupGestureMode`
@0x71d8). Parameters come from `/system/etc/dmp/glu_params.txt` (stock values: max outlier 1.5 g fraction,
min outlier 0.5, level angle 22.0°, trigger angle 30.0°, roll angle 10.0°) or from `setParameters(7, "key=value…")`;
conversion to DMP units (blob `libglass_sensorsutils.so` @0x1ce0–0x1e48): outlier = fraction² × 0.25 × 2³⁰;
level/trigger = cos²(angle) × 2³⁰; roll = cos²(90° − angle) × 2³⁰ (all rounded, negative → 0).

**No standard Android sensor needs the DMP.** The DMP image is proprietary: 2962 bytes, kernel-checked CRC32
0x130bd18d (`inv_mpu_iio.h:515-516`, `inv_mpu_misc.c:1751-1766`), embedded in `libmllite.so` at file offset 0xf70c,
sha256 `5c81c27e22356e291cfe8040ac92d212eab558b7df19ce43399c27d588e22e68`. An open HAL must not ship it. If the owner
wants GLU, a user could supply the image from their own device at runtime; that is a decision for the owner.

### 5.5 Calibration storage

The MPL stores its calibration state in `/data/inv_cal_data.bin` (string in blob `libmllite.so`;
`inv_load_calibration` / `inv_store_calibration`). The open HAL can use its own file (for example under
`/data/misc/sensors/`, created by an rc `mkdir` with owner `system`); it cannot read the MPL format.

### 5.6 Poll loop (shim)

`sensors.glass_1.so` `sensors_poll_context_t` polls five fds: [0] `/dev/iio:device0` (POLLIN), [1] compass fd
(POLLIN; unused with the MPU-attached compass), [2] light input device (POLLIN), [3] `event_glu` (POLLPRI),
[4] `event_tap` (POLLPRI). The poll timeout is −1 until the light sensor's `setDelay` sets it to
delay_ns/1000 clamped to [10, 1 000 000] (ctor @0x0a28, `setDelay` @0x0bf0). Activate/setDelay for handle 9 go to the
light sensor, everything else to the MPL object.

## 6. Light sensor (LTR-506ALS)

Kernel facts:

- Input device name `ltr506_als`, reports `EV_ABS/ABS_MISC` with the ALS ADC value 0..65535, then `SYN_REPORT`
  (`ltr506als.c:519-546, 1777-1779`). The value is ADC counts, not lux. When ALS was enabled across suspend and the
  value is unchanged, the driver flips its LSB once to force a new event (`:532-537`).
- Input device name `ltr506_ps`, reports `EV_ABS/ABS_DISTANCE` 0..4095 (`:428-437, 1815-1817`). The stock HAL does
  not expose proximity.
- sysfs on the input device (`/sys/devices/virtual/input/inputN/`, N = 2 for ALS and 3 for PS on stock, see rc
  lines 172-186; do not hard-code N): ALS `als_adc`, `als_adc_ch1`, `als_adc_ch2`, `als_resolution`, `als_enable`,
  `als_gain`, `als_meas_rate`, `als_threshold`, `als_filter_interrupts`; PS `ps_adc`, `ps_enable`, `ps_gain`,
  `ps_led`, `ps_meas_rate`, `ps_pulse_cnt`, `ps_threshold`, `ps_filter_interrupts` (`:1722-1770`). On the I2C device
  `4-003a`: `als_enable`, `ps_enable`, `dump_regs`, `status`, `als_filter_interrupts`, `ps_filter_interrupts`
  (`:1700-1716`).
- `als_enable` accepts "0" or "1" (`:1165-1184`). Part ID 0x90 needs the PS on while ALS is on; the driver handles
  that itself (`:918-966, 1843-1863`). Misc devices `ltr506_ls` and `ltr506_ps` exist but have no ioctl.
- Board defaults (`board-notle.c:1625-1660`): ALS gain 0, resolution field 0, measurement rate field 0 (100 ms),
  interrupt filtering off, no wake. If the IRQ cannot be requested the driver polls every 1000 ms (`ltr506als.c:48`).

Stock HAL facts (blob `libinvensense_hal.so` `LightSensor::LightSensor` @0x89ec, `enable` @0x8988, `readEvents`
@0x8828):

- Finds the input device whose name is `ltr506_als` under `/dev/input`, builds
  `/sys/class/input/<eventX>/device/als_enable`, and writes "1" at construction ("0"/"1" with a NUL, 2 bytes).
- Reports `data[0]` = ABS_MISC value converted to float **without scaling** (lux = ADC count), sensor handle 9,
  type LIGHT, one event per `SYN_REPORT`.

## 7. What the open HAL should expose (recommended design)

| handle | Android type | Source | Notes |
|---|---|---|---|
| 1 | ACCELEROMETER | IIO accel, raw mode | range 19.6133 m/s² (±2 g) or 39.2266 (±4 g, index 1) if preferred; resolution = FSR×9.80665/32768 |
| 2 | MAGNETIC_FIELD | IIO magn (AK8975 via MPU) | calibrated: HAL-side hard-iron offset estimate (sphere fit), persisted in its own file; range ±1200 µT, resolution 0.3 µT |
| 3 | GYROSCOPE | IIO anglvel, raw mode | ±2000 °/s = 34.9066 rad/s, resolution 0.0010652645; optional HAL-side bias removal at rest |
| 4 | LIGHT | `ltr506_als` input | lux = ADC count (stock behaviour) unless a device calibration is measured; range 64000 |
| optional | MAGNETIC_FIELD_UNCALIBRATED, GYROSCOPE_UNCALIBRATED | same data | type 14 / 16, available from KitKat |
| optional | PROXIMITY | `ltr506_ps` input | stock did not expose it; ABS_DISTANCE raw 0..4095, would need a threshold |

Leave orientation, rotation vector, gravity, linear acceleration and game rotation vector to the framework's
virtual sensors (SensorService registers them when the HAL lists accelerometer, gyroscope and magnetic field and
does not list those types itself; KitKat and Marshmallow both do this). Drop the vendor GLU/TAP sensors unless the
owner decides on a user-supplied DMP image; a software "look up" detector from the accelerometer is possible later.

Implementation rules derived from the facts above: one reader thread on `/dev/iio:device0`; parse records by the
enabled scan elements (sizes from `scan_elements/*_type`, or the fixed layout of section 4.4) and cross-check the
format byte; skip all-zero magnetometer triples; apply the sysfs mounting matrices; convert units; convert the
robust-clock timestamp to CLOCK_BOOTTIME; minimum delay 5 ms (200 Hz) for gyro/accel, 10 ms for the magnetometer
(the AK8975 single-measurement mode needs about 7–9 ms; the stock minDelay is 10000 µs).

## 8. API notes for KitKat and Marshmallow builds

- Stock device API is 1.1 (section 2). A 1.0 or 1.1 legacy `sensors_poll_device_1` works on both 4.4.4 and 6.0.1.
- `sensor_t` grew `stringType`, `requiredPermission`, `maxDelay`, `flags` after KitKat; guard the initialisers with
  the SDK level or `SENSORS_DEVICE_API_VERSION_1_3` availability. On 6.0.1, report `flags` (continuous for
  accel/gyro/mag, on-change for light) and `maxDelay` if compiled with 1.3 headers.
- `flush()` must return `-EINVAL` for one-shot sensors and produce a `META_DATA_FLUSH_COMPLETE` event on 1.1+.

## 9. Open questions that need a device

1. Confirm raw mode with `dmp_on` = 0 delivers gyro+accel+compass records with the format byte as described.
2. Verify the axes: Glass upright and still should give gravity on the expected Android axis after applying the
   sysfs matrices with body = M × chip; check the compass matrix the same way (turn the head).
3. Measure the offset between the robust clock and CLOCK_BOOTTIME across suspend/resume (the 32 kHz counter runs in
   suspend; CLOCK_BOOTTIME should track it closely).
4. Whether the light sensor needs a lux factor (compare ADC counts with a phone lux meter).
5. Which input numbers ALS/PS get on the new build (do not hard-code input2/input3).
6. Power state: confirm `power_state` ← 0 actually drops current when idle.
