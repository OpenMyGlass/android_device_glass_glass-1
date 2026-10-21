#
# Copyright (C) 2013 The Android Open-Source Project
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

# Inherit common omap4 board config
-include hardware/ti/omap4/BoardConfigCommon.mk
TI_OMAP_USE_BLTSVILLE := true

TARGET_NO_BOOTLOADER := true

TARGET_BOOTLOADER_BOARD_NAME := glass_1

# Kernel: this tree does not build the kernel inline. device.mk installs the zImage and the SGX
# module that glass-kernel builds (variant tuned-stable, DDK 1.9@2166536, the build of XE24's
# user-space libraries) from GLASS_KERNEL_DIR; see docs/kitkat.md.
BOARD_KERNEL_PAGESIZE := 2048
BOARD_KERNEL_BASE := 0x80000000
BOARD_KERNEL_CMDLINE := androidboot.hardware=omap4430 vmalloc=500M androidboot.carrier=wifi-only product_type=w androidboot.selinux=permissive
# omap4-aosp's build still includes core/tasks/kernel.mk, which defaults TARGET_KERNEL_SOURCE to
# kernel/<vendor>/<device> and, if that directory exists, adds its own rule for the kernel image.
# Point it at a path that never exists, so only device.mk's prebuilt rule applies (the task then
# just prints its "prebuilt kernel" deprecation warning).
TARGET_KERNEL_SOURCE := device/glass/glass-1/no-inline-kernel
# GLASS_NO_KERNEL=true: system image only, no boot or recovery image (device.mk).
ifeq ($(GLASS_NO_KERNEL),true)
TARGET_NO_KERNEL := true
endif

TI_CAMERAHAL_INTERFACE := OMX

# Filesystem
TARGET_USERIMAGES_USE_EXT4 := true
BOARD_BOOTIMAGE_PARTITION_SIZE := 8388608
BOARD_RECOVERYIMAGE_PARTITION_SIZE := 8388608
BOARD_SYSTEMIMAGE_PARTITION_SIZE := 1073741824
BOARD_USERDATAIMAGE_PARTITION_SIZE := 14028374016
BOARD_FLASH_BLOCK_SIZE := 131072

# ART only (docs/kitkat.md). 4.4 pre-optimises only for Dalvik: the host dexopt writes .odex
# files that ART does not read. So nothing is pre-optimised, every APK and jar keeps its
# classes.dex, and dex2oat compiles it on the device.
DISABLE_DEXPREOPT := true

# Wi-Fi
BOARD_WLAN_DEVICE                := bcmdhd
WPA_SUPPLICANT_VERSION           := VER_0_8_X
BOARD_WPA_SUPPLICANT_DRIVER      := NL80211
BOARD_WPA_SUPPLICANT_PRIVATE_LIB := lib_driver_cmd_$(BOARD_WLAN_DEVICE)
WIFI_DRIVER_FW_PATH_PARAM        := "/sys/module/bcmdhd/parameters/firmware_path"
WIFI_DRIVER_FW_PATH_STA          := "/vendor/firmware/fw_bcmdhd.bin"

# Bluetooth
BOARD_HAVE_BLUETOOTH := true
BOARD_HAVE_BLUETOOTH_BCM := true
BOARD_BLUETOOTH_BDROID_BUILDCFG_INCLUDE_DIR := device/glass/glass-1/bluetooth

# Lean system (docs/kitkat.md, "Lean items"). Packages the AOSP products inherited by
# full_glass_1.mk add, which Glass does not use. inherit-product cannot take a package away, so
# they are filtered out of the product's list here: BoardConfig.mk is read after product
# configuration and before build/core/main.mk reads PRODUCT_PACKAGES. Delete a name to build
# it again.
GLASS_REMOVED_PACKAGES := \
    BasicDreams Browser Calculator Calendar Camera2 Contacts DeskClock Email Exchange2 \
    Galaxy4 Gallery2 HoloSpiralWallpaper LatinIME Launcher2 LiveWallpapers \
    LiveWallpapersPicker MagicSmokeWallpapers Music NoiseField OneTimeInitializer OpenWnn \
    PhaseBeam PhotoTable PinyinIME PrintSpooler QuickSearchBox TeleService \
    TelephonyProvider VideoEditor VisualizationWallpapers WAPPushManager WallpaperCropper \
    libWnnEngDic libWnnJpnDic libwnndict
PRODUCTS.$(INTERNAL_PRODUCT).PRODUCT_PACKAGES := \
    $(filter-out $(GLASS_REMOVED_PACKAGES),$(PRODUCTS.$(INTERNAL_PRODUCT).PRODUCT_PACKAGES))
