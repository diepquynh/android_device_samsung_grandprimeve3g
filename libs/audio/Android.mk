# Copyright (C) 2011 The Android Open Source Project
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

LOCAL_PATH := $(call my-dir)

ifeq ($(strip $(BOARD_USES_TINYALSA_AUDIO)),true)

include $(CLEAR_VARS)

LOCAL_MODULE := audio.primary.$(TARGET_BOARD_PLATFORM)

LOCAL_MODULE_RELATIVE_PATH := hw

LOCAL_CFLAGS := \
	-D_POSIX_SOURCE \
	-Wno-multichar \
	-g \
	-Wno-unused-parameter

ifeq ($(BOARD_USES_LINE_CALL), true)
LOCAL_CFLAGS += -D_VOICE_CALL_VIA_LINEIN
endif

ifeq ($(TARGET_BUILD_VARIANT),userdebug)
LOCAL_CFLAGS += -DAUDIO_DEBUG
endif

ifneq ($(filter scx35_sc9620referphone scx35_sc9620openphone scx35_sc9620openphone_zt, $(TARGET_BOARD)),)
LOCAL_CFLAGS += -DVB_CONTROL_PARAMETER_V2
endif

ifeq ($(strip $(AUDIO_CONTROL_PARAMETER_V2)), true)
LOCAL_CFLAGS += -DVB_CONTROL_PARAMETER_V2
endif

ifneq (,$(filter sc8830 scx15,$(TARGET_BOARD_PLATFORM)))
LOCAL_CFLAGS += -DAUDIO_SPIPE_TD -D_LPA_IRAM
endif

ifeq ($(BOARD_USES_SS_VOIP), true)
# Default case, Nothing to do.
else
LOCAL_CFLAGS += -DVOIP_DSP_PROCESS
endif


LOCAL_C_INCLUDES += \
	external/tinyalsa/include \
	external/expat/lib \
	system/media/audio_utils/include \
	system/media/audio_effects/include \
	$(LOCAL_PATH)/record_process \

LOCAL_SRC_FILES := \
	audio_hw.c \
	record_process/aud_proc_config.c.arm \
	record_process/aud_filter_calc.c.arm \

ifeq ($(AUDIO_MUX_PIPE),true)
LOCAL_SRC_FILES += audio_mux_pcm.c
LOCAL_CFLAGS += -DAUDIO_MUX_PCM
endif

LOCAL_SHARED_LIBRARIES := \
	liblog \
	libcutils \
	libtinyalsa \
	libtinyalsautils \
	libaudioutils \
	libexpat \
	libdl \
	libvbeffect \
	libvbpga \
	libnvexchange \
	libdumpdata \
	libhardware_legacy \

LOCAL_REQUIRED_MODULES := \
	liblog \
	libcutils \
	libtinyalsa \
	libaudioutils \
	libexpat \
	libdl \
	libvbeffect \
	libvbpga \
	libnvexchange \
	libdumpdata \
	libhardware_legacy

ifeq ($(BOARD_USE_LIBATCHANNEL_WRAPPER),true)
LOCAL_CFLAGS += -DUSE_LIBATCHANNEL_WRAPPER
LOCAL_SHARED_LIBRARIES += libatchannel_wrapper
LOCAL_REQUIRED_MODULES += libatchannel_wrapper
else
LOCAL_SHARED_LIBRARIES += libatchannel
LOCAL_REQUIRED_MODULES += libatchannel
endif

LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)

include $(call all-makefiles-under,$(LOCAL_PATH))
endif

