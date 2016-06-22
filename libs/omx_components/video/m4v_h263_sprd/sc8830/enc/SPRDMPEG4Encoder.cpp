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

#define LOG_NDEBUG 0
#define LOG_TAG "SPRDMPEG4Encoder"
#include <utils/Log.h>

#include "m4v_h263_enc_api.h"

#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/MediaErrors.h>
#include <media/stagefright/MetaData.h>
#include <media/stagefright/Utils.h>

#include <MetadataBufferType.h>
#include <HardwareAPI.h>

#include <ui/Rect.h>
#include <ui/GraphicBufferMapper.h>
//#include <gui/ISurfaceTexture.h>

#include <linux/ion.h>
#include <binder/MemoryHeapIon.SPRD.h>

#include <dlfcn.h>

#include "SPRDMPEG4Encoder.h"
#include "ion_sprd.h"


#define VIDEOENC_CURRENT_OPT

namespace android {

template<class T>
static void InitOMXParams(T *params) {
    params->nSize = sizeof(T);
    params->nVersion.s.nVersionMajor = 1;
    params->nVersion.s.nVersionMinor = 0;
    params->nVersion.s.nRevision = 0;
    params->nVersion.s.nStep = 0;
}
void dump_yuv( uint8 * pBuffer,uint32 aInBufSize)
{
    FILE *fp = fopen("/data/encoder_in.yuv","ab");
    fwrite(pBuffer,1,aInBufSize,fp);
    fclose(fp);
}

/*
 * In case of orginal input height_org is not 16 aligned, we shoud copy original data to a larger space
 * Example: width_org = 640, height_org = 426,
 * We have to copy this data to a width_dst = 640 height_dst = 432 buffer which is 16 aligned.
 * Be careful, when doing this convert we MUST keep UV in their right position.
 *
 * FIXME: If width_org is not 16 aligned also, this would be much complicate
 *
 */
inline static void ConvertYUV420PlanarToYUV420SemiPlanar(
    uint8_t *inyuv, uint8_t* outyuv,
    int32_t width_org, int32_t height_org,
    int32_t width_dst, int32_t height_dst) {

    int32_t inYsize = width_org * height_org;
    uint32_t *outy = (uint32_t *) outyuv;
    uint16_t *incb = (uint16_t *) (inyuv + inYsize);
    uint16_t *incr = (uint16_t *) (inyuv + inYsize + (inYsize >> 2));

    /* Y copying */
    memcpy(outy, inyuv, inYsize);

    /* U & V copying */
    uint32_t *outUV = (uint32_t *) (outyuv + width_dst * height_dst);
    for (int32_t i = height_org >> 1; i > 0; --i) {
        for (int32_t j = width_org >> 2; j > 0; --j) {
            uint32_t tempU = *incb++;
            uint32_t tempV = *incr++;

            tempU = (tempU & 0xFF) | ((tempU & 0xFF00) << 8);
            tempV = (tempV & 0xFF) | ((tempV & 0xFF00) << 8);
            uint32_t temp = tempV | (tempU << 8);

            // Flip U and V
            *outUV++ = temp;
        }
    }
}

#ifdef VIDEOENC_CURRENT_OPT
inline static void set_ddr_freq(const char* freq_in_khz)
{
    const char* const set_freq = "/sys/devices/platform/scxx30-dmcfreq.0/devfreq/scxx30-dmcfreq.0/ondemand/set_freq";

    FILE* fp = fopen(set_freq, "w");
    if (fp != NULL) {
        fprintf(fp, "%s", freq_in_khz);
        ALOGE("set ddr freq to %skhz", freq_in_khz);
        fclose(fp);
    } else {
        ALOGE("Failed to open %s", set_freq);
    }
}
#endif

SPRDMPEG4Encoder::SPRDMPEG4Encoder(
    const char *name,
    const OMX_CALLBACKTYPE *callbacks,
    OMX_PTR appData,
    OMX_COMPONENTTYPE **component)
    : SprdSimpleOMXComponent(name, callbacks, appData, component),
      mVideoWidth(176),
      mVideoHeight(144),
      mVideoFrameRate(30),
      mVideoBitRate(192000),
      mVideoColorFormat(OMX_COLOR_FormatYUV420SemiPlanar),
      mIDRFrameRefreshIntervalInSec(1),
      mNumInputFrames(-1),
      mStarted(false),
      mSawInputEOS(false),
      mSignalledError(false),
      mStoreMetaData(OMX_FALSE),
      mPbuf_yuv_v(NULL),
      mPbuf_yuv_p(0),
      mPbuf_yuv_size(0),
      mPbuf_inter(NULL),
      mPbuf_extra_v(NULL),
      mPbuf_extra_p(0),
      mPbuf_extra_size(0),
      mPbuf_stream_v(NULL),
      mPbuf_stream_p(0),
      mPbuf_stream_size(0),
      mHandle(new tagMP4Handle),
      mEncConfig(new MMEncConfig),
      mSetFreqCount(0),
      mLibHandle(NULL),
      mMP4EncGetCodecCapability(NULL),
      mMP4EncPreInit(NULL),
      mMP4EncInit(NULL),
      mMP4EncSetConf(NULL),
      mMP4EncGetConf(NULL),
      mMP4EncStrmEncode(NULL),
      mMP4EncGenHeader(NULL),
      mMP4EncRelease(NULL) {

    ALOGI("Construct SPRDMPEG4Encoder, this: %0x", (void *)this);

    CHECK(mHandle != NULL);
    memset(mHandle, 0, sizeof(tagMP4Handle));

    mHandle->videoEncoderData = NULL;
    mHandle->userData = this;

    memset(&mEncInfo, 0, sizeof(mEncInfo));

    CHECK_EQ(openEncoder("libomx_m4vh263enc_hw_sprd.so"), true);

    if (!strcmp(name, "OMX.sprd.h263.encoder")) {
        mIsH263 = 1;
    } else {
        mIsH263 = 0;
        CHECK(!strcmp(name, "OMX.sprd.mpeg4.encoder"));
    }

    initPorts();
    ALOGI("Construct SPRDMPEG4Encoder");

    mIOMMUEnabled = MemoryHeapIon::Mm_iommu_is_enabled();
    ALOGI("%s, is IOMMU enabled: %d", __FUNCTION__, mIOMMUEnabled);

    MMCodecBuffer InterMemBfr;
    int32 size_inter = MP4ENC_INTERNAL_BUFFER_SIZE;

    mPbuf_inter = (uint8 *)malloc(size_inter);
    InterMemBfr.common_buffer_ptr = (uint8 *)mPbuf_inter;
    InterMemBfr.common_buffer_ptr_phy = 0;
    InterMemBfr.size = size_inter;

    CHECK_EQ((*mMP4EncPreInit)(mHandle, &InterMemBfr), MMENC_OK);

    CHECK_EQ ((*mMP4EncGetCodecCapability)(mHandle, &mCapability), MMENC_OK);

#ifdef SPRD_DUMP_YUV
    mFile_yuv = fopen("/data/video.yuv", "wb");
#endif

#ifdef SPRD_DUMP_BS
    mFile_bs = fopen("/data/video.m4v", "wb");
#endif
}

SPRDMPEG4Encoder::~SPRDMPEG4Encoder() {
    ALOGI("Destruct SPRDMPEG4Encoder, this: %0x", (void *)this);

    releaseEncoder();

    List<BufferInfo *> &outQueue = getPortQueue(1);
    List<BufferInfo *> &inQueue = getPortQueue(0);
    CHECK(outQueue.empty());
    CHECK(inQueue.empty());

    if(mLibHandle)
    {
        dlclose(mLibHandle);
        mLibHandle = NULL;
    }

#ifdef SPRD_DUMP_YUV
    if (mFile_yuv) {
        fclose(mFile_yuv);
        mFile_yuv = NULL;
    }
#endif

#ifdef SPRD_DUMP_BS
    if (mFile_bs) {
        fclose(mFile_bs);
        mFile_bs = NULL;
    }
#endif
}

OMX_ERRORTYPE SPRDMPEG4Encoder::initEncParams() {

    CHECK(mEncConfig != NULL);
    memset(mEncConfig, 0, sizeof(MMEncConfig));

#ifdef VIDEOENC_CURRENT_OPT
    if (((mVideoWidth <= 720) && (mVideoHeight <= 480)) || ((mVideoWidth <= 480) && (mVideoHeight <= 720))) {
        set_ddr_freq("200000");
        mSetFreqCount ++;
    }
#endif

    MMCodecBuffer ExtraMemBfr;
    MMCodecBuffer StreamMemBfr;
    int32 phy_addr = 0;
    int32 size = 0;

    unsigned int size_extra = ((mVideoWidth+15)&(~15)) * ((mVideoHeight+15)&(~15)) * 3/2 * 2;
    size_extra += 320*2*sizeof(uint32);
    size_extra += 10*1024;
    if (mIOMMUEnabled) {
        mPmem_extra = new MemoryHeapIon("/dev/ion", size_extra, MemoryHeapBase::NO_CACHING, ION_HEAP_ID_MASK_SYSTEM);
    } else {
        mPmem_extra = new MemoryHeapIon("/dev/ion", size_extra, MemoryHeapBase::NO_CACHING, ION_HEAP_ID_MASK_MM);
    }
    if (mPmem_extra->getHeapID() < 0) {
        ALOGE("Failed to alloc extra buffer (%d), getHeapID failed", size_extra);
        return OMX_ErrorInsufficientResources;
    } else
    {
        int32 ret;
        if(mIOMMUEnabled) {
            ret = mPmem_extra->get_mm_iova(&phy_addr, &size);
        } else {
            ret = mPmem_extra->get_phy_addr_from_ion(&phy_addr, &size);
        }
        if (ret < 0)
        {
            ALOGE("Failed to alloc extra buffer, get phy addr failed");
            return OMX_ErrorInsufficientResources;
        } else
        {
            mPbuf_extra_v = (unsigned char*)mPmem_extra->base();
            mPbuf_extra_p = (uint32)phy_addr;
            mPbuf_extra_size = (int32)size;
        }
    }

    unsigned int size_stream = ONEFRAME_BITSTREAM_BFR_SIZE;
    if(mIOMMUEnabled) {
        mPmem_stream = new MemoryHeapIon("/dev/ion", size_stream, MemoryHeapBase::NO_CACHING, ION_HEAP_ID_MASK_SYSTEM);
    } else {
        mPmem_stream = new MemoryHeapIon("/dev/ion", size_stream, MemoryHeapBase::NO_CACHING, ION_HEAP_ID_MASK_MM);
    }
    if (mPmem_stream->getHeapID() < 0) {
        ALOGE("Failed to alloc stream buffer (%d), getHeapID failed", size_stream);
        return OMX_ErrorInsufficientResources;
    } else
    {
        int32 ret;
        if(mIOMMUEnabled) {
            ret = mPmem_stream->get_mm_iova(&phy_addr, &size);
        } else {
            ret = mPmem_stream->get_phy_addr_from_ion(&phy_addr, &size);
        }
        if (ret < 0)
        {
            ALOGE("Failed to alloc stream buffer, get phy addr failed");
            return OMX_ErrorInsufficientResources;
        } else
        {
            mPbuf_stream_v = (unsigned char*)mPmem_stream->base();
            mPbuf_stream_p = (uint32)phy_addr;
            mPbuf_stream_size = (int32)size;
        }
    }

    ExtraMemBfr.common_buffer_ptr = mPbuf_extra_v;
    ExtraMemBfr.common_buffer_ptr_phy = mPbuf_extra_p;
    ExtraMemBfr.size = size_extra;

    StreamMemBfr.common_buffer_ptr = mPbuf_stream_v;
    StreamMemBfr.common_buffer_ptr_phy = mPbuf_stream_p;
    StreamMemBfr.size	= size_stream;

    mEncInfo.is_h263 = mIsH263;
    mEncInfo.frame_width = mVideoWidth;
    mEncInfo.frame_height = mVideoHeight;
    mEncInfo.uv_interleaved = 1;
    mEncInfo.time_scale = 1000;
#ifdef ANTI_SHAKE
    mEncInfo.b_anti_shake = 1;
#else
    mEncInfo.b_anti_shake = 0;
#endif

    if ((*mMP4EncInit)(mHandle, &ExtraMemBfr,&StreamMemBfr, &mEncInfo) != MMENC_OK) {
        ALOGE("Failed to init mp4enc");
        return OMX_ErrorUndefined;
    }

    if ((*mMP4EncGetConf)(mHandle, mEncConfig)) {
        ALOGE("Failed to get default encoding parameters");
        return OMX_ErrorUndefined;
    }

    mEncConfig->h263En = mIsH263;
    mEncConfig->RateCtrlEnable = 1;
    mEncConfig->targetBitRate = mVideoBitRate;
    mEncConfig->FrameRate = mVideoFrameRate;
    mEncConfig->QP_IVOP = 4;
    mEncConfig->QP_PVOP = 4;
    mEncConfig->vbv_buf_size = mVideoBitRate/2;
    mEncConfig->profileAndLevel = 1;

    if ((*mMP4EncSetConf)(mHandle, mEncConfig)) {
        ALOGE("Failed to set default encoding parameters");
        return OMX_ErrorUndefined;
    }

    return OMX_ErrorNone;
}

OMX_ERRORTYPE SPRDMPEG4Encoder::initEncoder() {
    CHECK(!mStarted);

    OMX_ERRORTYPE errType = OMX_ErrorNone;
    if (OMX_ErrorNone != (errType = initEncParams())) {
        ALOGE("Failed to initialized encoder params");
        mSignalledError = true;
        notify(OMX_EventError, OMX_ErrorUndefined, 0, 0);
        return errType;
    }

    mNumInputFrames = -1;  // 1st buffer for codec specific data
    mStarted = true;

    return OMX_ErrorNone;
}

OMX_ERRORTYPE SPRDMPEG4Encoder::releaseEncoder() {

    (*mMP4EncRelease)(mHandle);

    if (mPbuf_inter != NULL)
    {
        free(mPbuf_inter);
        mPbuf_inter = NULL;
    }

    if (mPbuf_extra_v != NULL)
    {
        if(mIOMMUEnabled) {
            mPmem_extra->free_mm_iova(mPbuf_extra_p, mPbuf_extra_size);
        }
        mPmem_extra.clear();
        mPbuf_extra_v = NULL;
        mPbuf_extra_p = 0;
        mPbuf_extra_size = 0;
    }

    if (mPbuf_stream_v != NULL)
    {
        if(mIOMMUEnabled) {
            mPmem_stream->free_mm_iova(mPbuf_stream_p, mPbuf_stream_size);
        }
        mPmem_stream.clear();
        mPbuf_stream_v = NULL;
        mPbuf_stream_p = 0;
        mPbuf_stream_size = 0;
    }

    if (mPbuf_yuv_v != NULL)
    {
        if(mIOMMUEnabled) {
            mYUVInPmemHeap->free_mm_iova(mPbuf_yuv_p, mPbuf_yuv_size);
        }
        mYUVInPmemHeap.clear();
        mPbuf_yuv_v = NULL;
        mPbuf_yuv_p = 0;
        mPbuf_yuv_size = 0;
    }

#ifdef VIDEOENC_CURRENT_OPT
    while (mSetFreqCount > 0) {
        set_ddr_freq("0");
        mSetFreqCount --;
    }
#endif

    delete mEncConfig;
    mEncConfig = NULL;

    delete mHandle;
    mHandle = NULL;

    mStarted = false;

    return OMX_ErrorNone;
}

void SPRDMPEG4Encoder::initPorts() {
    OMX_PARAM_PORTDEFINITIONTYPE def;
    InitOMXParams(&def);

    const size_t kInputBufferSize = (((mVideoWidth+15)&(~15))  * ((mVideoHeight+15)&(~15))  * 3) >> 1;

    // 256 * 1024 is a magic number for PV's encoder, not sure why
    const size_t kOutputBufferSize =
        (kInputBufferSize > 256 * 1024)
        ? kInputBufferSize: 256 * 1024;

    def.nPortIndex = 0;
    def.eDir = OMX_DirInput;
    def.nBufferCountMin = kNumBuffers;
    def.nBufferCountActual = def.nBufferCountMin;
    def.nBufferSize = kInputBufferSize;
    def.bEnabled = OMX_TRUE;
    def.bPopulated = OMX_FALSE;
    def.eDomain = OMX_PortDomainVideo;
    def.bBuffersContiguous = OMX_FALSE;
    def.nBufferAlignment = 1;

    def.format.video.cMIMEType = const_cast<char *>("video/raw");
    def.format.video.eCompressionFormat = OMX_VIDEO_CodingUnused;
    def.format.video.eColorFormat = OMX_COLOR_FormatYUV420SemiPlanar;
    def.format.video.xFramerate = (mVideoFrameRate << 16);  // Q16 format
    def.format.video.nBitrate = mVideoBitRate;
    def.format.video.nFrameWidth = mVideoWidth;
    def.format.video.nFrameHeight = mVideoHeight;
    def.format.video.nStride = mVideoWidth;
    def.format.video.nSliceHeight = mVideoHeight;

    addPort(def);

    def.nPortIndex = 1;
    def.eDir = OMX_DirOutput;
    def.nBufferCountMin = kNumBuffers;
    def.nBufferCountActual = def.nBufferCountMin;
    def.nBufferSize = kOutputBufferSize;
    def.bEnabled = OMX_TRUE;
    def.bPopulated = OMX_FALSE;
    def.eDomain = OMX_PortDomainVideo;
    def.bBuffersContiguous = OMX_FALSE;
    def.nBufferAlignment = 2;

    def.format.video.cMIMEType =
        (mIsH263 == 0)
        ? const_cast<char *>(MEDIA_MIMETYPE_VIDEO_MPEG4)
        : const_cast<char *>(MEDIA_MIMETYPE_VIDEO_H263);

    def.format.video.eCompressionFormat =
        (mIsH263 == 0)
        ? OMX_VIDEO_CodingMPEG4
        : OMX_VIDEO_CodingH263;

    def.format.video.eColorFormat = OMX_COLOR_FormatUnused;
    def.format.video.xFramerate = (0 << 16);  // Q16 format
    def.format.video.nBitrate = mVideoBitRate;
    def.format.video.nFrameWidth = mVideoWidth;
    def.format.video.nFrameHeight = mVideoHeight;
    def.format.video.nStride = mVideoWidth;
    def.format.video.nSliceHeight = mVideoHeight;

    addPort(def);
}

OMX_ERRORTYPE SPRDMPEG4Encoder::internalGetParameter(
    OMX_INDEXTYPE index, OMX_PTR params) {
    switch (index) {
    case OMX_IndexParamVideoErrorCorrection:
    {
        return OMX_ErrorNotImplemented;
    }

    case OMX_IndexParamVideoBitrate:
    {
        OMX_VIDEO_PARAM_BITRATETYPE *bitRate =
            (OMX_VIDEO_PARAM_BITRATETYPE *) params;

        if (bitRate->nPortIndex != 1) {
            return OMX_ErrorUndefined;
        }

        bitRate->eControlRate = OMX_Video_ControlRateVariable;
        bitRate->nTargetBitrate = mVideoBitRate;
        return OMX_ErrorNone;
    }

    case OMX_IndexParamVideoPortFormat:
    {
        OMX_VIDEO_PARAM_PORTFORMATTYPE *formatParams =
            (OMX_VIDEO_PARAM_PORTFORMATTYPE *)params;

        if (formatParams->nPortIndex > 1) {
            return OMX_ErrorUndefined;
        }

        if (formatParams->nIndex > 1) {
            return OMX_ErrorNoMore;
        }

        if (formatParams->nPortIndex == 0) {
            formatParams->eCompressionFormat = OMX_VIDEO_CodingUnused;
            if (formatParams->nIndex == 0) {
                formatParams->eColorFormat = OMX_COLOR_FormatYUV420Planar;
            } else {
                formatParams->eColorFormat = OMX_COLOR_FormatYUV420SemiPlanar;
            }
        } else {
            formatParams->eCompressionFormat =
                (mIsH263 == 0)
                ? OMX_VIDEO_CodingMPEG4
                : OMX_VIDEO_CodingH263;

            formatParams->eColorFormat = OMX_COLOR_FormatUnused;
        }

        return OMX_ErrorNone;
    }

    case OMX_IndexParamVideoH263:
    {
        OMX_VIDEO_PARAM_H263TYPE *h263type =
            (OMX_VIDEO_PARAM_H263TYPE *)params;

        if (h263type->nPortIndex != 1) {
            return OMX_ErrorUndefined;
        }

        h263type->nAllowedPictureTypes =
            (OMX_VIDEO_PictureTypeI | OMX_VIDEO_PictureTypeP);
        h263type->eProfile = OMX_VIDEO_H263ProfileBaseline;
        h263type->eLevel = OMX_VIDEO_H263Level45;
        h263type->bPLUSPTYPEAllowed = OMX_FALSE;
        h263type->bForceRoundingTypeToZero = OMX_FALSE;
        h263type->nPictureHeaderRepetition = 0;
        h263type->nGOBHeaderInterval = 0;

        return OMX_ErrorNone;
    }

    case OMX_IndexParamVideoMpeg4:
    {
        OMX_VIDEO_PARAM_MPEG4TYPE *mpeg4type =
            (OMX_VIDEO_PARAM_MPEG4TYPE *)params;

        if (mpeg4type->nPortIndex != 1) {
            return OMX_ErrorUndefined;
        }

        mpeg4type->eProfile = OMX_VIDEO_MPEG4ProfileCore;
        mpeg4type->eLevel = OMX_VIDEO_MPEG4Level2;
        mpeg4type->nAllowedPictureTypes =
            (OMX_VIDEO_PictureTypeI | OMX_VIDEO_PictureTypeP);
        mpeg4type->nBFrames = 0;
        mpeg4type->nIDCVLCThreshold = 0;
        mpeg4type->bACPred = OMX_TRUE;
        mpeg4type->nMaxPacketSize = 256;
        mpeg4type->nTimeIncRes = 1000;
        mpeg4type->nHeaderExtension = 0;
        mpeg4type->bReversibleVLC = OMX_FALSE;

        return OMX_ErrorNone;
    }

    case OMX_IndexParamVideoProfileLevelQuerySupported:
    {
        OMX_VIDEO_PARAM_PROFILELEVELTYPE *profileLevel =
            (OMX_VIDEO_PARAM_PROFILELEVELTYPE *)params;

        if (profileLevel->nPortIndex != 1) {
            return OMX_ErrorUndefined;
        }

        if (profileLevel->nProfileIndex > 0) {
            return OMX_ErrorNoMore;
        }

        if (mIsH263 == 1)  {
            profileLevel->eProfile = OMX_VIDEO_H263ProfileBaseline;
            profileLevel->eLevel = OMX_VIDEO_H263Level45;
        } else {
            profileLevel->eProfile = OMX_VIDEO_MPEG4ProfileCore;
            profileLevel->eLevel = OMX_VIDEO_MPEG4Level2;
        }

        return OMX_ErrorNone;
    }

    case OMX_IndexParamStoreMetaDataBuffer:
    {
        StoreMetaDataInBuffersParams *pStoreMetaData = (StoreMetaDataInBuffersParams *)params;
        pStoreMetaData->bStoreMetaData = mStoreMetaData;
        return OMX_ErrorNone;
    }

    default:
        return SprdSimpleOMXComponent::internalGetParameter(index, params);
    }
}

OMX_ERRORTYPE SPRDMPEG4Encoder::internalSetParameter(
    OMX_INDEXTYPE index, const OMX_PTR params) {
    switch (index) {
    case OMX_IndexParamVideoErrorCorrection:
    {
        return OMX_ErrorNotImplemented;
    }

    case OMX_IndexParamVideoBitrate:
    {
        OMX_VIDEO_PARAM_BITRATETYPE *bitRate =
            (OMX_VIDEO_PARAM_BITRATETYPE *) params;

        if (bitRate->nPortIndex != 1 ||
                bitRate->eControlRate != OMX_Video_ControlRateVariable) {
            return OMX_ErrorUndefined;
        }

        mVideoBitRate = bitRate->nTargetBitrate;
        return OMX_ErrorNone;
    }

    case OMX_IndexParamPortDefinition:
    {
        OMX_PARAM_PORTDEFINITIONTYPE *def =
            (OMX_PARAM_PORTDEFINITIONTYPE *)params;
        if (def->nPortIndex > 1) {
            return OMX_ErrorUndefined;
        }

        if (def->nPortIndex == 0) {
            if (def->format.video.eCompressionFormat != OMX_VIDEO_CodingUnused ||
                    (def->format.video.eColorFormat != OMX_COLOR_FormatYUV420Planar &&
                     def->format.video.eColorFormat != OMX_COLOR_FormatYUV420SemiPlanar)) {
                return OMX_ErrorUndefined;
            }
        } else {
            if (((mIsH263 == 0)  &&
                    def->format.video.eCompressionFormat != OMX_VIDEO_CodingMPEG4) ||
                    ((mIsH263 == 1)  &&
                     def->format.video.eCompressionFormat != OMX_VIDEO_CodingH263) ||
                    (def->format.video.eColorFormat != OMX_COLOR_FormatUnused)) {
                return OMX_ErrorUndefined;
            }
        }

        // Enlarge the buffer size for both input and output port
        if(def->nPortIndex <= 1) {
            uint32_t bufferSize = ((def->format.video.nFrameWidth+15)&(~15))*((def->format.video.nFrameHeight+15)&(~15))*3/2;
            if(bufferSize > def->nBufferSize) {
                def->nBufferSize = bufferSize;
            }
        }

        OMX_ERRORTYPE err = SprdSimpleOMXComponent::internalSetParameter(index, params);
        if (OMX_ErrorNone != err) {
            return err;
        }

        if (def->nPortIndex == 0) {
            mVideoWidth = def->format.video.nFrameWidth;
            mVideoHeight = def->format.video.nFrameHeight;
            mVideoFrameRate = def->format.video.xFramerate >> 16;
            mVideoColorFormat = def->format.video.eColorFormat;
        } else {
            mVideoBitRate = def->format.video.nBitrate;
        }

        return OMX_ErrorNone;
    }

    case OMX_IndexParamStandardComponentRole:
    {
        const OMX_PARAM_COMPONENTROLETYPE *roleParams =
            (const OMX_PARAM_COMPONENTROLETYPE *)params;

        if (strncmp((const char *)roleParams->cRole,
                    (mIsH263 == 1)
                    ? "video_encoder.h263": "video_encoder.mpeg4",
                    OMX_MAX_STRINGNAME_SIZE - 1)) {
            return OMX_ErrorUndefined;
        }

        return OMX_ErrorNone;
    }

    case OMX_IndexParamVideoPortFormat:
    {
        const OMX_VIDEO_PARAM_PORTFORMATTYPE *formatParams =
            (const OMX_VIDEO_PARAM_PORTFORMATTYPE *)params;

        if (formatParams->nPortIndex > 1) {
            return OMX_ErrorUndefined;
        }

        if (formatParams->nIndex > 1) {
            return OMX_ErrorNoMore;
        }

        if (formatParams->nPortIndex == 0) {
            if (formatParams->eCompressionFormat != OMX_VIDEO_CodingUnused ||
                    ((formatParams->nIndex == 0 &&
                      formatParams->eColorFormat != OMX_COLOR_FormatYUV420Planar) ||
                     (formatParams->nIndex == 1 &&
                      formatParams->eColorFormat != OMX_COLOR_FormatYUV420SemiPlanar))) {
                return OMX_ErrorUndefined;
            }
            mVideoColorFormat = formatParams->eColorFormat;
        } else {
            if (((mIsH263 == 1)  &&
                    formatParams->eCompressionFormat != OMX_VIDEO_CodingH263) ||
                    ((mIsH263 == 0)  &&
                     formatParams->eCompressionFormat != OMX_VIDEO_CodingMPEG4) ||
                    formatParams->eColorFormat != OMX_COLOR_FormatUnused) {
                return OMX_ErrorUndefined;
            }
        }

        return OMX_ErrorNone;
    }

    case OMX_IndexParamVideoH263:
    {
        OMX_VIDEO_PARAM_H263TYPE *h263type =
            (OMX_VIDEO_PARAM_H263TYPE *)params;

        if (h263type->nPortIndex != 1) {
            return OMX_ErrorUndefined;
        }

        if (h263type->eProfile != OMX_VIDEO_H263ProfileBaseline ||
                h263type->eLevel != OMX_VIDEO_H263Level45 ||
                (h263type->nAllowedPictureTypes & OMX_VIDEO_PictureTypeB) ||
                h263type->bPLUSPTYPEAllowed != OMX_FALSE ||
                h263type->bForceRoundingTypeToZero != OMX_FALSE ||
                h263type->nPictureHeaderRepetition != 0 ||
                h263type->nGOBHeaderInterval != 0) {
            return OMX_ErrorUndefined;
        }

        return OMX_ErrorNone;
    }

    case OMX_IndexParamVideoMpeg4:
    {
        OMX_VIDEO_PARAM_MPEG4TYPE *mpeg4type =
            (OMX_VIDEO_PARAM_MPEG4TYPE *)params;

        if (mpeg4type->nPortIndex != 1) {
            return OMX_ErrorUndefined;
        }

        if (mpeg4type->eProfile != OMX_VIDEO_MPEG4ProfileCore ||
                mpeg4type->eLevel != OMX_VIDEO_MPEG4Level2 ||
                (mpeg4type->nAllowedPictureTypes & OMX_VIDEO_PictureTypeB) ||
                mpeg4type->nBFrames != 0 ||
                mpeg4type->nIDCVLCThreshold != 0 ||
                mpeg4type->bACPred != OMX_TRUE ||
                mpeg4type->nMaxPacketSize != 256 ||
                mpeg4type->nTimeIncRes != 1000 ||
                mpeg4type->nHeaderExtension != 0 ||
                mpeg4type->bReversibleVLC != OMX_FALSE) {
            return OMX_ErrorUndefined;
        }

        return OMX_ErrorNone;
    }

    case OMX_IndexParamStoreMetaDataBuffer:
    {
        StoreMetaDataInBuffersParams *pStoreMetaData = (StoreMetaDataInBuffersParams *)params;

        // Ignore this setting on output port
        if (pStoreMetaData->nPortIndex == 1 /* kOutputPortIndex */)
            return OMX_ErrorNone;

        mStoreMetaData = pStoreMetaData->bStoreMetaData;
        return OMX_ErrorNone;
    }

    default:
        return SprdSimpleOMXComponent::internalSetParameter(index, params);
    }
}


OMX_ERRORTYPE SPRDMPEG4Encoder::getExtensionIndex(
    const char *name, OMX_INDEXTYPE *index)
{
    if(strcmp(name, "OMX.google.android.index.storeMetaDataInBuffers") == 0) {
        *index = (OMX_INDEXTYPE) OMX_IndexParamStoreMetaDataBuffer;
        return OMX_ErrorNone;
    }

    return SprdSimpleOMXComponent::getExtensionIndex(name, index);
}

void SPRDMPEG4Encoder::onQueueFilled(OMX_U32 portIndex) {
    if (mSignalledError || mSawInputEOS) {
        return;
    }
    if (!mStarted) {
        if (OMX_ErrorNone != initEncoder()) {
            return;
        }
    }
    List<BufferInfo *> &inQueue = getPortQueue(0);
    List<BufferInfo *> &outQueue = getPortQueue(1);

    while (!mSawInputEOS && !inQueue.empty() && !outQueue.empty()) {
        BufferInfo *inInfo = *inQueue.begin();
        OMX_BUFFERHEADERTYPE *inHeader = inInfo->mHeader;
        BufferInfo *outInfo = *outQueue.begin();
        OMX_BUFFERHEADERTYPE *outHeader = outInfo->mHeader;

        outHeader->nTimeStamp = 0;
        outHeader->nFlags = 0;
        outHeader->nOffset = 0;
        outHeader->nFilledLen = 0;
        outHeader->nOffset = 0;

        uint8_t *outPtr = (uint8_t *) outHeader->pBuffer;
        uint32_t dataLength = outHeader->nAllocLen;

        if (mNumInputFrames < 0) {
            MMEncOut encOut;
            if ((*mMP4EncGenHeader)(mHandle, &encOut) != MMENC_OK) {
                ALOGE("Failed to generate VOL header");
                mSignalledError = true;
                notify(OMX_EventError, OMX_ErrorUndefined, 0, 0);
                return;
            }

#ifdef SPRD_DUMP_BS
            if (mFile_bs != NULL) {
                fwrite(encOut.pOutBuf, 1, encOut.strmSize, mFile_bs);
            }
#endif

            dataLength = encOut.strmSize;
            memcpy(outPtr, encOut.pOutBuf, dataLength);

            ALOGV("Output VOL header: %d bytes", dataLength);
            ++mNumInputFrames;
            outHeader->nFlags |= OMX_BUFFERFLAG_CODECCONFIG;
            outHeader->nFilledLen = dataLength;
            outQueue.erase(outQueue.begin());
            outInfo->mOwnedByUs = false;
            notifyFillBufferDone(outHeader);
            return;
        }

        // Save the input buffer info so that it can be
        // passed to an output buffer
        InputBufferInfo info;
        info.mTimeUs = inHeader->nTimeStamp;
        info.mFlags = inHeader->nFlags;
        mInputBufferInfoVec.push(info);

        if (inHeader->nFlags & OMX_BUFFERFLAG_EOS) {
            mSawInputEOS = true;
        }

        if (inHeader->nFilledLen > 0) {
            const void *inData = inHeader->pBuffer + inHeader->nOffset;
            uint8_t *inputData = (uint8_t *) inData;
            CHECK(inputData != NULL);


            MMEncIn vid_in;
            MMEncOut vid_out;
            memset(&vid_in, 0, sizeof(vid_in));
            memset(&vid_out, 0, sizeof(vid_out));
            uint8_t* py = NULL;
            uint8_t* py_phy = NULL;
            uint32_t width = 0;
            uint32_t height = 0;
            uint32_t x = 0;
            uint32_t y = 0;

            if (mStoreMetaData) {
                unsigned int type = *(unsigned int *) inputData;
                if (type == kMetadataBufferTypeCameraSource) {
                    py = (uint8_t*)(*((int *) inputData + 2));
                    py_phy = (uint8_t*)(*((int *) inputData + 1));
                    width = (uint32_t)(*((int *) inputData + 3));
                    height = (uint32_t)(*((int *) inputData + 4));
                    x = (uint32_t)(*((int *) inputData + 5));
                    y = (uint32_t)(*((int *) inputData + 6));
                } else if (type == kMetadataBufferTypeGrallocSource) {
                    if (mPbuf_yuv_v == NULL) {
                        int32 yuv_size = ((mVideoWidth+15)&(~15)) * ((mVideoHeight+15)&(~15)) *3/2;
                        if(mIOMMUEnabled) {
                            mYUVInPmemHeap = new MemoryHeapIon("/dev/ion", yuv_size, MemoryHeapBase::NO_CACHING, ION_HEAP_ID_MASK_SYSTEM);
                        } else {
                            mYUVInPmemHeap = new MemoryHeapIon("/dev/ion", yuv_size, MemoryHeapBase::NO_CACHING, ION_HEAP_ID_MASK_MM);
                        }
                        if (mYUVInPmemHeap->getHeapID() < 0) {
                            ALOGE("Failed to alloc yuv buffer");
                            return;
                        }
                        int ret,phy_addr, buffer_size;
                        if(mIOMMUEnabled) {
                            ret = mYUVInPmemHeap->get_mm_iova(&phy_addr, &buffer_size);
                        } else {
                            ret = mYUVInPmemHeap->get_phy_addr_from_ion(&phy_addr, &buffer_size);
                        }
                        if(ret) {
                            ALOGE("Failed to get_phy_addr_from_ion %d", ret);
                            return;
                        }
                        mPbuf_yuv_v =(uint8_t *) mYUVInPmemHeap->base();
                        mPbuf_yuv_p = phy_addr;
                        mPbuf_yuv_size = buffer_size;
                    }


                    py = mPbuf_yuv_v;
                    py_phy = (uint8_t *)mPbuf_yuv_p;

                    GraphicBufferMapper &mapper = GraphicBufferMapper::get();
                    buffer_handle_t buf = *((buffer_handle_t *)(inputData + 4));
                    Rect bounds((mVideoWidth+15)&(~15), (mVideoHeight+15)&(~15));

                    void* vaddr;
                    if (mapper.lock(buf, GRALLOC_USAGE_SW_READ_OFTEN|GRALLOC_USAGE_SW_WRITE_NEVER, bounds, &vaddr)) {
                        return;
                    }

                    if (mVideoColorFormat != OMX_COLOR_FormatYUV420SemiPlanar) {
                        ConvertYUV420PlanarToYUV420SemiPlanar((uint8_t*)vaddr, py, mVideoWidth, mVideoHeight,
                                                             (mVideoWidth + 15) & (~15), (mVideoHeight + 15) & (~15));
                    } else {
                        memcpy(py, vaddr, ((mVideoWidth+15)&(~15)) * ((mVideoHeight+15)&(~15)) * 3/2);
                    }

                    if (mapper.unlock(buf)) {
                        return;
                    }
                } else {
                    ALOGE("Error MetadataBufferType %d", type);
                    return;
                }
            } else {
                if (mPbuf_yuv_v == NULL) {
                    int32 yuv_size = ((mVideoWidth+15)&(~15))*((mVideoHeight+15)&(~15))*3/2;
                    if(mIOMMUEnabled) {
                        mYUVInPmemHeap = new MemoryHeapIon("/dev/ion", yuv_size, MemoryHeapBase::NO_CACHING, ION_HEAP_ID_MASK_SYSTEM);
                    } else {
                        mYUVInPmemHeap = new MemoryHeapIon("/dev/ion", yuv_size, MemoryHeapBase::NO_CACHING, ION_HEAP_ID_MASK_MM);
                    }
                    if (mYUVInPmemHeap->getHeapID() < 0) {
                        ALOGE("Failed to alloc yuv buffer");
                        return;
                    }
                    int ret,phy_addr, buffer_size;
                    if(mIOMMUEnabled) {
                        ret = mYUVInPmemHeap->get_mm_iova(&phy_addr, &buffer_size);
                    } else {
                        ret = mYUVInPmemHeap->get_phy_addr_from_ion(&phy_addr, &buffer_size);
                    }
                    if(ret) {
                        ALOGE("Failed to get_phy_addr_from_ion %d", ret);
                        return;
                    }
                    mPbuf_yuv_v =(uint8_t *) mYUVInPmemHeap->base();
                    mPbuf_yuv_p = phy_addr;
                    mPbuf_yuv_size = buffer_size;
                }


                py = mPbuf_yuv_v;
                py_phy = (uint8_t*)mPbuf_yuv_p;

                if (mVideoColorFormat != OMX_COLOR_FormatYUV420SemiPlanar) {
                    ConvertYUV420PlanarToYUV420SemiPlanar(inputData, py, mVideoWidth, mVideoHeight,
                                                         (mVideoWidth + 15) & (~15), (mVideoHeight + 15) & (~15));
                } else {
                    memcpy(py, inputData, ((mVideoWidth+15)&(~15)) * ((mVideoHeight+15)&(~15)) * 3/2);
                }
            }

            vid_in.time_stamp = (inHeader->nTimeStamp + 500) / 1000;  // in ms;
            vid_in.channel_quality = 1;
            vid_in.vopType = (mNumInputFrames % mVideoFrameRate) ? 1 : 0;
            vid_in.p_src_y = py;
            vid_in.p_src_v = 0;
            vid_in.p_src_y_phy = py_phy;
            vid_in.p_src_v_phy = 0;
            if(width != 0 && height != 0) {
                vid_in.p_src_u = py + ((width+15)&(~15)) * ((height+15)&(~15));
                vid_in.p_src_u_phy = py_phy + ((width+15)&(~15)) * ((height+15)&(~15));
            } else {
                vid_in.p_src_u = py + ((mVideoWidth+15)&(~15)) * ((mVideoHeight+15)&(~15));
                vid_in.p_src_u_phy = py_phy + ((mVideoWidth+15)&(~15)) * ((mVideoHeight+15)&(~15));
            }
            vid_in.org_img_width = (int32_t)width;
            vid_in.org_img_height = (int32_t)height;
            vid_in.crop_x = (int32_t)x;
            vid_in.crop_y = (int32_t)y;
#ifdef SPRD_DUMP_YUV
            if (mFile_yuv != NULL) {
                fwrite(py, 1, ((mVideoWidth+15)&(~15)) * ((mVideoHeight+15)&(~15))*3/2, mFile_yuv);
            }
#endif
            //dump_yuv(py, width*height*3/2);
            int64_t start_encode = systemTime();
            int ret = (*mMP4EncStrmEncode)(mHandle, &vid_in, &vid_out);
            int64_t end_encode = systemTime();
            ALOGI("MP4EncStrmEncode[%lld] %dms, in {%p-%p, %dx%d}, out {%p-%d}, wh{%d, %d}, xy{%d, %d}",
                  mNumInputFrames, (unsigned int)((end_encode-start_encode) / 1000000L), py, py_phy,
                  mVideoWidth, mVideoHeight, vid_out.pOutBuf, vid_out.strmSize, width, height, x, y);
            if ((vid_out.strmSize < 0) || (ret != MMENC_OK)) {
                ALOGE("Failed to encode frame %lld, ret=%d", mNumInputFrames, ret);
                mSignalledError = true;
                notify(OMX_EventError, OMX_ErrorUndefined, 0, 0);
            }

#ifdef SPRD_DUMP_BS
            if (mFile_bs != NULL) {
                fwrite(vid_out.pOutBuf, 1, vid_out.strmSize, mFile_bs);
            }
#endif

            if(vid_out.strmSize > 0) {
                dataLength = vid_out.strmSize;
                memcpy(outPtr, vid_out.pOutBuf, dataLength);
                if (vid_in.vopType == 0) {
                    outHeader->nFlags |= OMX_BUFFERFLAG_SYNCFRAME;
                }
            } else {
                dataLength = 0;
            }

            ++mNumInputFrames;
        } else {
            dataLength = 0;
        }

        if ((inHeader->nFlags & OMX_BUFFERFLAG_EOS) && (inHeader->nFilledLen == 0)) {
            // We also tag this output buffer with EOS if it corresponds
            // to the final input buffer.
            outHeader->nFlags = OMX_BUFFERFLAG_EOS;
        }

        inQueue.erase(inQueue.begin());
        inInfo->mOwnedByUs = false;
        notifyEmptyBufferDone(inHeader);

        outQueue.erase(outQueue.begin());
        CHECK(!mInputBufferInfoVec.empty());
        InputBufferInfo *inputBufInfo = mInputBufferInfoVec.begin();
        outHeader->nTimeStamp = inputBufInfo->mTimeUs;
        outHeader->nFlags |= (inputBufInfo->mFlags | OMX_BUFFERFLAG_ENDOFFRAME);
        outHeader->nFilledLen = dataLength;
        mInputBufferInfoVec.erase(mInputBufferInfoVec.begin());
        outInfo->mOwnedByUs = false;
        notifyFillBufferDone(outHeader);
    }
}

bool SPRDMPEG4Encoder::openEncoder(const char* libName)
{
    if(mLibHandle) {
        dlclose(mLibHandle);
    }

    ALOGI("openEncoder, lib: %s",libName);

    mLibHandle = dlopen(libName, RTLD_NOW);
    if(mLibHandle == NULL) {
        ALOGE("openEncoder, can't open lib: %s",libName);
        return false;
    }

    mMP4EncGetCodecCapability = (FT_MP4EncGetCodecCapability)dlsym(mLibHandle, "MP4EncGetCodecCapability");
    if(mMP4EncGetCodecCapability == NULL) {
        ALOGE("Can't find MP4EncGetCodecCapability in %s",libName);
        dlclose(mLibHandle);
        mLibHandle = NULL;
        return false;
    }

    mMP4EncPreInit = (FT_MP4EncPreInit)dlsym(mLibHandle, "MP4EncPreInit");
    if(mMP4EncPreInit == NULL) {
        ALOGE("Can't find MP4EncPreInit in %s",libName);
        dlclose(mLibHandle);
        mLibHandle = NULL;
        return false;
    }

    mMP4EncInit = (FT_MP4EncInit)dlsym(mLibHandle, "MP4EncInit");
    if(mMP4EncInit == NULL) {
        ALOGE("Can't find MP4EncInit in %s",libName);
        dlclose(mLibHandle);
        mLibHandle = NULL;
        return false;
    }

    mMP4EncSetConf = (FT_MP4EncSetConf)dlsym(mLibHandle, "MP4EncSetConf");
    if(mMP4EncSetConf == NULL) {
        ALOGE("Can't find MP4EncSetConf in %s",libName);
        dlclose(mLibHandle);
        mLibHandle = NULL;
        return false;
    }

    mMP4EncGetConf = (FT_MP4EncGetConf)dlsym(mLibHandle, "MP4EncGetConf");
    if(mMP4EncGetConf == NULL) {
        ALOGE("Can't find MP4EncGetConf in %s",libName);
        dlclose(mLibHandle);
        mLibHandle = NULL;
        return false;
    }

    mMP4EncStrmEncode = (FT_MP4EncStrmEncode)dlsym(mLibHandle, "MP4EncStrmEncode");
    if(mMP4EncStrmEncode == NULL) {
        ALOGE("Can't find MP4EncStrmEncode in %s",libName);
        dlclose(mLibHandle);
        mLibHandle = NULL;
        return false;
    }

    mMP4EncGenHeader = (FT_MP4EncGenHeader)dlsym(mLibHandle, "MP4EncGenHeader");
    if(mMP4EncGenHeader == NULL) {
        ALOGE("Can't find MP4EncGenHeader in %s",libName);
        dlclose(mLibHandle);
        mLibHandle = NULL;
        return false;
    }

    mMP4EncRelease = (FT_MP4EncRelease)dlsym(mLibHandle, "MP4EncRelease");
    if(mMP4EncRelease == NULL) {
        ALOGE("Can't find MP4EncRelease in %s",libName);
        dlclose(mLibHandle);
        mLibHandle = NULL;
    }

    return true;
}

}  // namespace android

android::SprdOMXComponent *createSprdOMXComponent(
    const char *name, const OMX_CALLBACKTYPE *callbacks,
    OMX_PTR appData, OMX_COMPONENTTYPE **component) {
    return new android::SPRDMPEG4Encoder(name, callbacks, appData, component);
}
