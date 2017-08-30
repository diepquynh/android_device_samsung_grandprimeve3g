## Specify phone tech before including full_phone	
$(call inherit-product, vendor/cm/config/telephony.mk)

# Release name
PRODUCT_RELEASE_NAME := grandprimeve3g

# Inherit some common CM stuff.
$(call inherit-product, vendor/cm/config/common_full_phone.mk)

# Inherit device configuration
$(call inherit-product, $(LOCAL_PATH)/grandprimeve3g.mk)

## Device identifier. This must come after all inclusions
PRODUCT_DEVICE := grandprimeve3g
PRODUCT_NAME := lineage_grandprimeve3g
PRODUCT_BRAND := samsung
PRODUCT_MODEL := SM-G531H
PRODUCT_MANUFACTURER := samsung
PRODUCT_CHARACTERISTICS := phone

# Stock build fingerprint
BUILD_FINGERPRINT := "samsung/grandprimeve3gxx/grandprimeve3g:5.1.1/LMY48B/G531HDDU0APK2:user/release-keys"
PRIVATE_BUILD_DESC := "grandprimeve3gxx-user 5.1.1 LMY48B G531HDDU0APK2 release-keys"

PRODUCT_PROPERTY_OVERRIDES += \
	ro.build.fingerprint=$(BUILD_FINGERPRINT)
