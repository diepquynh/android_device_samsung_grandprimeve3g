LOCAL_PATH := $(call my-dir)

#Building omxregister-bellagio binary which will be placed in the /system/bin folder
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
	src/common.c \
	src/omxregister.c \
	src/omxregister.h 

LOCAL_MODULE_TAGS := eng
LOCAL_MODULE := omxregister-bellagio

LOCAL_CFLAGS := -DOMXILCOMPONENTSPATH=\"/system/lib\" -DCONFIG_DEBUG_LEVEL=0

LOCAL_C_INCLUDES := \
	$(LOCAL_PATH)/bellagio \
	$(TOP)/frameworks/av/include/media/stagefright/ \
	$(LOCAL_PATH)/../libstagefrighthw/include/

LOCAL_SHARED_LIBRARIES :=       \
        libutils                \
        libdl			\
	liblog

	
include $(BUILD_EXECUTABLE)

# Building the libomxil-bellagio 
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
 	src/common.c \
	src/content_pipe_file.c \
	src/content_pipe_inet.c \
	src/omx_create_loaders_linux.c \
	src/omxcore.c \
 	src/omx_reference_resource_manager.c \
	src/omxregister.c \
	src/queue.c \
	src/st_static_component_loader.c \
	src/tsemaphore.c \
	src/utils.c \
 	src/base/OMXComponentRMExt.c \
	src/base/omx_base_audio_port.c \
	src/base/omx_base_clock_port.c \
	src/base/omx_base_component.c \
	src/base/omx_base_filter.c \
	src/base/omx_base_image_port.c \
	src/base/omx_base_port.c \
	src/base/omx_base_sink.c \
	src/base/omx_base_source.c \
	src/base/omx_base_video_port.c \
 	src/core_extensions/OMXCoreRMExt.c

LOCAL_MODULE_TAGS := eng
LOCAL_MODULE := libomxil-bellagio

LOCAL_CFLAGS :=  -DOMXILCOMPONENTSPATH=\"/system/lib\" -DCONFIG_DEBUG_LEVEL=0 -DCNM_SPRD_PLATFORM

LOCAL_STATIC_LIBRARIES := 

#LOCAL_LDLIBS += -lpthread
LOCAL_SHARED_LIBRARIES :=       \
        libutils                \
	libcutils               \
        libdl			\
	liblog

LOCAL_C_INCLUDES := \
	$(LOCAL_PATH)/bellagio \
	$(TOP)/frameworks/av/include/media/stagefright/ \
	$(LOCAL_PATH)/../libstagefrighthw/include/

include $(BUILD_SHARED_LIBRARY)

