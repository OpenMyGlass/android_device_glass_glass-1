<!-- SPDX-License-Identifier: Apache-2.0 -->

# android_device_glass_glass-1

AOSP device tree for the Google Glass Explorer Edition (`glass-1`, OMAP4430).

Based on Justin Driggers' android_device_glass_glass-1 (Apache-2.0). Google
files were removed from the history of this tree. That includes `firmware/`,
`rootdir/`, `wifi/bcmdhd.cal`, `wifi/wpa_supplicant.conf`, the two
`thermal_*_config.xml` overlays, `audio_policy.conf`, `system.prop`,
`board-info.txt` and `egl.cfg`. The new `rootdir/` and `system.prop` files
were written clean-room from `docs/hardware-facts.md`, and the new
`audio/audio_policy.conf` from `docs/specs/audio.md`. `media_codecs.xml` and
`media_profiles.xml` were byte-identical to the XE24 files; they were replaced by
files written from AOSP's `device/samsung/tuna` ones and the facts in
`docs/media-facts.md`. The old copies are still in this tree's history.

## Building

The proprietary binaries are extracted from your own device's factory image
with [glass-rom](https://github.com/OpenMyGlass/glass-rom). Follow its README
first, since `fetch-factory.sh` and `sparse.py` need the setup it describes.
The `tools/` paths below are relative to a glass-rom checkout:

    tools/fetch-factory.sh factory/
    tools/sparse.py factory/system.img factory/system.raw
    tools/extract_vendor.py factory/system.raw $AOSP/device/glass/glass-1/proprietary-files.txt $AOSP/vendor/glass/glass-1

This writes `vendor/glass/glass-1/proprietary/` and `device-vendor.mk`, which
`device.mk` inherits.

This branch (Android 6.0.1, Track B) is built from
[OpenMyGlass/manifest](https://github.com/OpenMyGlass/manifest), branch `android-6.0`, which
pins every project and has the build script and guide. The kernel and the GPU module come from
[glass-kernel](https://github.com/OpenMyGlass/glass-kernel) (variant `tuned-stable`, IMG DDK
`1.9@2166536`, dual MIT/GPL-2.0, the build of XE24's user-space libraries); the manifest's
`tools/fetch-kernel.sh` puts them in `vendor/glass/kernel/`, where `device.mk` installs them
from. `GLASS_NO_KERNEL=true` builds the system image only.

No image from this branch has been tested on a device. See
[docs/device-checklist.md](docs/device-checklist.md) for what to check on a real Glass, with
`fastboot boot` only.

## What stays binary

The bootloader, xloader and FPGA partitions are never written. Chip firmware
(Ducati, Tesla DSP, BCM4330 Wi-Fi and Bluetooth, glasshub, touchpad, gas
gauge) and the PowerVR SGX540 user-space libraries stay binary. See the
[roadmap](https://github.com/OpenMyGlass/glass-rom/blob/main/docs/plans/2026-10-21-open-source-roadmap.md).
