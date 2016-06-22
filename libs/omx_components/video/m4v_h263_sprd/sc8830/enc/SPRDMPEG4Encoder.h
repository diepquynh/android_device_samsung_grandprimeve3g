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

#ifndef SPRD_MPEG4_ENCODER_H_
#define SPRD_MPEG4_ENCODER_H_

#include "SprdSimpleOMXComponent.h"
#include "m4v_h263_enc_api.h"

#define MP4ENC_INTERNAL_BUFFER_SIZE  (0x200000)
#define ONEFRAME_BITSTREAM_BFR_SIZE	(1500*1024)  //for bitstream size of one encoded frame.

namespace android {

//#define SPRD_DUMP_YUV
//#define SPRD_DUMP_BS

struct SPRDMPEG4Encoder : public SprdSimpleOMXComponent {
    SPRDMPEG4Encoder(
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
    virtual ~SPRDMPEG4Encoder();

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
    int32_t mPbuf_yuv_p;
    int32_t mPbuf_yuv_size;

    bool mIOMMUEnabled;
    uint8_t *mPbuf_inter;

    sp<MemoryHeapIon> mPmem_stream;
    unsigned char* mPbuf_stream_v;
    int32_t mPbuf_stream_p;
    int32_t mPbuf_stream_size;

    sp<MemoryHeapIon> mPmem_extra;
    unsigned char* mPbuf_extra_v;
    int32_t mPbuf_extra_p;
    int32_t mPbuf_extra_size;

    int32_t mIsH263;
    MMEncVideoInfo mEncInfo;
    MMEncCapability mCapability;

    int32_t  mVideoWidth;
    int32_t  mVideoHeight;
    int32_t  mVideoFrameRate;
    int32_t  mVideoBitRate;
    int32_t  mVideoColorFormat;
    int32_t  mIDRFrameRefreshIntervalInSec;

    int64_t  mNumInputFrames;
    bool     mStarted;
    bool     mSawInputEOS;
    bool     mSignalledError;

    tagMP4Handle   *mHandle;
    MMEncConfig *mEncConfig;
    Vector<InputBufferInfo> mInputBufferInfoVec;

    int mSetFreqCount;

    void* mLibHandle;
    FT_MP4EncGetCodecCapability	mMP4EncGetCodecCapability;
    FT_MP4EncPreInit        mMP4EncPreInit;
    FT_MP4EncInit        mMP4EncInit;
    FT_MP4EncSetConf        mMP4EncSetConf;
    FT_MP4EncGetConf        mMP4EncGetConf;
    FT_MP4EncStrmEncode        mMP4EncStrmEncode;
    FT_MP4EncGenHeader        mMP4EncGenHeader;
    FT_MP4EncRelease        mMP4EncRelease;

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
    bool openEncoder(const char* libName);

    DISALLOW_EVIL_CONSTRUCTORS(SPRDMPEG4Encoder);
};

}  // namespace android

#endif  // SPRD_MPEG4_ENCODER_H_
