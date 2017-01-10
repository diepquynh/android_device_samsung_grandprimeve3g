# Copyright (C) 2014 The CyanogenMod Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Inherit from scx30g_v2 common configs
-include device/samsung/scx30g_v2-common/BoardConfigCommon.mk

# Inherit from the proprietary version
-include vendor/samsung/grandprimeve3g/BoardConfigVendor.mk

# Kernel
BOARD_KERNEL_BASE := 0x00000000
BOARD_KERNEL_PAGESIZE := 2048
TARGET_KERNEL_CONFIG := cyanogen_grandprimeve3g_defconfig
TARGET_KERNEL_SOURCE := kernel/samsung/grandprimeve3g
BOARD_MKBOOTIMG_ARGS := --kernel_offset 0x00008000 --ramdisk_offset 0x01000000 --tags_offset 0x00000100 --dt device/samsung/grandprimeve3g/dt.img
TARGET_KERNEL_CROSS_COMPILE_PREFIX := arm-eabi-
KERNEL_TOOLCHAIN := $(ANDROID_BUILD_TOP)/prebuilts/gcc/linux-x86/arm/linaro-6.2/bin

# Resolution
TARGET_SCREEN_HEIGHT := 960
TARGET_SCREEN_WIDTH := 540

# Assert
TARGET_OTA_ASSERT_DEVICE := SM-G531H,SM-G531BT,grandprimeve3g,grandprimeve3gdtv,grandprimeve3gub,grandprimeve3gxx,grandprimeve3gdtvvj
