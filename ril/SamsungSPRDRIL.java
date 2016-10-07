/*
 * Copyright (C) 2012 The CyanogenMod Project
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

import static com.android.internal.telephony.RILConstants.*;

import android.content.Context;
import android.os.AsyncResult;
import android.os.Message;
import android.os.Parcel;
import android.os.SystemProperties;
import android.telephony.Rlog;
import android.telephony.PhoneNumberUtils;

import com.android.internal.telephony.uicc.SpnOverride;
import com.android.internal.telephony.RILConstants;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;

/**
 * Custom RIL to handle unique behavior of SPRD RIL
 *
 * {@hide}
 */
public class SamsungSPRDRIL extends RIL implements CommandsInterface {

    public SamsungSPRDRIL(Context context, int networkMode, int cdmaSubscription) {
        this(context, networkMode, cdmaSubscription, null);
    }

    public SamsungSPRDRIL(Context context, int networkMode,
            int cdmaSubscription, Integer instanceId) {
        super(context, networkMode, cdmaSubscription, instanceId);
        mQANElements = SystemProperties.getInt("ro.telephony.ril_qanelements", 6);
    }

    @Override
    public void
    dial(String address, int clirMode, UUSInfo uusInfo, Message result) {
        RILRequest rr = RILRequest.obtain(RIL_REQUEST_DIAL, result);

        rr.mParcel.writeString(address);
        rr.mParcel.writeInt(clirMode);
        rr.mParcel.writeInt(0);     // CallDetails.call_type
        rr.mParcel.writeInt(1);     // CallDetails.call_domain
        rr.mParcel.writeString(""); // CallDetails.getCsvFromExtras

        if (uusInfo == null) {
            rr.mParcel.writeInt(0); // UUS information is absent
        } else {
            rr.mParcel.writeInt(1); // UUS information is present
            rr.mParcel.writeInt(uusInfo.getType());
            rr.mParcel.writeInt(uusInfo.getDcs());
            rr.mParcel.writeByteArray(uusInfo.getUserData());
        }

        if (RILJ_LOGD) riljLog(rr.serialString() + "> " + requestToString(rr.mRequest));

        send(rr);
    }

    @Override
    public void setUiccSubscription(int slotId, int appIndex, int subId,
            int subStatus, Message result) {
        if (RILJ_LOGD) riljLog("setUiccSubscription" + slotId + " " + appIndex + " " + subId + " " + subStatus);

        // Fake response (note: should be sent before mSubscriptionStatusRegistrants or
        // SubscriptionManager might not set the readiness correctly)
        AsyncResult.forMessage(result, 0, null);
        result.sendToTarget();

        // TODO: Actually turn off/on the radio (and don't fight with the ServiceStateTracker)
        if (subStatus == 1 /* ACTIVATE */) {
            // Subscription changed: enabled
            if (mSubscriptionStatusRegistrants != null) {
                mSubscriptionStatusRegistrants.notifyRegistrants(
                        new AsyncResult (null, new int[] {1}, null));
            }
        } else if (subStatus == 0 /* DEACTIVATE */) {
            // Subscription changed: disabled
            if (mSubscriptionStatusRegistrants != null) {
                mSubscriptionStatusRegistrants.notifyRegistrants(
                        new AsyncResult (null, new int[] {0}, null));
            }
        }
    }

    @Override
    public void setDataAllowed(boolean allowed, Message result) {
        int simId = mInstanceId == null ? 0 : mInstanceId;
        if (allowed) {
            riljLog("Setting data subscription to sim [" + simId + "]");
            invokeOemRilRequestRaw(new byte[] {0x9, 0x4}, result);
        } else {
            riljLog("Do nothing when turn-off data on sim [" + simId + "]");
            if (result != null) {
                AsyncResult.forMessage(result, 0, null);
                result.sendToTarget();
            }
        }
    }

    @Override
    public void getHardwareConfig (Message result) {
        riljLog("Ignoring call to 'getHardwareConfig'");
        if (result != null) {
            AsyncResult.forMessage(result, null, new CommandException(
                    CommandException.Error.REQUEST_NOT_SUPPORTED));
            result.sendToTarget();
        }
    }

    @Override
    protected RadioState getRadioStateFromInt(int stateInt) {
        RadioState state;

        /* RIL_RadioState ril.h */
        switch(stateInt) {
            case 0: state = RadioState.RADIO_OFF; break;
            case 1: state = RadioState.RADIO_UNAVAILABLE; break;
            case 2:
            case 3:
            case 4:
            case 5:
            case 6:
            case 7:
            case 8:
            case 9:
            case 10:
            case 13: state = RadioState.RADIO_ON; break;

            default:
                throw new RuntimeException(
                            "Unrecognized RIL_RadioState: " + stateInt);
        }
        return state;
    }

    @Override
    protected void notifyRegistrantsRilConnectionChanged(int rilVer) {
        super.notifyRegistrantsRilConnectionChanged(rilVer);
        if (rilVer != -1) {
            if (mInstanceId != null) {
                riljLog("Enable simultaneous data/voice on Multi-SIM");
                invokeOemRilRequestSprd((byte) 3, (byte) 1, null);
            } else {
                riljLog("Set data subscription to allow data in either SIM slot when using single SIM mode");
                setDataAllowed(true, null);
            }
        }
    }

    @Override
    protected Object
    responseCallList(Parcel p) {
        int num;
        int voiceSettings;
        ArrayList<DriverCall> response;
        DriverCall dc;

        num = p.readInt();
        response = new ArrayList<DriverCall>(num);

        if (RILJ_LOGV) {
            riljLog("responseCallList: num=" + num +
                    " mEmergencyCallbackModeRegistrant=" + mEmergencyCallbackModeRegistrant +
                    " mTestingEmergencyCall=" + mTestingEmergencyCall.get());
        }
        for (int i = 0 ; i < num ; i++) {
            dc = new DriverCall();

            dc.state = DriverCall.stateFromCLCC(p.readInt());
            // & 0xff to truncate to 1 byte added for us, not in RIL.java
            dc.index = p.readInt() & 0xff;
            dc.TOA = p.readInt();
            dc.isMpty = (0 != p.readInt());
            dc.isMT = (0 != p.readInt());
            dc.als = p.readInt();
            voiceSettings = p.readInt();
            dc.isVoice = (0 != voiceSettings);
            boolean isVideo = (0 != p.readInt());
            int call_type = p.readInt();            // Samsung CallDetails
            int call_domain = p.readInt();          // Samsung CallDetails
            String csv = p.readString();            // Samsung CallDetails
            dc.isVoicePrivacy = (0 != p.readInt());
            dc.number = p.readString();
            dc.numberPresentation = DriverCall.presentationFromCLIP(p.readInt());
            dc.name = p.readString();
            dc.namePresentation = p.readInt();
            int uusInfoPresent = p.readInt();
            if (uusInfoPresent == 1) {
                dc.uusInfo = new UUSInfo();
                dc.uusInfo.setType(p.readInt());
                dc.uusInfo.setDcs(p.readInt());
                byte[] userData = p.createByteArray();
                dc.uusInfo.setUserData(userData);
                riljLogv(String.format("Incoming UUS : type=%d, dcs=%d, length=%d",
                                dc.uusInfo.getType(), dc.uusInfo.getDcs(),
                                dc.uusInfo.getUserData().length));
                riljLogv("Incoming UUS : data (string)="
                        + new String(dc.uusInfo.getUserData()));
                riljLogv("Incoming UUS : data (hex): "
                        + IccUtils.bytesToHexString(dc.uusInfo.getUserData()));
            } else {
                riljLogv("Incoming UUS : NOT present!");
            }

            // Make sure there's a leading + on addresses with a TOA of 145
            dc.number = PhoneNumberUtils.stringFromStringAndTOA(dc.number, dc.TOA);

            response.add(dc);

            if (dc.isVoicePrivacy) {
                mVoicePrivacyOnRegistrants.notifyRegistrants();
                riljLog("InCall VoicePrivacy is enabled");
            } else {
                mVoicePrivacyOffRegistrants.notifyRegistrants();
                riljLog("InCall VoicePrivacy is disabled");
            }
        }

        Collections.sort(response);

        if ((num == 0) && mTestingEmergencyCall.getAndSet(false)) {
            if (mEmergencyCallbackModeRegistrant != null) {
                riljLog("responseCallList: call ended, testing emergency call," +
                            " notify ECM Registrants");
                mEmergencyCallbackModeRegistrant.notifyRegistrant();
            }
        }

        return response;
    }

    private void invokeOemRilRequestSprd(byte key, byte value, Message response) {
        invokeOemRilRequestRaw(new byte[] { 'S', 'P', 'R', 'D', key, value }, response);
    }
}
