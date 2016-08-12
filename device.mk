# Copyright (C) 2014 The CyanogenMod Project
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

# The gps config appropriate for this device
$(call inherit-product, device/common/gps/gps_us_supl.mk)

$(call inherit-product-if-exists, vendor/samsung/grandprimeve3g/grandprimeve3g-vendor.mk)

DEVICE_PACKAGE_OVERLAYS += $(LOCAL_PATH)/overlay

# Boot animation
TARGET_SCREEN_HEIGHT := 960
TARGET_SCREEN_WIDTH := 540

# Languages
PRODUCT_PROPERTY_OVERRIDES += \
	ro.product.locale.language=en \
	ro.product.locale.region=GB

# Rootdir files
ROOTDIR_FILES := \
	$(LOCAL_PATH)/rootdir/init.rc \
	$(LOCAL_PATH)/rootdir/init.board.rc \
	$(LOCAL_PATH)/rootdir/init.recovery.board.rc \
	$(LOCAL_PATH)/rootdir/init.sc8830.rc \
	$(LOCAL_PATH)/rootdir/init.sc8830.usb.rc \
	$(LOCAL_PATH)/rootdir/init.sc8830_ss.rc \
	$(LOCAL_PATH)/rootdir/init.grandprimeve3g.rc \
	$(LOCAL_PATH)/rootdir/init.grandprimeve3g_base.rc \
	$(LOCAL_PATH)/rootdir/init.wifi.rc \
	$(LOCAL_PATH)/rootdir/ueventd.sc8830.rc \
	$(LOCAL_PATH)/rootdir/fstab.sc8830

PRODUCT_COPY_FILES += \
	$(foreach f,$(ROOTDIR_FILES),$(f):root/$(notdir $(f)))

# enable Google-specific location features,
# like NetworkLocationProvider and LocationCollector
PRODUCT_PROPERTY_OVERRIDES += \
	ro.com.google.locationfeatures=1 \
	ro.com.google.networklocation=1

# Dalvik heap config
$(call inherit-product, frameworks/native/build/phone-hdpi-2048-dalvik-heap.mk)
$(call inherit-product, frameworks/native/build/phone-xxhdpi-2048-hwui-memory.mk)

# WiFi
$(call inherit-product, hardware/broadcom/wlan/bcmdhd/firmware/bcm4343/device-bcm.mk)

# For userdebug builds
ADDITIONAL_DEFAULT_PROPERTIES += \
	ro.secure=0 \
	ro.adb.secure=0 \
	ro.debuggable=1 \
	persist.service.adb.enable=1

PRODUCT_BUILD_PROP_OVERRIDES += BUILD_UTC_DATE=0
PRODUCT_NAME := full_grandprimeve3g
PRODUCT_DEVICE := grandprimeve3g
PRODUCT_BRAND := samsung
PRODUCT_MODEL := SM-G531H
