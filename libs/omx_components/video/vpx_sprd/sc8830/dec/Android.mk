LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
	SPRDVPXDecoder.cpp

LOCAL_C_INCLUDES := \
	frameworks/av/media/libstagefright/include \
	frameworks/native/include/media/openmax \
	frameworks/native/include/media/hardware \
	frameworks/native/include/ui \
	frameworks/native/include/utils \
	frameworks/native/include/media/hardware \
	$(LOCAL_PATH)/../../../../../gralloc \
	$(LOCAL_PATH)/../../../../../libstagefrighthw/include \
	$(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include/video

LOCAL_CFLAGS := -DOSCL_EXPORT_REF= -DOSCL_IMPORT_REF=

LOCAL_ARM_MODE := arm

LOCAL_SHARED_LIBRARIES := \
	libstagefright libstagefright_omx libstagefright_foundation libstagefrighthw libutils libui libbinder libdl liblog

LOCAL_MODULE := libstagefright_sprd_vpxdec
LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)

include $(call all-makefiles-under,$(LOCAL_PATH))

