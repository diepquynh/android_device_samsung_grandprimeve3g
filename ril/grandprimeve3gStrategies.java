/*
 * Copyright (C) 2016 Android Open Source Project
 *
 * Copyright (C) 2016 The CyanogenMod Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package com.android.internal.telephony;

import android.os.SystemProperties;

import android.telephony.Rlog;
import android.telephony.SubscriptionManager;

/**
 *
 * @hide
 */
public class grandprimeve3gStrategies extends TelephonyStrategies {

    private static final String LOG_TAG = grandprimeve3gStrategies.class.getSimpleName();

    public grandprimeve3gStrategies() { }

    @Override
    public void setTelephonyProperty(int phoneId, String property, String value) {
        if (SubscriptionManager.isValidPhoneId(phoneId)) {
            String actualProp = getActualProp(phoneId, property);
            String propVal = value == null ? "" : value;
            if (actualProp.length() > SystemProperties.PROP_NAME_MAX
                    || propVal.length() > SystemProperties.PROP_VALUE_MAX) {
                Rlog.d(LOG_TAG, "setTelephonyProperty: property to long" +
                        " phoneId=" + phoneId +
                        " property=" + property +
                        " value=" + value +
                        " actualProp=" + actualProp +
                        " propVal" + propVal);
            } else {
                Rlog.d(LOG_TAG, "setTelephonyProperty: success" +
                        " phoneId=" + phoneId +
                        " property=" + property +
                        " value=" + value +
                        " actualProp=" + actualProp +
                        " propVal=" + propVal);
                SystemProperties.set(actualProp, propVal);
            }
        } else {
            Rlog.d(LOG_TAG, "setTelephonyProperty: invalid phoneId=" + phoneId +
                    " property=" + property +
                    " value=" + value);
        }
    }

    @Override
    public String getTelephonyProperty(int phoneId, String property, String defaultVal) {
        String result = defaultVal;
        if (SubscriptionManager.isValidPhoneId(phoneId)) {
            String actualProp = getActualProp(phoneId, property);
            String propVal = SystemProperties.get(actualProp);
            if (!propVal.isEmpty()) {
                result = propVal;
                Rlog.d(LOG_TAG, "getTelephonyProperty: return result=" + result +
                        " phoneId=" + phoneId +
                        " property=" + property +
                        " defaultVal=" + defaultVal +
                        " actualProp=" + actualProp +
                        " propVal=" + propVal);
            }
        } else {
            Rlog.e(LOG_TAG, "getTelephonyProperty: invalid phoneId=" + phoneId +
                    " property=" + property +
                    " defaultVal=" + defaultVal);
        }
        return result;
    }

    private String getActualProp(int phoneId, String prop) {
        return phoneId <= 0 ? prop : prop + (phoneId + 1);
    }
}
