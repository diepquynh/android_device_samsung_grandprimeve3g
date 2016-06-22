/*
 * Copyright (C) 2011 The Android Open Source Project
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

#ifndef SPRD_MPEG4_DECODER_H_
#define SPRD_MPEG4_DECODER_H_

#include "SprdSimpleOMXComponent.h"
#include <binder/MemoryHeapIon.SPRD.h>
#include "m4v_h263_dec_api.h"

#define SPRD_ION_DEV "/dev/ion"

#define MP4DEC_INTERNAL_BUFFER_SIZE  (0x200000)
#define ONEFRAME_BITSTREAM_BFR_SIZE	(1500*1024)  //for bitstream size of one encoded frame.

struct tagMP4Handle;

namespace android {

struct SPRDMPEG4Decoder : public SprdSimpleOMXComponent {
    SPRDMPEG4Decoder(const char *name,
                     const OMX_CALLBACKTYPE *callbacks,
                     OMX_PTR appData,
                     OMX_COMPONENTTYPE **component);

protected:
    virtual ~SPRDMPEG4Decoder();

    virtual OMX_ERRORTYPE internalGetParameter(
        OMX_INDEXTYPE index, OMX_PTR params);

    virtual OMX_ERRORTYPE internalSetParameter(
        OMX_INDEXTYPE index, const OMX_PTR params);

    virtual OMX_ERRORTYPE internalUseBuffer(
            OMX_BUFFERHEADERTYPE **buffer,
            OMX_U32 portIndex,
            OMX_PTR appPrivate,
            OMX_U32 size,
            OMX_U8 *ptr,
            BufferPrivateStruct* bufferPrivate=NULL);

    virtual OMX_ERRORTYPE allocateBuffer(
        OMX_BUFFERHEADERTYPE **header,
        OMX_U32 portIndex,
        OMX_PTR appPrivate,
        OMX_U32 size);

    virtual OMX_ERRORTYPE freeBuffer(
        OMX_U32 portIndex,
        OMX_BUFFERHEADERTYPE *header);

    virtual OMX_ERRORTYPE getConfig(OMX_INDEXTYPE index, OMX_PTR params);

    virtual void onQueueFilled(OMX_U32 portIndex);
    virtual void onPortFlushCompleted(OMX_U32 portIndex);
    virtual void onPortEnableCompleted(OMX_U32 portIndex, bool enabled);
    virtual void onPortFlushPrepare(OMX_U32 portIndex);

    virtual OMX_ERRORTYPE getExtensionIndex(
        const char *name, OMX_INDEXTYPE *index);

private:
    enum {
        kNumInputBuffers  = 8,
        kNumOutputBuffers = 5,
    };

    enum {
        MODE_MPEG4,
        MODE_H263,

    } mMode;

    enum EOSStatus {
        INPUT_DATA_AVAILABLE,
        INPUT_EOS_SEEN,
        OUTPUT_FRAMES_FLUSHED,
    };

    tagMP4Handle *mHandle;

    size_t mInputBufferCount;

    int32_t mWidth, mHeight;
    int32_t mCropLeft, mCropTop, mCropRight, mCropBottom;

    int32 mMaxWidth, mMaxHeight;
    int mSetFreqCount;

    bool mSignalledError;
    bool mInitialized;
    bool mFramesConfigured;

    int32_t mNumSamplesOutput;

    bool mIOMMUEnabled;
    uint8_t *mCodecInterBuffer;
    uint8_t *mCodecExtraBuffer;

    sp<MemoryHeapIon> mPmem_stream;
    unsigned char* mPbuf_stream_v;
    int32 mPbuf_stream_p;
    int32 mPbuf_stream_size;

    sp<MemoryHeapIon> mPmem_extra;
    unsigned char*  mPbuf_extra_v;
    int32  mPbuf_extra_p;
    int32  mPbuf_extra_size;

    OMX_BOOL iUseAndroidNativeBuffer[2];

    void* mLibHandle;
    bool mDecoderSwFlag;
    bool mChangeToHwDec;
    EOSStatus mEOSStatus;
    bool mNeedIVOP;
    bool mHeadersDecoded;
    bool mAllocateBuffers;
    FT_MP4DecSetCurRecPic mMP4DecSetCurRecPic;
    FT_MP4DecInit mMP4DecInit;
    FT_MP4DecVolHeader mMP4DecVolHeader;
    FT_MP4DecMemInit mMP4DecMemInit;
    FT_MP4DecDecode mMP4DecDecode;
    FT_MP4DecRelease mMP4DecRelease;
    FT_Mp4GetVideoDimensions mMp4GetVideoDimensions;
    FT_Mp4GetBufferDimensions mMp4GetBufferDimensions;
    FT_MP4DecReleaseRefBuffers mMP4DecReleaseRefBuffers;
    FT_MP4DecSetReferenceYUV mMP4DecSetReferenceYUV;
    FT_MP4DecGetLastDspFrm mMP4DecGetLastDspFrm;
    FT_MP4GetCodecCapability mMP4GetCodecCapability;

    static int32_t extMemoryAllocWrapper(void *userData, unsigned int extra_mem_size);
    static int32_t BindFrameWrapper(void *aUserData, void *pHeader, int flag);
    static int32_t UnbindFrameWrapper(void *aUserData, void *pHeader, int flag);

    int extMemoryAlloc(unsigned int extra_mem_size) ;
    int VSP_bind_cb(void *pHeader,int flag);
    int VSP_unbind_cb(void *pHeader,int flag);

    enum {
        NONE,
        AWAITING_DISABLED,
        AWAITING_ENABLED
    } mOutputPortSettingsChange;

    void initPorts();
    status_t initDecoder();
    void releaseDecoder();
    bool drainAllOutputBuffers();
    bool portSettingsChanged();
    void updatePortDefinitions();
    bool openDecoder(const char* libName);
    void set_ddr_freq(const char* freq_in_khz);
    void change_ddr_freq();

    DISALLOW_EVIL_CONSTRUCTORS(SPRDMPEG4Decoder);
};

}  // namespace android

#endif  // SPRD_MPEG4_DECODER_H_


