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

# Inherit from the proprietary version
-include vendor/samsung/core33g/BoardConfigVendor.mk

# Platform
TARGET_ARCH := arm
TARGET_BOARD_PLATFORM := sc8830
TARGET_BOARD_PLATFORM_GPU := mali-400
TARGET_CPU_ABI := armeabi-v7a
TARGET_CPU_ABI2 := armeabi
TARGET_ARCH_VARIANT := armv7-a-neon
TARGET_CPU_VARIANT := cortex-a7
TARGET_CPU_SMP := true
ARCH_ARM_HAVE_TLS_REGISTER := true
TARGET_BOOTLOADER_BOARD_NAME := sc7730s
TARGET_GLOBAL_CFLAGS += -mtune=cortex-a7 -mfpu=neon -mfloat-abi=softfp
TARGET_GLOBAL_CPPFLAGS += -mtune=cortex-a7 -mfpu=neon -mfloat-abi=softfp
BOARD_VENDOR := samsung

# Config u-boot
TARGET_NO_BOOTLOADER := true

BOARD_BOOTIMAGE_PARTITION_SIZE := 16777216
BOARD_RECOVERYIMAGE_PARTITION_SIZE := 16777216
BOARD_SYSTEMIMAGE_PARTITION_SIZE := 1258291200
BOARD_USERDATAIMAGE_PARTITION_SIZE := 6094323712
BOARD_CACHEIMAGE_PARTITION_SIZE := 209715200
BOARD_FLASH_BLOCK_SIZE := 131072
TARGET_USERIMAGES_USE_EXT4 := true
BOARD_HAS_LARGE_FILESYSTEM := true

# Kernel
BOARD_KERNEL_BASE := 0x00000000
BOARD_KERNEL_CMDLINE := console=ttyS1,115200n8
BOARD_KERNEL_PAGESIZE := 2048
TARGET_KERNEL_CONFIG := cyanogen_core33g_defconfig
TARGET_KERNEL_SOURCE := kernel/samsung/core33g
BOARD_MKBOOTIMG_ARGS := --kernel_offset 0x00008000 --ramdisk_offset 0x01000000 --tags_offset 0x00000100 --dt device/samsung/core33g/dt.img

# RIL
BOARD_RIL_CLASS := ../../../device/samsung/core33g/ril

# Bluetooth
BOARD_HAVE_BLUETOOTH := true
BOARD_HAVE_BLUETOOTH_BCM := true
BOARD_BLUETOOTH_BDROID_BUILDCFG_INCLUDE_DIR := device/samsung/core33g/bluetooth
BOARD_BLUEDROID_VENDOR_CONF := device/samsung/core33g/bluetooth/libbt_vndcfg.txt

# Wifi
BOARD_WLAN_DEVICE := bcmdhd
BOARD_WLAN_DEVICE_REV := bcm4343
WPA_SUPPLICANT_VERSION := VER_0_8_X
BOARD_WPA_SUPPLICANT_DRIVER := NL80211
BOARD_WPA_SUPPLICANT_PRIVATE_LIB := lib_driver_cmd_bcmdhd
BOARD_HOSTAPD_DRIVER := NL80211
BOARD_HOSTAPD_PRIVATE_LIB := lib_driver_cmd_bcmdhd
WIFI_DRIVER_FW_PATH_PARAM := "/sys/module/dhd/parameters/firmware_path"
WIFI_DRIVER_FW_PATH_STA := "/system/etc/wifi/bcmdhd_sta.bin"
WIFI_DRIVER_FW_PATH_AP := "/system/etc/wifi/bcmdhd_apsta.bin"
WIFI_DRIVER_NVRAM_PATH_PARAM := "/sys/module/dhd/parameters/nvram_path"
WIFI_DRIVER_NVRAM_PATH := "/system/etc/wifi/nvram_net.txt"
WIFI_BAND := 802_11_ABG
BOARD_HAVE_SAMSUNG_WIFI := true

# Hardware rendering
BOARD_EGL_CFG := device/samsung/core33g/configs/egl.cfg
BOARD_USE_MHEAP_SCREENSHOT := true
BOARD_EGL_WORKAROUND_BUG_10194508 := true
TARGET_RUNNING_WITHOUT_SYNC_FRAMEWORK := true
HWUI_COMPILE_FOR_PERF := true
USE_SPRD_HWCOMPOSER := true
USE_OPENGL_RENDERER := true
USE_OVERLAY_COMPOSER_GPU := true
DEVICE_FORCE_VIDEO_GO_OVERLAYCOMPOSER := true
COMMON_GLOBAL_CFLAGS += -DSC8830_HWC

# Include an expanded selection of fonts
EXTENDED_FONT_FOOTPRINT := true

# Resolution
TARGET_SCREEN_HEIGHT := 800
TARGET_SCREEN_WIDTH := 480

# Audio
BOARD_USES_TINYALSA_AUDIO := true
BOARD_USES_SS_VOIP := true
BOARD_USE_LIBATCHANNEL_WRAPPER := true

# Media
COMMON_GLOBAL_CFLAGS += -DADD_LEGACY_ACQUIRE_BUFFER_SYMBOL
COMMON_GLOBAL_CFLAGS += -DBOARD_CANT_REALLOCATE_OMX_BUFFERS

# Board specific features
BOARD_USE_SAMSUNG_COLORFORMAT := true
BOARD_NEEDS_MEMORYHEAPION_SPRD := true
COMMON_GLOBAL_CFLAGS += -DSPRD_HARDWARE

# healthd
BOARD_HAL_STATIC_LIBRARIES := libhealthd-core33g.sc8830

# Use the CM PowerHAL
TARGET_USES_CM_POWERHAL := true
CM_POWERHAL_EXTENSION := sc8830
TARGET_POWERHAL_VARIANT := cm

# Init
TARGET_NR_SVC_SUPP_GIDS := 24
TARGET_PROVIDES_INIT_RC := true

# Recovery
BOARD_HAS_NO_REAL_SDCARD := true
BOARD_HAS_NO_SELECT_BUTTON := true
TARGET_RECOVERY_PIXEL_FORMAT := "RGBX_8888"

# Assert
TARGET_OTA_ASSERT_DEVICE := SM-G360H,SM-G360HU,core33g,core33gdd,core33gdx

# SELinux
#BOARD_SEPOLICY_DIRS += device/samsung/core33g/sepolicy
#BOARD_SEPOLICY_UNION :=	\
#	file.te	\
#	file_contexts \
#	seapp_contexts \
#	theme.te \
#	healthd.te \
#	init.te \
#	init_shell.te \
#	installd.te \
#	netd.te \
#	shell.te \
#	system.te \
#	untrusted_app.te \
#	vold.te	\
#	zygote.te

# Use dmalloc() for such low memory devices like us
MALLOC_IMPL := dlmalloc

# Use prebuilt webviewchromium to cut down build time
PRODUCT_PREBUILT_WEBVIEWCHROMIUM := yes

# Enable dex-preoptimization to speed up the first boot sequence
WITH_DEXPREOPT := true
#WITH_DEXPREOPT_BOOT_IMG_ONLY := true
WITH_DEXPREOPT_PIC := true
WITH_DEXPREOPT_COMP := false

# Charger
BOARD_CHARGER_ENABLE_SUSPEND := true
BOARD_CHARGING_MODE_BOOTING_LPM := /sys/class/power_supply/battery/batt_lp_charging
CHARGING_ENABLED_PATH := /sys/class/power_supply/battery/batt_lp_charging
BACKLIGHT_PATH := /sys/class/backlight/panel/brightness


# CMHW
BOARD_HARDWARE_CLASS := device/samsung/core33g/cmhw/

# TWRP
##RECOVERY_VARIANT := twrp
RECOVERY_GRAPHICS_USE_LINELENGTH := true
SP1_NAME := "internal_sd"
SP1_BACKUP_METHOD := files
SP1_MOUNTABLE := 1
TW_INTERNAL_STORAGE_PATH := "/data/media/0"
TW_INTERNAL_STORAGE_MOUNT_POINT := "data"
TW_EXTERNAL_STORAGE_PATH := "/external_sd"
TW_EXTERNAL_STORAGE_MOUNT_POINT := "external_sd"
TW_DEFAULT_EXTERNAL_STORAGE := true
TW_FLASH_FROM_STORAGE := true
TW_NO_REBOOT_BOOTLOADER := true
TW_CUSTOM_CPU_TEMP_PATH := "/sys/devices/platform/sec-thermistor/temperature"
TWHAVE_SELINUX := true
TARGET_RECOVERY_INITRC := device/samsung/core33g/etc/init.rc
TARGET_RECOVERY_FSTAB := device/samsung/core33g/ramdisk/recovery.fstab
TW_HAS_DOWNLOAD_MODE := true
DEVICE_RESOLUTION := 480x800
TW_THEME := portrait_mdpi
BOARD_USE_CUSTOM_RECOVERY_FONT := \"roboto_10x18.h\"

# UMS
BOARD_UMS_LUNFILE := "/sys/devices/virtual/android_usb/android0/f_mass_storage/lun/file"
TARGET_USE_CUSTOM_LUN_FILE_PATH := "/sys/devices/virtual/android_usb/android0/f_mass_storage/lun/file"
