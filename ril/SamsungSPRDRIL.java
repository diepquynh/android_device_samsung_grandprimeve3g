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
import android.telephony.ModemActivityInfo;

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

    public SamsungSPRDRIL(Context context, int preferredNetworkType, int cdmaSubscription) {
        this(context, preferredNetworkType, cdmaSubscription, null);
    }

    public SamsungSPRDRIL(Context context, int preferredNetworkType,
            int cdmaSubscription, Integer instanceId) {
        super(context, preferredNetworkType, cdmaSubscription, instanceId);
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
    public void setUiccSubscription(int appIndex, boolean activate, Message result) {
        riljLog("setUiccSubscription " + appIndex + " " + activate);

        // Fake response (note: should be sent before mSubscriptionStatusRegistrants or
        // SubscriptionManager might not set the readiness correctly)
        AsyncResult.forMessage(result, 0, null);
        result.sendToTarget();

        // TODO: Actually turn off/on the radio (and don't fight with the ServiceStateTracker)
        if (mSubscriptionStatusRegistrants != null)
            mSubscriptionStatusRegistrants.notifyRegistrants(
                    new AsyncResult (null, new int[] { activate ? 1 : 0 }, null));
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
    public void getRadioCapability(Message response) {
        String rafString = mContext.getResources().getString(
            com.android.internal.R.string.config_radio_access_family);
        riljLog("getRadioCapability: returning static radio capability [" + rafString + "]");
        if (response != null) {
            Object ret = makeStaticRadioCapability();
            AsyncResult.forMessage(response, ret, null);
            response.sendToTarget();
        }
    }

    @Override
    protected RadioState getRadioStateFromInt(int stateInt) {
        RadioState state;
        switch (stateInt) {
        case 13: state = RadioState.RADIO_ON; break;
        default:
            state = super.getRadioStateFromInt(stateInt);
        }
        return state;
    }

     @Override
    public void getHardwareConfig(Message response) {
        unsupportedRequest("getHardwareConfig", response);
    }

    @Override
    public void startLceService(int reportIntervalMs, boolean pullMode, Message response) {
        unsupportedRequest("startLceService", response);
    }

    @Override
    public void stopLceService(Message response) {
        unsupportedRequest("stopLceService", response);
    }

    @Override
    public void pullLceData(Message response) {
        unsupportedRequest("pullLceData", response);
    }

    @Override
    protected Object responseFailCause(Parcel p) {
        int numInts = p.readInt();
        int response[] = new int[numInts];
        for (int i = 0 ; i < numInts ; i++)
            response[i] = p.readInt();
        LastCallFailCause failCause = new LastCallFailCause();
        failCause.causeCode = response[0];
        if (p.dataAvail() > 0)
            failCause.vendorCause = p.readString();
        return failCause;
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
            int np = p.readInt();
            dc.numberPresentation = DriverCall.presentationFromCLIP(np);
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

    private void unsupportedRequest(String methodName, Message response) {
        riljLog("[" + getClass().getSimpleName() + "] Ignore call to: " + methodName);
        if (response != null) {
            AsyncResult.forMessage(response, null, new CommandException(
                    CommandException.Error.REQUEST_NOT_SUPPORTED));
            response.sendToTarget();
        }
    }

    private void invokeOemRilRequestSprd(byte key, byte value, Message response) {
        invokeOemRilRequestRaw(new byte[] { 'S', 'P', 'R', 'D', key, value }, response);
    }
}
