# 
# Copyright (C) 2010 ARM Limited. All rights reserved.
# 
# Copyright (C) 2008 The Android Open Source Project
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

ifneq ($(TARGET_SIMULATOR),true)

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_PRELINK_MODULE := false
LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/hw

LOCAL_MODULE := gralloc.$(TARGET_BOARD_PLATFORM)
LOCAL_MODULE_TAGS := optional

SHARED_MEM_LIBS := libion libhardware
LOCAL_SHARED_LIBRARIES := liblog libcutils libGLESv1_CM $(SHARED_MEM_LIBS)

LOCAL_C_INCLUDES += \
    $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include/video/ \
    $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include/ \
    $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/
LOCAL_CFLAGS:= -DLOG_TAG=\"gralloc.$(TARGET_BOARD_PLATFORM)\"

ifeq ($(strip $(USE_UI_OVERLAY)),true)
        LOCAL_CFLAGS += -DUSE_UI_OVERLAY
endif

ifneq ($(strip $(TARGET_BUILD_VARIANT)), user)
        LOCAL_CFLAGS += -DDUMP_FB
endif

LOCAL_SRC_FILES := \
	gralloc_module.cpp \
	alloc_device.cpp \
	framebuffer_device.cpp \
	dump_bmp.cpp

#LOCAL_CFLAGS+= -DMALI_VSYNC_EVENT_REPORT_ENABLE
include $(BUILD_SHARED_LIBRARY)

endif
