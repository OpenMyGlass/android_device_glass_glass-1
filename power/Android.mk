# SPDX-License-Identifier: Apache-2.0
# power.glass_1: see docs/hals.md. tools/build-hals.py reads LOCAL_MODULE, LOCAL_SRC_FILES,
# LOCAL_CFLAGS and LOCAL_SHARED_LIBRARIES from this file to build it in CI.
LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := power.glass_1
LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_MODULE_TAGS := optional
LOCAL_SRC_FILES := power.c power_logic.c
LOCAL_SHARED_LIBRARIES := liblog
LOCAL_CFLAGS := -Wall -Wextra -Werror
include $(BUILD_SHARED_LIBRARY)
