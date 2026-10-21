# SPDX-License-Identifier: Apache-2.0
# audio.primary.glass_1: see docs/hals.md. tools/build-hals.py reads LOCAL_MODULE, LOCAL_SRC_FILES,
# LOCAL_CFLAGS and LOCAL_SHARED_LIBRARIES from this file to build it in CI.
LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := audio.primary.glass_1
# LOCAL_MODULE_PATH rather than LOCAL_MODULE_RELATIVE_PATH (5.0 and later): it works on both
# the 4.4 and 6.0 builds, and the device is 32-bit only.
LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/hw
LOCAL_MODULE_TAGS := optional
LOCAL_SRC_FILES := audio_hw.c audio_logic.c
LOCAL_C_INCLUDES += \
	external/tinyalsa/include \
	$(call include-path-for, audio-utils)
LOCAL_SHARED_LIBRARIES := liblog libcutils libtinyalsa libaudioutils
LOCAL_CFLAGS := -Wall -Wextra -Werror
include $(BUILD_SHARED_LIBRARY)
