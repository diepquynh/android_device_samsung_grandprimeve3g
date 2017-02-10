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

LOCAL_PATH := device/samsung/grandprimeve3g

# Overlays
DEVICE_PACKAGE_OVERLAYS += $(LOCAL_PATH)/overlay

# Inherit from vendor tree
$(call inherit-product-if-exists, vendor/samsung/grandprimeve3g/grandprimeve3g-vendor.mk)

# Inherit from scx30g_v2-common device configuration
$(call inherit-product, device/samsung/scx30g_v2-common/common.mk)

DEVICE_PACKAGE_OVERLAYS += $(LOCAL_PATH)/overlay

# Boot animation
TARGET_SCREEN_HEIGHT := 960
TARGET_SCREEN_WIDTH := 540

# Media config
MEDIA_CONFIGS := \
	$(LOCAL_PATH)/media/media_profiles.xml

PRODUCT_COPY_FILES += \
	$(foreach f,$(MEDIA_CONFIGS),$(f):system/etc/$(notdir $(f)))

# Rootdir files
ROOTDIR_FILES := \
	$(LOCAL_PATH)/rootdir/init.sc8830.rc \
	$(LOCAL_PATH)/rootdir/init.sc8830.usb.rc \
	$(LOCAL_PATH)/rootdir/init.grandprimeve3g_base.rc \
	$(LOCAL_PATH)/rootdir/ueventd.sc8830.rc

PRODUCT_COPY_FILES += \
	$(foreach f,$(ROOTDIR_FILES),$(f):root/$(notdir $(f)))

# Permissions
PERMISSIONS_XML_FILES := \
	frameworks/native/data/etc/android.hardware.sensor.compass.xml

PRODUCT_COPY_FILES += \
	$(foreach f,$(PERMISSIONS_XML_FILES),$(f):system/etc/permissions/$(notdir $(f)))

# Set those variables here to overwrite the inherited values.
PRODUCT_NAME := full_grandprimeve3g
PRODUCT_DEVICE := grandprimeve3g
PRODUCT_BRAND := samsung
PRODUCT_MANUFACTURER := samsung
PRODUCT_MODEL := SM-G531H
