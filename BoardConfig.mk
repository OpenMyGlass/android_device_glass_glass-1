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

# Inline kernel building
TARGET_KERNEL_SOURCE := kernel/glass/glass-1
TARGET_KERNEL_CONFIG := notle_defconfig
BOARD_KERNEL_PAGESIZE := 2048
BOARD_KERNEL_BASE := 0x80000000
BOARD_KERNEL_CMDLINE := androidboot.hardware=omap4430 vmalloc=500M androidboot.carrier=wifi-only product_type=w androidboot.selinux=permissive

# External SGX module, built from IMG DDK 1.9@2166536 (dual MIT/GPL-2.0): the build of
# XE24's user-space libraries, which check the module's DDK build. Fetch it with
# tools/fetch-pvr.sh $(GLASS_PVR_FETCH_DIR); tools/pvr.env pins it as glass-kernel does.
# The make variables are glass-kernel's scripts/build.sh ones: the same DDK build options as
# the module glass-kernel compares with the stock XE24 one (its docs/gpu-module.md).
GLASS_PVR_FETCH_DIR ?= vendor/glass/pvr-km
GLASS_PVR_KM ?= $(GLASS_PVR_FETCH_DIR)/proprietary/imgtec/eurasia_km
# Recursive (=): KERNEL_OUT is defined after this file is read.
GLASS_PVR_BUILD = $(abspath $(KERNEL_OUT)/../pvr-build)
SGX_MODULES:
	test -n "$(KERNEL_OUT)" && test -n "$(KERNEL_MODULES_OUT)"
	test -f $(GLASS_PVR_KM)/include4/pvrversion.h || { echo "error: no DDK at $(GLASS_PVR_KM); run device/glass/glass-1/tools/fetch-pvr.sh $(GLASS_PVR_FETCH_DIR)" >&2; exit 1; }
	rm -rf $(GLASS_PVR_BUILD)
	cp -R $(GLASS_PVR_KM) $(GLASS_PVR_BUILD)
	make -j8 -C $(GLASS_PVR_BUILD)/eurasiacon/build/linux2/omap4430_android ARCH=arm KERNEL_CROSS_COMPILE=arm-eabi- CROSS_COMPILE=arm-eabi- KERNELDIR=$(abspath $(KERNEL_OUT)) TARGET_PRODUCT=blaze_tablet BUILD=release TARGET_SGX=540 PLATFORM_VERSION=4.0 OUT=$(GLASS_PVR_BUILD)/binary kbuild
	cp $(GLASS_PVR_BUILD)/binary/target/pvrsrvkm_sgx540_120.ko $(KERNEL_MODULES_OUT)
	$(ARM_EABI_TOOLCHAIN)/arm-eabi-strip --strip-unneeded $(KERNEL_MODULES_OUT)/pvrsrvkm_sgx540_120.ko

TARGET_KERNEL_MODULES += SGX_MODULES

TI_CAMERAHAL_INTERFACE := OMX

# Filesystem
TARGET_USERIMAGES_USE_EXT4 := true
BOARD_BOOTIMAGE_PARTITION_SIZE := 8388608
BOARD_RECOVERYIMAGE_PARTITION_SIZE := 8388608
BOARD_SYSTEMIMAGE_PARTITION_SIZE := 1073741824
BOARD_USERDATAIMAGE_PARTITION_SIZE := 14028374016
BOARD_FLASH_BLOCK_SIZE := 131072

# Enable dex pre-optimization with PIC
WITH_DEXPREOPT := true
WITH_DEXPREOPT_PIC := true

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
