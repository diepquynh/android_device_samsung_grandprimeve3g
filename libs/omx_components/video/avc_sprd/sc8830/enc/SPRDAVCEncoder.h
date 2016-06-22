/*
 * Copyright (C) 2012 The Android Open Source Project
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

#ifndef SPRD_AVC_ENCODER_H_
#define SPRD_AVC_ENCODER_H_

#include "SprdSimpleOMXComponent.h"

#include "avc_enc_api.h"

#define H264ENC_INTERNAL_BUFFER_SIZE  (0x200000)
#define ONEFRAME_BITSTREAM_BFR_SIZE	(1500*1024)  //for bitstream size of one encoded frame.

namespace android {

//#define SPRD_DUMP_YUV
//#define SPRD_DUMP_BS

struct SPRDAVCEncoder :  public SprdSimpleOMXComponent {
    SPRDAVCEncoder(
        const char *name,
        const OMX_CALLBACKTYPE *callbacks,
        OMX_PTR appData,
        OMX_COMPONENTTYPE **component);

    // Override SimpleSoftOMXComponent methods
    virtual OMX_ERRORTYPE internalGetParameter(
        OMX_INDEXTYPE index, OMX_PTR params);

    virtual OMX_ERRORTYPE internalSetParameter(
        OMX_INDEXTYPE index, const OMX_PTR params);

    virtual void onQueueFilled(OMX_U32 portIndex);

    virtual OMX_ERRORTYPE getExtensionIndex(
        const char *name, OMX_INDEXTYPE *index);

protected:
    virtual ~SPRDAVCEncoder();

private:
    enum {
        kNumBuffers = 2,
    };

    // OMX input buffer's timestamp and flags
    typedef struct {
        int64_t mTimeUs;
        int32_t mFlags;
    } InputBufferInfo;


    OMX_BOOL mStoreMetaData;
    sp<MemoryHeapIon> mYUVInPmemHeap;
    unsigned char* mPbuf_yuv_v;
    int32 mPbuf_yuv_p;
    int32 mPbuf_yuv_size;

    bool mIOMMUEnabled;
    uint8_t *mPbuf_inter;

    sp<MemoryHeapIon> mPmem_stream;
    unsigned char* mPbuf_stream_v;
    int32 mPbuf_stream_p;
    int32 mPbuf_stream_size;

    sp<MemoryHeapIon> mPmem_extra;
    unsigned char* mPbuf_extra_v;
    int32  mPbuf_extra_p;
    int32  mPbuf_extra_size;

    MMEncVideoInfo mEncInfo;
    MMEncCapability mCapability;

    int32_t  mVideoWidth;
    int32_t  mVideoHeight;
    int32_t  mVideoFrameRate;
    int32_t  mVideoBitRate;
    int32_t  mVideoColorFormat;
    int32_t  mIDRFrameRefreshIntervalInSec;
    AVCProfile mAVCEncProfile;
    AVCLevel   mAVCEncLevel;

    int64_t  mNumInputFrames;
    int64_t  mPrevTimestampUs;
    bool     mStarted;
    bool     mSpsPpsHeaderReceived;
    bool     mReadyForNextFrame;
    bool     mSawInputEOS;
    bool     mSignalledError;
    //bool     mIsIDRFrame;

    tagAVCHandle          *mHandle;
    tagAVCEncParam        *mEncParams;
    MMEncConfig *mEncConfig;
    uint32_t              *mSliceGroup;
//    Vector<MediaBuffer *> mOutputBuffers;
    Vector<InputBufferInfo> mInputBufferInfoVec;

    int mSetFreqCount;

    void* mLibHandle;
    FT_H264EncGetCodecCapability	mH264EncGetCodecCapability;
    FT_H264EncPreInit        mH264EncPreInit;
    FT_H264EncInit        mH264EncInit;
    FT_H264EncSetConf        mH264EncSetConf;
    FT_H264EncGetConf        mH264EncGetConf;
    FT_H264EncStrmEncode        mH264EncStrmEncode;
    FT_H264EncGenHeader        mH264EncGenHeader;
    FT_H264EncRelease        mH264EncRelease;

#ifdef SPRD_DUMP_YUV
    FILE* mFile_yuv;
#endif

#ifdef SPRD_DUMP_BS
    FILE* mFile_bs;
#endif

    void initPorts();
    OMX_ERRORTYPE initEncParams();
    OMX_ERRORTYPE initEncoder();
    OMX_ERRORTYPE releaseEncoder();
    OMX_ERRORTYPE releaseResource();
//    void releaseOutputBuffers();
    bool openEncoder(const char* libName);

    DISALLOW_EVIL_CONSTRUCTORS(SPRDAVCEncoder);
};

}  // namespace android

#endif  // SPRD_AVC_ENCODER_H_
