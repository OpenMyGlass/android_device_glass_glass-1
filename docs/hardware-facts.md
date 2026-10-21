<!-- SPDX-License-Identifier: Apache-2.0 -->
# Glass Explorer Edition hardware facts

These are the facts the device's init, fstab, ueventd and property files must
carry for the Android 6.0.1 port. Each row names where the fact was seen:

- `XE24 ramdisk`: the boot ramdisk of the XE24 factory image (Android 4.4).
- `XE24 build.prop`: `/system/build.prop` of the same image.
- `XE24 system`: the system partition of the same image.
- `Justin 6.0 tree`: Justin Driggers' Android 6.0 port of this tree.

Where the two disagree, the row follows what 6.0 needs and the Source cell says
which one it follows. `(unverified)` marks a fact that no source states
directly.

Conventions used in every table:

- Modes are four-digit octal.
- `-` in a Mode column means the mode is not changed (only the owner is).
- `-` in a User or Group column of the Services table means the service does
  not set it, so init runs it as root.

## Partitions

Every block device is `/dev/block/platform/omap/omap_hsmmc.1/by-name/<Name>`,
except `zram0`, which is `/dev/block/zram0`. The `efs` partition is listed
twice, once per mount point.

| Name | Mount point | Type | Mount options | fs_mgr flags | Source |
|---|---|---|---|---|---|
| boot | /boot | emmc | defaults | defaults | XE24 ramdisk, Justin 6.0 tree |
| cache | /cache | ext4 | noatime,nosuid,nodev,nomblk_io_submit,errors=panic | wait,check | XE24 ramdisk, Justin 6.0 tree |
| efs | /bootconfig | emmc | defaults | defaults | XE24 ramdisk, Justin 6.0 tree |
| efs | /bootloader | emmc | defaults | defaults | XE24 ramdisk, Justin 6.0 tree |
| fpga | /fpga | emmc | defaults | defaults | XE24 ramdisk, Justin 6.0 tree |
| misc | /misc | emmc | defaults | defaults | XE24 ramdisk, Justin 6.0 tree |
| recovery | /recovery | emmc | defaults | defaults | XE24 ramdisk, Justin 6.0 tree |
| system | /system | ext4 | ro | wait | XE24 ramdisk, Justin 6.0 tree |
| userdata | /data | ext4 | noatime,nosuid,nodev,nomblk_io_submit,errors=panic | wait,check | XE24 ramdisk, Justin 6.0 tree |
| xloader | /xloader | emmc | defaults | defaults | XE24 ramdisk, Justin 6.0 tree |
| zram0 | none | swap | defaults | zramsize=134217728 | XE24 ramdisk, Justin 6.0 tree |

## Device nodes

The stock files set some of these in ueventd and some in init; all of them
can be set in `ueventd.omap4430.rc`.

| Path | Mode | User | Group | Source |
|---|---|---|---|---|
| /dev/accelirq | 0660 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /dev/block/mmcblk0p4 | 0660 | root | system | XE24 ramdisk, Justin 6.0 tree |
| /dev/compassirq | 0660 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /dev/dsscomp | 0666 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /dev/glasshub | 0600 | system | system | XE24 ramdisk, Justin 6.0 tree (unverified: mode; the stock files set the owner only) |
| /dev/iio:device0 | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /dev/ion | 0666 | system | system | XE24 ramdisk, Justin 6.0 tree (ueventd mode; stock init later widens it to 0777) |
| /dev/mpu | 0660 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /dev/mpuirq | 0660 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /dev/pvrsrvkm | 0666 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /dev/rpmsg-omx0 | 0666 | drmrpc | drmrpc | XE24 ramdisk, Justin 6.0 tree |
| /dev/rpmsg-omx1 | 0666 | drmrpc | drmrpc | XE24 ramdisk, Justin 6.0 tree |
| /dev/rproc_user | 0666 | drmrpc | drmrpc | XE24 ramdisk, Justin 6.0 tree |
| /dev/si114x | 0666 | root | root | XE24 ramdisk, Justin 6.0 tree (unverified: owner) |
| /dev/tiler | 0666 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /dev/timerirq | 0660 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /dev/ttyO1 | 0600 | bluetooth | bluetooth | XE24 ramdisk, Justin 6.0 tree (unverified: mode; the stock files set the owner only) |
| /dev/ttyO3 | 0660 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /dev/video0 | 0777 | system | system | XE24 ramdisk, Justin 6.0 tree |

`/dev/ttyO1` is the Bluetooth UART and `/dev/ttyO3` the GPS UART.
`/dev/block/mmcblk0p4` is the bootconfig partition.

## Sysfs

Set these with `chown` and `chmod` in `init.omap4430.rc`. The stock files set
them at three stages:

- `on init`: everything under /sys/bus/iio, /sys/devices/platform/omap_i2c.4/i2c-4/4-0068,
  /sys/devices/platform/omap_i2c.4/i2c-4/4-003a, /sys/devices/platform/usb_mux.0,
  /sys/devices/virtual/input and /sys/devices/touchpad.
- `on fs`: everything under /sys/devices/platform/omapdss.
- `on boot`: everything under /sys/class/rfkill, /sys/devices/system/cpu,
  /sys/devices/platform/omap_i2c.4/i2c-4/4-0035 (glasshub) and /sys/kernel/debug.
  The /sys/kernel/debug paths exist only after debugfs is mounted (see Other init steps).

rfkill0 and rfkill3 belong to Bluetooth, rfkill1 and rfkill2 to Wi-Fi.

| Path | Mode | User | Group | Source |
|---|---|---|---|---|
| /sys/bus/iio/devices/iio:device0/accl_bias | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/bus/iio/devices/iio:device0/accl_enable | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/bus/iio/devices/iio:device0/accl_matrix | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/bus/iio/devices/iio:device0/buffer/enable | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/bus/iio/devices/iio:device0/buffer/length | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/bus/iio/devices/iio:device0/compass_enable | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/bus/iio/devices/iio:device0/compass_matrix | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/bus/iio/devices/iio:device0/dmp_event_int_on | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/bus/iio/devices/iio:device0/dmp_firmware | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/bus/iio/devices/iio:device0/dmp_int_on | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/bus/iio/devices/iio:device0/dmp_on | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/bus/iio/devices/iio:device0/dmp_output_rate | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/bus/iio/devices/iio:device0/event_display_orientation | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/bus/iio/devices/iio:device0/event_glu | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/bus/iio/devices/iio:device0/firmware_loaded | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/bus/iio/devices/iio:device0/glu_level | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/bus/iio/devices/iio:device0/glu_outlier | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree (unverified: mode; the stock chmod names glu_outliner) |
| /sys/bus/iio/devices/iio:device0/glu_roll | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/bus/iio/devices/iio:device0/glu_trigger | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/bus/iio/devices/iio:device0/gyro_enable | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/bus/iio/devices/iio:device0/gyro_matrix | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/bus/iio/devices/iio:device0/in_accel_scale | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/bus/iio/devices/iio:device0/in_anglvel_scale | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/bus/iio/devices/iio:device0/in_magn_scale | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/bus/iio/devices/iio:device0/key | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/bus/iio/devices/iio:device0/power_state | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/bus/iio/devices/iio:device0/quaternion_on | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/bus/iio/devices/iio:device0/sampling_frequency | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/bus/iio/devices/iio:device0/scan_elements/in_accel_x_en | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/bus/iio/devices/iio:device0/scan_elements/in_accel_y_en | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/bus/iio/devices/iio:device0/scan_elements/in_accel_z_en | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/bus/iio/devices/iio:device0/scan_elements/in_anglvel_x_en | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/bus/iio/devices/iio:device0/scan_elements/in_anglvel_y_en | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/bus/iio/devices/iio:device0/scan_elements/in_anglvel_z_en | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/bus/iio/devices/iio:device0/scan_elements/in_magn_x_en | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/bus/iio/devices/iio:device0/scan_elements/in_magn_y_en | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/bus/iio/devices/iio:device0/scan_elements/in_magn_z_en | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/bus/iio/devices/iio:device0/scan_elements/in_quaternion_r_en | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/bus/iio/devices/iio:device0/scan_elements/in_quaternion_x_en | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/bus/iio/devices/iio:device0/scan_elements/in_quaternion_y_en | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/bus/iio/devices/iio:device0/scan_elements/in_quaternion_z_en | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/bus/iio/devices/iio:device0/scan_elements/in_timestamp_en | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/bus/iio/devices/iio:device0/self_test | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/bus/iio/devices/iio:device0/tap_on | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/bus/iio/devices/iio:device0/temperature | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/bus/iio/devices/iio:device0/trigger/current_trigger | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/bus/iio/devices/trigger0/name | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/class/rfkill/rfkill0/state | 0660 | bluetooth | bluetooth | XE24 ramdisk, Justin 6.0 tree |
| /sys/class/rfkill/rfkill0/type | - | bluetooth | bluetooth | XE24 ramdisk, Justin 6.0 tree |
| /sys/class/rfkill/rfkill1/state | 0660 | wifi | wifi | XE24 ramdisk, Justin 6.0 tree |
| /sys/class/rfkill/rfkill1/type | - | wifi | wifi | XE24 ramdisk, Justin 6.0 tree |
| /sys/class/rfkill/rfkill2/state | 0660 | wifi | wifi | XE24 ramdisk, Justin 6.0 tree |
| /sys/class/rfkill/rfkill2/type | - | wifi | wifi | XE24 ramdisk, Justin 6.0 tree |
| /sys/class/rfkill/rfkill3/state | 0660 | bluetooth | bluetooth | XE24 ramdisk, Justin 6.0 tree |
| /sys/class/rfkill/rfkill3/type | - | bluetooth | bluetooth | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omap_i2c.4/i2c-4/4-0035/activity_guardband | - | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omap_i2c.4/i2c-4/4-0035/activity_threshold | - | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omap_i2c.4/i2c-4/4-0035/activity_timeout | - | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omap_i2c.4/i2c-4/4-0035/ambient_enable | - | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omap_i2c.4/i2c-4/4-0035/bootloader_version | - | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omap_i2c.4/i2c-4/4-0035/calibrate | - | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omap_i2c.4/i2c-4/4-0035/calibration_values | - | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omap_i2c.4/i2c-4/4-0035/current_timestamp | - | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omap_i2c.4/i2c-4/4-0035/debug | - | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omap_i2c.4/i2c-4/4-0035/detector_gain | - | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omap_i2c.4/i2c-4/4-0035/disable | - | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omap_i2c.4/i2c-4/4-0035/doff_threshold | - | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omap_i2c.4/i2c-4/4-0035/don_doff | - | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omap_i2c.4/i2c-4/4-0035/don_doff_enable | - | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omap_i2c.4/i2c-4/4-0035/don_max_threshold | - | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omap_i2c.4/i2c-4/4-0035/don_threshold | - | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omap_i2c.4/i2c-4/4-0035/driver_flags | - | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omap_i2c.4/i2c-4/4-0035/driver_version | - | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omap_i2c.4/i2c-4/4-0035/error_code | - | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omap_i2c.4/i2c-4/4-0035/error_count | - | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omap_i2c.4/i2c-4/4-0035/flash_status | - | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omap_i2c.4/i2c-4/4-0035/ir | - | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omap_i2c.4/i2c-4/4-0035/irq | - | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omap_i2c.4/i2c-4/4-0035/irq_status | - | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omap_i2c.4/i2c-4/4-0035/irq_timestamp | - | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omap_i2c.4/i2c-4/4-0035/last_irq_status | - | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omap_i2c.4/i2c-4/4-0035/last_timestamp | - | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omap_i2c.4/i2c-4/4-0035/led_drive | - | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omap_i2c.4/i2c-4/4-0035/mcu_debug | - | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omap_i2c.4/i2c-4/4-0035/mcu_debug16 | - | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omap_i2c.4/i2c-4/4-0035/passthru_enable | - | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omap_i2c.4/i2c-4/4-0035/pause | - | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omap_i2c.4/i2c-4/4-0035/pause_for_audio | 0664 | media | media | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omap_i2c.4/i2c-4/4-0035/prox_version | - | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omap_i2c.4/i2c-4/4-0035/proxraw | - | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omap_i2c.4/i2c-4/4-0035/sample_count | - | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omap_i2c.4/i2c-4/4-0035/timer_count | - | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omap_i2c.4/i2c-4/4-0035/update_fw_data | - | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omap_i2c.4/i2c-4/4-0035/update_fw_enable | - | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omap_i2c.4/i2c-4/4-0035/version | - | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omap_i2c.4/i2c-4/4-0035/visible | - | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omap_i2c.4/i2c-4/4-0035/wink | - | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omap_i2c.4/i2c-4/4-0035/wink_enable | - | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omap_i2c.4/i2c-4/4-0035/wink_flag_enable | - | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omap_i2c.4/i2c-4/4-0035/wink_max | - | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omap_i2c.4/i2c-4/4-0035/wink_min | - | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omap_i2c.4/i2c-4/4-003a/als_enable | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omap_i2c.4/i2c-4/4-003a/als_filter_interrupts | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omap_i2c.4/i2c-4/4-003a/dump_regs | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omap_i2c.4/i2c-4/4-003a/ps_enable | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omap_i2c.4/i2c-4/4-003a/ps_filter_interrupts | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omap_i2c.4/i2c-4/4-003a/status | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omap_i2c.4/i2c-4/4-0068/iio:device0/display_orientation_on | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omap_i2c.4/i2c-4/4-0068/iio:device0/glu_gyro_bias_lpf_gain | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omap_i2c.4/i2c-4/4-0068/iio:device0/glu_horizontal_threshold_squared | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omap_i2c.4/i2c-4/4-0068/iio:device0/glu_outlier_max | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omap_i2c.4/i2c-4/4-0068/iio:device0/glu_outlier_min | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omap_i2c.4/i2c-4/4-0068/iio:device0/glu_relative_angle_threshold | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omap_i2c.4/i2c-4/4-0068/iio:device0/glu_relative_lookup_leak_gain | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omap_i2c.4/i2c-4/4-0068/iio:device0/glu_version4_on | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omap_i2c.4/i2c-4/4-0068/iio:device0/in_accel_x_calibbias | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omap_i2c.4/i2c-4/4-0068/iio:device0/in_accel_x_offset | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omap_i2c.4/i2c-4/4-0068/iio:device0/in_accel_y_calibbias | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omap_i2c.4/i2c-4/4-0068/iio:device0/in_accel_y_offset | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omap_i2c.4/i2c-4/4-0068/iio:device0/in_accel_z_calibbias | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omap_i2c.4/i2c-4/4-0068/iio:device0/in_accel_z_offset | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omap_i2c.4/i2c-4/4-0068/iio:device0/in_anglvel_x_calibbias | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omap_i2c.4/i2c-4/4-0068/iio:device0/in_anglvel_y_calibbias | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omap_i2c.4/i2c-4/4-0068/iio:device0/in_anglvel_z_calibbias | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omap_i2c.4/i2c-4/4-0068/iio:device0/lpa_freq | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omap_i2c.4/i2c-4/4-0068/iio:device0/lpa_mode | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omap_i2c.4/i2c-4/4-0068/iio:device0/orientation_on | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omap_i2c.4/i2c-4/4-0068/iio:device0/tap_min_count | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omap_i2c.4/i2c-4/4-0068/iio:device0/tap_threshold | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omap_i2c.4/i2c-4/4-0068/iio:device0/tap_time | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omapdss/manager2/panel-notle-dpi/brightness | - | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omapdss/manager2/panel-notle-dpi/brightness_limit | - | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omapdss/manager2/panel-notle-dpi/brightness_multiplier | - | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omapdss/manager2/panel-notle-dpi/gamma_preset | - | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omapdss/manager2/panel-notle-dpi/mono | - | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omapdss/manager2/vsync_divider | - | media | media | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/usb_mux.0/mux_mode | 0664 | media | media | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor | - | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/touchpad/enabled | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/touchpad/touchpad.fn01/chargerinput | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/touchpad/touchpad.fn01/nosleep | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/touchpad/touchpad.fn01/reportrate | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/touchpad/touchpad.fn01/reset | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/touchpad/touchpad.fn01/sleepmode | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/touchpad/touchpad.fn05/no_auto_cal | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/touchpad/touchpad.fn11/abs_pos_filt | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/touchpad/touchpad.fn11/relreport | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/touchpad/touchpad.fn11/rezero | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/touchpad/touchpad.fn34/blocknum | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/touchpad/touchpad.fn34/bootloaderid | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/touchpad/touchpad.fn34/cmd | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/touchpad/touchpad.fn34/data | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/touchpad/touchpad.fn34/rescanPDT | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/touchpad/touchpad.fn34/status | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/virtual/input/input2/als_adc | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/virtual/input/input2/als_enable | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/virtual/input/input2/als_filter_interrupts | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/virtual/input/input2/als_gain | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/virtual/input/input2/als_meas_rate | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/virtual/input/input2/als_resolution | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/virtual/input/input2/als_threshold | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/virtual/input/input3/ps_adc | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/virtual/input/input3/ps_enable | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/virtual/input/input3/ps_filter_interrupts | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/virtual/input/input3/ps_gain | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/virtual/input/input3/ps_led | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/virtual/input/input3/ps_meas_rate | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/virtual/input/input3/ps_pulse_cnt | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/virtual/input/input3/ps_threshold | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/kernel/debug/rmi/touchpad/F01/interrupt_enable | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/kernel/debug/rmi/touchpad/F11/clip.0 | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/kernel/debug/rmi/touchpad/F11/delta_threshold.0 | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/kernel/debug/rmi/touchpad/F11/flip.0 | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/kernel/debug/rmi/touchpad/F11/offset.0 | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/kernel/debug/rmi/touchpad/F11/rezero_wait | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/kernel/debug/rmi/touchpad/F11/swap.0 | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/kernel/debug/rmi/touchpad/F11/type_a.0 | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/kernel/debug/rmi/touchpad/comms_debug | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /sys/kernel/debug/rmi/touchpad/irq_debug | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |

## Services

| Name | Program | Class | User | Group | Oneshot | Source |
|---|---|---|---|---|---|---|
| battery-charger | /charger | charger | - | - | no | XE24 ramdisk, Justin 6.0 tree |
| dhcpcd_bt-pan | /system/bin/dhcpcd | main | - | - | yes | XE24 ramdisk, Justin 6.0 tree |
| dhcpcd_wlan0 | /system/bin/dhcpcd | main | - | - | yes | XE24 ramdisk, Justin 6.0 tree |
| iprenew_bt-pan | /system/bin/dhcpcd | main | - | - | yes | XE24 ramdisk, Justin 6.0 tree |
| iprenew_wlan0 | /system/bin/dhcpcd | main | - | - | yes | XE24 ramdisk, Justin 6.0 tree |
| raft | /system/bin/raft | main | - | - | yes | XE24 ramdisk |
| setup_fs | /system/bin/setup_fs | core | root | root | yes | XE24 ramdisk, Justin 6.0 tree |
| shm_service | /system/bin/shm_service | core | system | system | no | XE24 ramdisk (Justin 6.0 tree disables it) |
| sysproxyd | /system/bin/sysproxyd | main | root | root | no | XE24 ramdisk (Justin 6.0 tree drops it) |
| wpa_supplicant | /system/bin/wpa_supplicant | main | - | - | yes | Justin 6.0 tree |

## Service arguments

| Name | Arguments | Options | Source |
|---|---|---|---|
| battery-charger | none | none | XE24 ramdisk, Justin 6.0 tree |
| dhcpcd_bt-pan | -ABKL | disabled | XE24 ramdisk, Justin 6.0 tree |
| dhcpcd_wlan0 | -ABKL | disabled | XE24 ramdisk, Justin 6.0 tree |
| iprenew_bt-pan | -n | disabled | XE24 ramdisk, Justin 6.0 tree |
| iprenew_wlan0 | -n | disabled | XE24 ramdisk, Justin 6.0 tree |
| raft | none | disabled; started when persist.sys.raft_logs becomes true, stopped when it becomes false | XE24 ramdisk |
| setup_fs | the cache and userdata block devices (by-name paths from Partitions) | none | XE24 ramdisk, Justin 6.0 tree |
| shm_service | --no-fork | none | XE24 ramdisk |
| sysproxyd | none | none | XE24 ramdisk |
| wpa_supplicant | -iwlan0 -Dnl80211 -c/data/misc/wifi/wpa_supplicant.conf -I/system/etc/wifi/wpa_supplicant_overlay.conf -e/data/misc/wifi/entropy.bin -g@android:wpa_wlan0 | disabled; socket wpa_wlan0, dgram, 0660, wifi wifi | Justin 6.0 tree |

## Properties

| Property | Value | Source |
|---|---|---|
| bluetooth.enable_timeout_ms | 10000 | XE24 build.prop, Justin 6.0 tree |
| com.ti.omap_enhancement | true | XE24 build.prop |
| persist.sys.forced_orientation | 0 | XE24 build.prop, Justin 6.0 tree |
| ro.bq.gpu_to_cpu_unsupported | 1 | XE24 build.prop, Justin 6.0 tree |
| ro.bt.bdaddr_path | /data/misc/bluedroid/bdaddr | XE24 ramdisk, Justin 6.0 tree |
| ro.carrier | wifi-only | Justin 6.0 tree |
| ro.hwui.disable_scissor_opt | true | XE24 build.prop, Justin 6.0 tree |
| ro.hwui.drop_shadow_cache_size | 1 | XE24 build.prop |
| ro.hwui.gradient_cache_size | 0.5 | XE24 build.prop |
| ro.hwui.layer_cache_size | 4 | XE24 build.prop |
| ro.hwui.patch_cache_size | 64 | XE24 build.prop |
| ro.hwui.path_cache_size | 1 | XE24 build.prop |
| ro.hwui.r_buffer_cache_size | 1 | XE24 build.prop |
| ro.hwui.shape_cache_size | 0.5 | XE24 build.prop |
| ro.hwui.text_large_cache_height | 512 | XE24 build.prop |
| ro.hwui.text_large_cache_width | 2048 | XE24 build.prop |
| ro.hwui.text_small_cache_height | 256 | XE24 build.prop |
| ro.hwui.text_small_cache_width | 1024 | XE24 build.prop |
| ro.hwui.texture_cache_flushrate | 0.4 | XE24 build.prop |
| ro.hwui.texture_cache_size | 12 | XE24 build.prop |
| ro.opengles.version | 131072 | XE24 build.prop, Justin 6.0 tree |
| ro.sf.lcd_density | 240 | XE24 build.prop, Justin 6.0 tree |
| video.h264enc.profile | 1 | XE24 ramdisk, Justin 6.0 tree |
| wifi.interface | wlan0 | XE24 build.prop, Justin 6.0 tree |
| wifi.supplicant_scan_interval | 60 | XE24 build.prop, Justin 6.0 tree |
| wlan.driver.vendor | bcm | XE24 ramdisk, Justin 6.0 tree |

The stock image sets `ro.bt.bdaddr_path`, `video.h264enc.profile` and
`wlan.driver.vendor` from init; `system.prop` gives the same values.

## USB

Every function set uses the same IDs. When the set contains `adb`, adbd is
started; for `none`, adbd is stopped and the gadget disabled.

| Function set | idVendor | idProduct | Source |
|---|---|---|---|
| acm,usbnet | 18d1 | 9001 | XE24 ramdisk, Justin 6.0 tree |
| acm,usbnet,adb | 18d1 | 9001 | XE24 ramdisk, Justin 6.0 tree |
| adb | 18d1 | 9001 | XE24 ramdisk, Justin 6.0 tree |
| mass_storage | 18d1 | 9001 | XE24 ramdisk, Justin 6.0 tree |
| mass_storage,adb | 18d1 | 9001 | XE24 ramdisk, Justin 6.0 tree |
| mtp | 18d1 | 9001 | XE24 ramdisk, Justin 6.0 tree |
| mtp,adb | 18d1 | 9001 | XE24 ramdisk, Justin 6.0 tree |
| ptp | 18d1 | 9001 | XE24 ramdisk, Justin 6.0 tree |
| ptp,adb | 18d1 | 9001 | XE24 ramdisk, Justin 6.0 tree |
| rndis | 18d1 | 9001 | XE24 ramdisk, Justin 6.0 tree |
| rndis,acm,usbnet | 18d1 | 9001 | XE24 ramdisk, Justin 6.0 tree |
| rndis,acm,usbnet,adb | 18d1 | 9001 | XE24 ramdisk, Justin 6.0 tree |
| rndis,adb | 18d1 | 9001 | XE24 ramdisk, Justin 6.0 tree |
| usbnet | 18d1 | 9001 | XE24 ramdisk, Justin 6.0 tree |

## USB gadget settings

All paths are under /sys/class/android_usb.

| Path | Value | When | Source |
|---|---|---|---|
| android0/bDeviceClass | 0 | function set none | XE24 ramdisk, Justin 6.0 tree |
| android0/bDeviceClass | 224 | every function set containing rndis | XE24 ramdisk, Justin 6.0 tree |
| android0/iManufacturer | value of ro.product.manufacturer | on boot | XE24 ramdisk, Justin 6.0 tree |
| android0/iProduct | value of ro.product.model | on boot | XE24 ramdisk, Justin 6.0 tree |
| android0/iSerial | value of ro.serialno | on boot | XE24 ramdisk, Justin 6.0 tree |
| f_mass_storage/inquiry_string | "EltonBoard            0100" (12 spaces) | on boot | XE24 ramdisk, Justin 6.0 tree |

## Directories

| Path | Mode | User | Group | Source |
|---|---|---|---|---|
| /data/gps | 0770 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /data/gps/nvm | 0770 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /data/misc/audio | 0664 | system | system | XE24 ramdisk, Justin 6.0 tree |
| /data/misc/dhcp | 0770 | dhcp | dhcp | XE24 ramdisk, Justin 6.0 tree |
| /data/misc/gcam | 0770 | media | camera | XE24 ramdisk, Justin 6.0 tree |
| /data/misc/sensors | 0770 | system | system | This tree: the open sensors HAL keeps its magnetometer calibration there (docs/hals.md). Not in XE24, whose MPL library wrote /data/inv_cal_data.bin (docs/specs/sensors.md §5.5) |
| /data/misc/wifi | 0770 | wifi | wifi | XE24 ramdisk, Justin 6.0 tree |
| /data/misc/wifi/sockets | 0770 | wifi | wifi | XE24 ramdisk, Justin 6.0 tree |

The stock files create these `on boot`, except /data/misc/sensors, which only this tree has.
`init.omap4430.rc` creates all of them `on boot` (checked by `tests/test_facts.py`).

## Sysfs writes

| Path | Value | When | Source |
|---|---|---|---|
| /sys/bus/rmi/devices/touchpad.fn11/gesture | 1 | on boot | XE24 ramdisk, Justin 6.0 tree |
| /sys/bus/rmi/devices/touchpad.fn11/view_enable | 1 | on boot | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omapdss/manager2/panel-notle-dpi/brightness | 100 | on boot | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omapdss/manager2/panel-notle-dpi/brightness | 255 | on charger | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/platform/omapdss/manager2/vsync_divider | 6 | on boot | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/system/cpu/cpu0/cpufreq/boost_cpufreq | 1 | on boot | XE24 ramdisk, Justin 6.0 tree |
| /sys/devices/system/cpu/cpufreq/interactive/boost_timeout | 30000000 | on boot | XE24 ramdisk, Justin 6.0 tree |

The boost writes hold the CPU at a higher clock for the first 30 seconds of boot.

## Other init steps

| When | Step | Source |
|---|---|---|
| on init | /mnt/sdcard and /storage/sdcard0 are symlinks to /sdcard | Justin 6.0 tree |
| on fs | /dev/block/platform/omap is a symlink to /dev/block/platform, so both by-name paths work | XE24 ramdisk, Justin 6.0 tree |
| on fs | mount all and enable swap from /fstab.omap4430 | XE24 ramdisk, Justin 6.0 tree |
| on fs | wait for /dev/rpmsg-omx0 and /dev/rpmsg-omx1, then load /system/lib/modules/pvrsrvkm_sgx540_120.ko | XE24 ramdisk, Justin 6.0 tree |
| on boot | mount debugfs at /sys/kernel/debug, before the /sys/kernel/debug rows of Sysfs | XE24 ramdisk, Justin 6.0 tree |
| on boot | /dev/fd is a symlink to /proc/self/fd | XE24 ramdisk, Justin 6.0 tree |

The SGX kernel module is built by `BoardConfig.mk` from IMG DDK `1.9@2166536`
(KFire-Android/android_vendor_amazon_otter, `proprietary/imgtec/eurasia_km`, dual
MIT/GPL-2.0, pinned in `tools/pvr.env` and fetched by `tools/fetch-pvr.sh`), with
glass-kernel's build options. XE24's user-space libraries are that DDK build; the
Justin 6.0 tree built OMAP4-AOSP `pvr-source` (`1.9@2291151`) instead. XE24 shipped
its own copy in /system/lib/modules; the tree does not extract it.

## Left out

| Item | Reason |
|---|---|
| service glass_bdaddr (/system/etc/glass_bdaddr.sh) | Glass-only script, not in proprietary-files.txt. Without it nothing writes the file named by ro.bt.bdaddr_path. |
| service glass_bootconfig (/system/etc/glass_bootconfig.sh) | Glass-only script, not in proprietary-files.txt (the xbin/bootconfig tool it drives is). |
| service glass_bqflash (/system/etc/glass_bqflash.sh) | Glass-only script, not in proprietary-files.txt (the bin/bqflash tool it drives is). |
| service glass_gpsclear (/system/etc/glass_gpsclear.sh) | Glass-only script, not in proprietary-files.txt. |
| service glass_init (/system/etc/glass_init.sh) | Glass-only script, not in proprietary-files.txt. |
| service glass_tpflash (/system/etc/glass_tpflash.sh) | Glass-only script, not in proprietary-files.txt. |
| service pvrsrvinit | OMAP4-AOSP `init.omap4.rc`, which `init.omap4430.rc` imports, already defines it for /system/bin/pvrsrvinit, built from OMAP4-AOSP; a second definition would clash. Justin 6.0 tree drops it too. |
| service sdcard | Android 6.0 vold starts the sdcard daemon itself; Justin 6.0 tree drops it. |
| XE24 emulated-storage layout (/mnt/shell/emulated, /storage/emulated, EXTERNAL_STORAGE and EMULATED_STORAGE_* variables) | Android 4.4 layout; 6.0 vold manages storage. Justin 6.0 tree drops it. |
| /data/private-cache (mode 6770) | Owned by a Glass-only user ID that AOSP does not define. |
| /data/system and /data/media | AOSP init.rc already creates them. |
| /data/local/tmp mode 0777 | Debugging aid for valgrind; AOSP init.rc sets its own mode. |
| /dev/ion mode 0777 (stock init, on boot) | Wider than the ueventd mode 0666; the Device nodes table keeps 0666. |
| /proc/cmdline mode 0444 | XE24 only; Justin 6.0 tree drops it. |
| PATH, TERM and TERMINFO exports | AOSP 6.0 init.environ.rc sets the environment. |
| cpufreq interactive governor tunables (timer_rate, min_sample_time, hispeed_freq) | Disabled in the stock files. |
| dalvik.vm.* heap properties | `device.mk` inherits phone-xhdpi-1024-dalvik-heap.mk. |
| Build, Glass version, Google app and USB default (persist.sys.usb.config) properties | Not hardware facts; the build sets them. |
