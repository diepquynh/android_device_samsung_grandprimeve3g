LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
        SPRDMP3Decoder.cpp

LOCAL_C_INCLUDES := \
          frameworks/av/media/libstagefright/include \
          frameworks/native/include/media/openmax    \
	  frameworks/av/include/media/stagefright \
	  $(LOCAL_PATH)/../../..//libstagefrighthw/include

LOCAL_CFLAGS := -DOSCL_EXPORT_REF= -DOSCL_IMPORT_REF=

LOCAL_LDFLAGS += -Wl,--no-warn-shared-textrel

LOCAL_SHARED_LIBRARIES := \
          libstagefright libstagefright_omx libstagefright_foundation libstagefrighthw libutils libui libbinder libdl libcutils liblog 


LOCAL_MODULE := libstagefright_sprd_mp3dec
LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)
