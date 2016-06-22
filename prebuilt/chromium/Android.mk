LOCAL_PATH := $(call my-dir)

ifeq ($(PRODUCT_PREBUILT_WEBVIEWCHROMIUM),yes)

include $(CLEAR_VARS)

LOCAL_MODULE := webview
LOCAL_SRC_FILES := webview.apk
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_SUFFIX := $(COMMON_ANDROID_PACKAGE_SUFFIX)
LOCAL_MODULE_CLASS := APPS
LOCAL_CERTIFICATE := platform

$(shell mkdir -p $(TARGET_OUT_SHARED_LIBRARIES))
$(shell cp $(LOCAL_PATH)/libwebviewchromium.so $(TARGET_OUT_SHARED_LIBRARIES))

LOCAL_JNI_SHARED_LIBRARIES := libwebviewchromium

include $(BUILD_PREBUILT)

include $(CLEAR_VARS)

LOCAL_MODULE := libwebviewchromium_plat_support.so
LOCAL_MODULE_TAGS := optional eng
LOCAL_MODULE_CLASS := SHARED_LIBRARIES
LOCAL_MODULE_PATH := $(TARGET_OUT)/lib
LOCAL_SRC_FILES := libwebviewchromium_plat_support.so

include $(BUILD_PREBUILT)

include $(CLEAR_VARS)

LOCAL_MODULE := libwebviewchromium_loader.so
LOCAL_MODULE_TAGS := optional eng
LOCAL_MODULE_CLASS := SHARED_LIBRARIES
LOCAL_MODULE_PATH := $(TARGET_OUT)/lib
LOCAL_SRC_FILES := libwebviewchromium_loader.so

include $(BUILD_PREBUILT)

endif # PRODUCT_PREBUILT_WEBVIEWCHROMIUM
