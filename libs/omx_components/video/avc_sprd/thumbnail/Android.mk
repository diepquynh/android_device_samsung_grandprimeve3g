LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
	SoftSPRDAVC.cpp \

LOCAL_C_INCLUDES := \
        frameworks/av/media/libstagefright/include \
        frameworks/native/include/media/openmax \
	$(LOCAL_PATH)/../../../../libstagefrighthw/include

LOCAL_CFLAGS := -DOSCL_EXPORT_REF= -DOSCL_IMPORT_REF=
LOCAL_ARM_MODE := arm

LOCAL_SHARED_LIBRARIES := \
        libstagefright libstagefright_omx libstagefright_foundation libstagefrighthw libutils libdl liblog

LOCAL_MODULE := libstagefright_sprd_soft_h264dec
LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)

