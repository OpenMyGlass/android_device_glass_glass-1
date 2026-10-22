#
# Copyright (C) 2016 Justin Driggers
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

LOCAL_PATH := device/glass/glass-1

TARGET_BOARD_OMAP_CPU := 4430

# Include common omap4 makefile
$(call inherit-product, hardware/ti/omap4/omap4.mk)

WIFI_BAND := 802_11_BG

# Include bcm4330 makefile
$(call inherit-product, hardware/broadcom/wlan/bcmdhd/firmware/bcm4330/device-bcm.mk)

# Init files
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/rootdir/init.omap4430.rc:root/init.omap4430.rc \
    $(LOCAL_PATH)/rootdir/init.omap4430.usb.rc:root/init.omap4430.usb.rc \
    $(LOCAL_PATH)/rootdir/ueventd.omap4430.rc:root/ueventd.omap4430.rc

# Fstab
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/rootdir/fstab.omap4430:root/fstab.omap4430

# Kernel and SGX module, built by glass-kernel (variant tuned-stable) and copied into
# GLASS_KERNEL_DIR by OpenMyGlass/manifest's tools/fetch-kernel.sh. The module only loads into
# the kernel it was built with, so both come from the same glass-kernel build. Without them the
# build stops, unless GLASS_NO_KERNEL=true (system image only, as in CI checks).
GLASS_KERNEL_DIR ?= vendor/glass/kernel
ifneq ($(and $(wildcard $(GLASS_KERNEL_DIR)/zImage),$(wildcard $(GLASS_KERNEL_DIR)/pvrsrvkm_sgx540_120.ko)),)
PRODUCT_COPY_FILES += \
    $(GLASS_KERNEL_DIR)/zImage:kernel \
    $(GLASS_KERNEL_DIR)/pvrsrvkm_sgx540_120.ko:system/lib/modules/pvrsrvkm_sgx540_120.ko
else ifneq ($(GLASS_NO_KERNEL),true)
$(error no $(GLASS_KERNEL_DIR)/zImage and pvrsrvkm_sgx540_120.ko: run tools/fetch-kernel.sh from OpenMyGlass/manifest, or set GLASS_NO_KERNEL=true for a system-only build)
endif

# Media (docs/media-facts.md). media_codecs.xml includes the release's software codec lists.
PRODUCT_COPY_FILES += \
    device/glass/glass-1/media_codecs.xml:system/etc/media_codecs.xml \
    device/glass/glass-1/media_profiles.xml:system/etc/media_profiles.xml \
    frameworks/av/media/libstagefright/data/media_codecs_google_audio.xml:system/etc/media_codecs_google_audio.xml \
    frameworks/av/media/libstagefright/data/media_codecs_google_video.xml:system/etc/media_codecs_google_video.xml

# Hardware-specific features
PRODUCT_COPY_FILES += \
    device/glass/glass-1/permissions/hardware.xml:system/etc/permissions/hardware.xml

DEVICE_PACKAGE_OVERLAYS := \
    device/glass/glass-1/overlay

# Wi-Fi
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/wifi/wpa_supplicant_overlay.conf:system/etc/wifi/wpa_supplicant_overlay.conf

PRODUCT_PACKAGES += \
    libwpa_client \
    dhcpd.conf \
    wpa_supplicant \
    wpa_supplicant.conf

#Packages
PRODUCT_PACKAGES += \
    camera.omap4

# Hardware composer, built from hardware/ti/omap4/hwc (docs/hals.md). omap4.mk already adds it;
# listing it here keeps it if that changes. It installs to /system/vendor/lib/hw, where stock has
# its blob; proprietary-files.txt must not list that blob (tests/test_hwc.py).
PRODUCT_PACKAGES += \
    hwcomposer.omap4

# Open HALs (power/, lights/, sensors/; docs/hals.md). libhardware tries ro.product.board
# (glass_1) before ro.board.platform (omap4), so these win over any *.omap4 module. power.omap4
# stays installed but unused: hardware/ti/omap4/omap4.mk, inherited above, adds it.
PRODUCT_PACKAGES += \
    lights.glass_1 \
    power.glass_1 \
    sensors.glass_1

# Audio (audio/; docs/hals.md). Our primary HAL and policy replace the stock blobs. The a2dp,
# usb and r_submix modules are AOSP's; tinymix is for debugging the mixer.
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/audio/audio_policy.conf:system/etc/audio_policy.conf

PRODUCT_PACKAGES += \
    audio.a2dp.default \
    audio.primary.glass_1 \
    audio.r_submix.default \
    audio.usb.default \
    tinymix

# Filesystem
PRODUCT_PACKAGES += \
    e2fsck \
    make_ext4fs \
    setup_fs

$(call inherit-product-if-exists, vendor/glass/glass-1/device-vendor.mk)
$(call inherit-product, frameworks/native/build/phone-xhdpi-1024-dalvik-heap.mk)
