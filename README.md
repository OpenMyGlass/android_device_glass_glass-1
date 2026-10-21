<!-- SPDX-License-Identifier: Apache-2.0 -->

# android_device_glass_glass-1

AOSP device tree for the Google Glass Explorer Edition (`glass-1`, OMAP4430).

Based on Justin Driggers' android_device_glass_glass-1 (Apache-2.0). Google
files were removed from the history of this tree. That includes `firmware/`,
`rootdir/`, `wifi/bcmdhd.cal`, `wifi/wpa_supplicant.conf`, the two
`thermal_*_config.xml` overlays, `audio_policy.conf`, `system.prop`,
`board-info.txt` and `egl.cfg`. New `rootdir/` and `system.prop` files come in
a later change.

## Building

The proprietary binaries are extracted from your own device's factory image
with [glass-rom](https://github.com/OpenMyGlass/glass-rom)
(the `tools/` paths below are relative to a glass-rom checkout):

    tools/fetch-factory.sh factory/
    tools/sparse.py factory/system.img factory/system.raw
    tools/extract_vendor.py factory/system.raw path/to/android_device_glass_glass-1/proprietary-files.txt $AOSP/vendor/glass/glass-1

This writes `vendor/glass/glass-1/proprietary/` and `device-vendor.mk`, which
`device.mk` inherits. Then, from the AOSP root:

    lunch aosp_glass_1-userdebug

## What stays binary

The bootloader, xloader and FPGA partitions are never written. Chip firmware
(Ducati, Tesla DSP, BCM4330 Wi-Fi and Bluetooth, glasshub, touchpad, gas
gauge) and the PowerVR SGX540 user-space libraries stay binary. See the
[roadmap](https://github.com/OpenMyGlass/glass-rom/blob/main/docs/plans/2026-10-21-open-source-roadmap.md).
