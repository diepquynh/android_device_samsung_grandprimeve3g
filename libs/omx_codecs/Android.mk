LOCAL_PATH := $(call my-dir)

define my_add_package
include $(CLEAR_VARS)
LOCAL_MODULE := $1
LOCAL_MODULE_TAGS := optional eng
LOCAL_MODULE_CLASS := SHARED_LIBRARIES
LOCAL_MODULE_PATH := $(TARGET_OUT)/lib
LOCAL_SRC_FILES := $1
include $(BUILD_PREBUILT)
endef

my_packages := \
	libomx_aacdec_sprd.so \
	libomx_avcdec_hw_sprd.so \
	libomx_avcdec_sw_sprd.so \
	libomx_avcenc_hw_sprd.so \
	libomx_m4vh263dec_hw_sprd.so \
	libomx_m4vh263dec_sw_sprd.so \
	libomx_m4vh263enc_hw_sprd.so \
	libomx_mp3dec_sprd.so \
	libomx_vpxdec_hw_sprd.so

$(foreach p,$(my_packages),$(eval $(call my_add_package,$(p))))
