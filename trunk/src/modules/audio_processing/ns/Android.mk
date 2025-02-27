# Copyright (c) 2011 The WebRTC project authors. All Rights Reserved.
#
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file in the root of the source
# tree. An additional intellectual property rights grant can be found
# in the file PATENTS.  All contributing project authors may
# be found in the AUTHORS file in the root of the source tree.

#############################
# Build the non-neon library.
LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

include $(LOCAL_PATH)/../../../../android-webrtc.mk

LOCAL_MODULE_CLASS := STATIC_LIBRARIES
LOCAL_MODULE := libwebrtc_ns
LOCAL_MODULE_TAGS := optional
LOCAL_GENERATED_SOURCES :=
LOCAL_SRC_FILES := \
    noise_suppression_x.c \
    nsx_core.c

# Files for floating point.
# noise_suppression.c ns_core.c 

# Flags passed to both C and C++ files.
LOCAL_CFLAGS := $(MY_WEBRTC_COMMON_DEFS)

LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/interface \
    $(LOCAL_PATH)/../utility \
    $(LOCAL_PATH)/../../.. \
    $(LOCAL_PATH)/../../../common_audio/signal_processing/include \
    $(LOCAL_PATH)/../../../system_wrappers/interface

LOCAL_STATIC_LIBRARIES += libwebrtc_system_wrappers

LOCAL_SHARED_LIBRARIES := \
    libcutils \
    libdl \
    libstlport

ifndef NDK_ROOT
include external/stlport/libstlport.mk
endif
include $(BUILD_STATIC_LIBRARY)

#############################
# Build the neon library.

include $(CLEAR_VARS)

LOCAL_MODULE_CLASS := STATIC_LIBRARIES
LOCAL_MODULE := libwebrtc_ns_neon
LOCAL_MODULE_TAGS := optional
LOCAL_GENERATED_SOURCES :=

LOCAL_SRC_FILES := nsx_core_neon.c

# Flags passed to both C and C++ files.
LOCAL_CFLAGS := \
    $(MY_WEBRTC_COMMON_DEFS) \
    -mfpu=neon \
    -flax-vector-conversions

LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/interface \
    $(LOCAL_PATH)/../../.. \
    $(LOCAL_PATH)/../../../common_audio/signal_processing/include

ifndef NDK_ROOT
include external/stlport/libstlport.mk
endif
include $(BUILD_STATIC_LIBRARY)
