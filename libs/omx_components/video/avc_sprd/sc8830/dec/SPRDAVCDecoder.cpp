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

#define LOG_NDEBUG 0
#define LOG_TAG "SPRDAVCDecoder"
#include <utils/Log.h>

#include "SPRDAVCDecoder.h"

#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/MediaErrors.h>
#include <media/IOMX.h>

#include <dlfcn.h>
#include <media/hardware/HardwareAPI.h>
#include <ui/GraphicBufferMapper.h>

#include "gralloc_priv.h"
#include "ion_sprd.h"
#include "avc_dec_api.h"

//#define VIDEODEC_CURRENT_OPT  /*only open for SAMSUNG currently*/


namespace android {

static const CodecProfileLevel kProfileLevels[] = {
    { OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel1  },
    { OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel1b },
    { OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel11 },
    { OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel12 },
    { OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel13 },
    { OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel2  },
    { OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel21 },
    { OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel22 },
    { OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel3  },
    { OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel31 },
    { OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel32 },
    { OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel4  },
    { OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel41 },
    { OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel42 },
    { OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel5  },
    { OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel51 },

    { OMX_VIDEO_AVCProfileMain, OMX_VIDEO_AVCLevel1  },
    { OMX_VIDEO_AVCProfileMain, OMX_VIDEO_AVCLevel1b },
    { OMX_VIDEO_AVCProfileMain, OMX_VIDEO_AVCLevel11 },
    { OMX_VIDEO_AVCProfileMain, OMX_VIDEO_AVCLevel12 },
    { OMX_VIDEO_AVCProfileMain, OMX_VIDEO_AVCLevel13 },
    { OMX_VIDEO_AVCProfileMain, OMX_VIDEO_AVCLevel2  },
    { OMX_VIDEO_AVCProfileMain, OMX_VIDEO_AVCLevel21 },
    { OMX_VIDEO_AVCProfileMain, OMX_VIDEO_AVCLevel22 },
    { OMX_VIDEO_AVCProfileMain, OMX_VIDEO_AVCLevel3  },
    { OMX_VIDEO_AVCProfileMain, OMX_VIDEO_AVCLevel31 },
    { OMX_VIDEO_AVCProfileMain, OMX_VIDEO_AVCLevel32 },
    { OMX_VIDEO_AVCProfileMain, OMX_VIDEO_AVCLevel4  },
    { OMX_VIDEO_AVCProfileMain, OMX_VIDEO_AVCLevel41 },
    { OMX_VIDEO_AVCProfileMain, OMX_VIDEO_AVCLevel42 },
    { OMX_VIDEO_AVCProfileMain, OMX_VIDEO_AVCLevel5  },
    { OMX_VIDEO_AVCProfileMain, OMX_VIDEO_AVCLevel51 },

    { OMX_VIDEO_AVCProfileHigh, OMX_VIDEO_AVCLevel1  },
    { OMX_VIDEO_AVCProfileHigh, OMX_VIDEO_AVCLevel1b },
    { OMX_VIDEO_AVCProfileHigh, OMX_VIDEO_AVCLevel11 },
    { OMX_VIDEO_AVCProfileHigh, OMX_VIDEO_AVCLevel12 },
    { OMX_VIDEO_AVCProfileHigh, OMX_VIDEO_AVCLevel13 },
    { OMX_VIDEO_AVCProfileHigh, OMX_VIDEO_AVCLevel2  },
    { OMX_VIDEO_AVCProfileHigh, OMX_VIDEO_AVCLevel21 },
    { OMX_VIDEO_AVCProfileHigh, OMX_VIDEO_AVCLevel22 },
    { OMX_VIDEO_AVCProfileHigh, OMX_VIDEO_AVCLevel3  },
    { OMX_VIDEO_AVCProfileHigh, OMX_VIDEO_AVCLevel31 },
    { OMX_VIDEO_AVCProfileHigh, OMX_VIDEO_AVCLevel32 },
    { OMX_VIDEO_AVCProfileHigh, OMX_VIDEO_AVCLevel4  },
    { OMX_VIDEO_AVCProfileHigh, OMX_VIDEO_AVCLevel41 },
    { OMX_VIDEO_AVCProfileHigh, OMX_VIDEO_AVCLevel42 },
    { OMX_VIDEO_AVCProfileHigh, OMX_VIDEO_AVCLevel5  },
    { OMX_VIDEO_AVCProfileHigh, OMX_VIDEO_AVCLevel51 },
};

template<class T>
static void InitOMXParams(T *params) {
    params->nSize = sizeof(T);
    params->nVersion.s.nVersionMajor = 1;
    params->nVersion.s.nVersionMinor = 0;
    params->nVersion.s.nRevision = 0;
    params->nVersion.s.nStep = 0;
}

SPRDAVCDecoder::SPRDAVCDecoder(
    const char *name,
    const OMX_CALLBACKTYPE *callbacks,
    OMX_PTR appData,
    OMX_COMPONENTTYPE **component)
    : SprdSimpleOMXComponent(name, callbacks, appData, component),
      mHandle(new tagAVCHandle),
      mInputBufferCount(0),
      mWidth(320),
      mHeight(240),
      mPictureSize(mWidth * mHeight * 3 / 2),
      mCropLeft(0),
      mCropTop(0),
      mCropWidth(mWidth),
      mCropHeight(mHeight),
      mMaxWidth(352),
      mMaxHeight(288),
      mPicId(0),
      mSetFreqCount(0),
      mHeadersDecoded(false),
      mEOSStatus(INPUT_DATA_AVAILABLE),
      mOutputPortSettingsChange(NONE),
      mSignalledError(false),
      mLibHandle(NULL),
      mDecoderSwFlag(false),
      mChangeToSwDec(false),
      mAllocateBuffers(false),
      mNeedIVOP(true),
      mIOMMUEnabled(false),
      mCodecInterBuffer(NULL),
      mCodecExtraBuffer(NULL),
      mPbuf_extra_v(NULL),
      mPbuf_extra_p(0),
      mPbuf_extra_size(0),
      mPbuf_stream_v(NULL),
      mPbuf_stream_p(0),
      mPbuf_stream_size(0),
      mH264DecInit(NULL),
      mH264DecGetInfo(NULL),
      mH264DecDecode(NULL),
      mH264DecRelease(NULL),
      mH264Dec_SetCurRecPic(NULL),
      mH264Dec_GetLastDspFrm(NULL),
      mH264Dec_ReleaseRefBuffers(NULL),
      mH264DecMemInit(NULL) {

    ALOGI("Construct SPRDAVCDecoder, this: %0x", (void *)this);

    //read config flag
#define USE_SW_DECODER	0x01
#define USE_HW_DECODER	0x00

    uint8 video_cfg = USE_HW_DECODER;
    FILE *fp = fopen("/data/data/com.sprd.test.videoplayer/app_decode/flag", "rb");
    if (fp != NULL) {
        fread(&video_cfg, sizeof(uint8), 1, fp);
        fclose(fp);
    }
    ALOGI("%s, video_cfg: %d", __FUNCTION__, video_cfg);

    bool ret = false;
    if (USE_HW_DECODER == video_cfg) {
        ret = openDecoder("libomx_avcdec_hw_sprd.so");
    }

    if(ret == false) {
        ret = openDecoder("libomx_avcdec_sw_sprd.so");
        mDecoderSwFlag = true;
    }

    CHECK_EQ(ret, true);

    mIOMMUEnabled = MemoryHeapIon::Mm_iommu_is_enabled();
    ALOGI("%s, is IOMMU enabled: %d", __FUNCTION__, mIOMMUEnabled);

    if(mDecoderSwFlag) {
        CHECK_EQ(initDecoder(), (status_t)OK);
    } else {
        if(initDecoder() != OK) {
            ret = openDecoder("libomx_avcdec_sw_sprd.so");
            mDecoderSwFlag = true;
            CHECK_EQ(ret, true);
            CHECK_EQ(initDecoder(), (status_t)OK);
        }
    }

    initPorts();

    iUseAndroidNativeBuffer[OMX_DirInput] = OMX_FALSE;
    iUseAndroidNativeBuffer[OMX_DirOutput] = OMX_FALSE;
}

SPRDAVCDecoder::~SPRDAVCDecoder() {
    ALOGI("Destruct SPRDAVCDecoder, this: %0x", (void *)this);

    releaseDecoder();

    while (mSetFreqCount > 0)
    {
        set_ddr_freq("0");
        mSetFreqCount--;
    }

    delete mHandle;
    mHandle = NULL;

    List<BufferInfo *> &outQueue = getPortQueue(kOutputPortIndex);
    List<BufferInfo *> &inQueue = getPortQueue(kInputPortIndex);
    CHECK(outQueue.empty());
    CHECK(inQueue.empty());
}

void SPRDAVCDecoder::initPorts() {
    OMX_PARAM_PORTDEFINITIONTYPE def;
    InitOMXParams(&def);

    def.nPortIndex = kInputPortIndex;
    def.eDir = OMX_DirInput;
    def.nBufferCountMin = 1;
    def.nBufferCountActual = kNumInputBuffers;
    def.nBufferSize = 128*1024 ;///8192;
    def.bEnabled = OMX_TRUE;
    def.bPopulated = OMX_FALSE;
    def.eDomain = OMX_PortDomainVideo;
    def.bBuffersContiguous = OMX_FALSE;
    def.nBufferAlignment = 1;

    def.format.video.cMIMEType = const_cast<char *>(MEDIA_MIMETYPE_VIDEO_AVC);
    def.format.video.pNativeRender = NULL;
    def.format.video.nFrameWidth = mWidth;
    def.format.video.nFrameHeight = mHeight;
    def.format.video.nStride = def.format.video.nFrameWidth;
    def.format.video.nSliceHeight = def.format.video.nFrameHeight;
    def.format.video.nBitrate = 0;
    def.format.video.xFramerate = 0;
    def.format.video.bFlagErrorConcealment = OMX_FALSE;
    def.format.video.eCompressionFormat = OMX_VIDEO_CodingAVC;
    def.format.video.eColorFormat = OMX_COLOR_FormatUnused;
    def.format.video.pNativeWindow = NULL;

    addPort(def);

    def.nPortIndex = kOutputPortIndex;
    def.eDir = OMX_DirOutput;
    def.nBufferCountMin = 2;
    def.nBufferCountActual = kNumOutputBuffers;
    def.bEnabled = OMX_TRUE;
    def.bPopulated = OMX_FALSE;
    def.eDomain = OMX_PortDomainVideo;
    def.bBuffersContiguous = OMX_FALSE;
    def.nBufferAlignment = 2;

    def.format.video.cMIMEType = const_cast<char *>(MEDIA_MIMETYPE_VIDEO_RAW);
    def.format.video.pNativeRender = NULL;
    def.format.video.nFrameWidth = mWidth;
    def.format.video.nFrameHeight = mHeight;
    def.format.video.nStride = def.format.video.nFrameWidth;
    def.format.video.nSliceHeight = def.format.video.nFrameHeight;
    def.format.video.nBitrate = 0;
    def.format.video.xFramerate = 0;
    def.format.video.bFlagErrorConcealment = OMX_FALSE;
    def.format.video.eCompressionFormat = OMX_VIDEO_CodingUnused;
    def.format.video.eColorFormat = OMX_COLOR_FormatYUV420SemiPlanar;
    def.format.video.pNativeWindow = NULL;

    def.nBufferSize =
        (def.format.video.nFrameWidth * def.format.video.nFrameHeight * 3) / 2;

    addPort(def);
}

void SPRDAVCDecoder::set_ddr_freq(const char* freq_in_khz)
{
    const char* const set_freq = "/sys/devices/platform/scxx30-dmcfreq.0/devfreq/scxx30-dmcfreq.0/ondemand/set_freq";
    FILE* fp = fopen(set_freq, "w");
    if (fp != NULL)
    {
        fprintf(fp, "%s", freq_in_khz);
        ALOGE("set ddr freq to %skhz", freq_in_khz);
        fclose(fp);
    }
    else
    {
        ALOGE("Failed to open %s", set_freq);
    }
}

void SPRDAVCDecoder::change_ddr_freq()
{
    if(!mDecoderSwFlag)
	{
        uint32_t frame_size = mWidth * mHeight;
        char* ddr_freq;

        if(frame_size > 1280*720)
        {
            ddr_freq = "500000";
        }
#ifdef VIDEODEC_CURRENT_OPT
        else if(frame_size > 864*480)
        {
            ddr_freq = "300000";
        }
#else
        else if(frame_size > 720*576)
        {
            ddr_freq = "400000";
        }
        else if(frame_size > 320*240)
        {
            ddr_freq = "300000";
        }
#endif
        else
        {
            ddr_freq = "200000";
        }
        set_ddr_freq(ddr_freq);
        mSetFreqCount ++;
    }
}

status_t SPRDAVCDecoder::initDecoder() {

    memset(mHandle, 0, sizeof(tagAVCHandle));

    mHandle->userdata = (void *)this;
    mHandle->VSP_bindCb = BindFrameWrapper;
    mHandle->VSP_unbindCb = UnbindFrameWrapper;
    mHandle->VSP_extMemCb = ExtMemAllocWrapper;

    int32 phy_addr = 0;
    int32 size = 0, size_stream;

    size_stream = H264_DECODER_STREAM_BUFFER_SIZE;
    if (mDecoderSwFlag) {
        mPbuf_stream_v = (unsigned char*)malloc(size_stream * sizeof(unsigned char));
        mPbuf_stream_p = (int32)0;
        mPbuf_stream_size = (int32)size_stream;
    } else {
        if (mIOMMUEnabled) {
            mPmem_stream = new MemoryHeapIon(SPRD_ION_DEV, size_stream, MemoryHeapBase::NO_CACHING, ION_HEAP_ID_MASK_SYSTEM);
        } else {
            mPmem_stream = new MemoryHeapIon(SPRD_ION_DEV, size_stream, MemoryHeapBase::NO_CACHING, ION_HEAP_ID_MASK_MM);
        }
        if (mPmem_stream->getHeapID() < 0) {
            ALOGE("Failed to alloc bitstream pmem buffer, getHeapID failed");
            return OMX_ErrorInsufficientResources;
        } else {
            int32 ret;
            if (mIOMMUEnabled) {
                ret = mPmem_stream->get_mm_iova(&phy_addr, &size);
            } else {
                ret = mPmem_stream->get_phy_addr_from_ion(&phy_addr, &size);
            }
            if (ret < 0) {
                ALOGE("Failed to alloc bitstream pmem buffer, get phy addr failed");
                return OMX_ErrorInsufficientResources;
            } else {
                mPbuf_stream_v = (unsigned char*)mPmem_stream->base();
                mPbuf_stream_p = (int32)phy_addr;
                mPbuf_stream_size = (int32)size;
                ALOGI("pmem %p - %p - %d", mPbuf_stream_p, mPbuf_stream_v, mPbuf_stream_size);
            }
        }
    }

    int32 size_inter = H264_DECODER_INTERNAL_BUFFER_SIZE;
    mCodecInterBuffer = (uint8 *)malloc(size_inter);

    MMCodecBuffer codec_buf;
    MMDecVideoFormat video_format;

    codec_buf.common_buffer_ptr = (uint8 *)(mCodecInterBuffer);
    codec_buf.common_buffer_ptr_phy = 0;
    codec_buf.size = size_inter;
    codec_buf.int_buffer_ptr = NULL;
    codec_buf.int_size = 0;

    video_format.video_std = H264;
    video_format.frame_width = 0;
    video_format.frame_height = 0;
    video_format.p_extra = NULL;
    video_format.p_extra_phy = 0;
    video_format.i_extra = 0;
    video_format.uv_interleaved = 1;

    if ((*mH264DecInit)(mHandle, &codec_buf,&video_format) != MMDEC_OK) {
        ALOGE("Failed to init AVCDEC");
        return OMX_ErrorUndefined;
    }

    //int32 codec_capabilty;
    if ((*mH264GetCodecCapability)(mHandle, &mMaxWidth, &mMaxHeight) != MMDEC_OK) {
        ALOGE("Failed to mH264GetCodecCapability");
    }

    return OMX_ErrorNone;
}

void SPRDAVCDecoder::releaseDecoder() {
    (*mH264DecRelease)(mHandle);

    if (mCodecInterBuffer != NULL) {
        free(mCodecInterBuffer);
        mCodecInterBuffer = NULL;
    }

    if (mCodecExtraBuffer != NULL) {
        free(mCodecExtraBuffer);
        mCodecExtraBuffer = NULL;
    }

    if (mPbuf_stream_v != NULL) {
        if (mDecoderSwFlag) {
            free(mPbuf_stream_v);
            mPbuf_stream_v = NULL;
        } else {
            if (mIOMMUEnabled) {
                mPmem_stream->free_mm_iova(mPbuf_stream_p, mPbuf_stream_size);
            }
            mPmem_stream.clear();
            mPbuf_stream_v = NULL;
            mPbuf_stream_p = 0;
            mPbuf_stream_size = 0;
        }
    }
    if (mPbuf_extra_v != NULL) {
        if (mIOMMUEnabled) {
            mPmem_extra->free_mm_iova(mPbuf_extra_p, mPbuf_extra_size);
        }
        mPmem_extra.clear();
        mPbuf_extra_v = NULL;
        mPbuf_extra_p = 0;
        mPbuf_extra_size = 0;
    }

    if(mLibHandle) {
        dlclose(mLibHandle);
        mLibHandle = NULL;
    }
}

OMX_ERRORTYPE SPRDAVCDecoder::internalGetParameter(
    OMX_INDEXTYPE index, OMX_PTR params) {
    switch (index) {
    case OMX_IndexParamVideoPortFormat:
    {
        OMX_VIDEO_PARAM_PORTFORMATTYPE *formatParams =
            (OMX_VIDEO_PARAM_PORTFORMATTYPE *)params;

        if (formatParams->nPortIndex > kOutputPortIndex) {
            return OMX_ErrorUndefined;
        }

        if (formatParams->nIndex != 0) {
            return OMX_ErrorNoMore;
        }

        if (formatParams->nPortIndex == kInputPortIndex) {
            formatParams->eCompressionFormat = OMX_VIDEO_CodingAVC;
            formatParams->eColorFormat = OMX_COLOR_FormatUnused;
            formatParams->xFramerate = 0;
        } else {
            CHECK(formatParams->nPortIndex == kOutputPortIndex);

            PortInfo *pOutPort = editPortInfo(OMX_DirOutput);
            ALOGI("internalGetParameter, OMX_IndexParamVideoPortFormat, eColorFormat: 0x%x",pOutPort->mDef.format.video.eColorFormat);
            formatParams->eCompressionFormat = OMX_VIDEO_CodingUnused;
            formatParams->eColorFormat = pOutPort->mDef.format.video.eColorFormat;
            formatParams->xFramerate = 0;
        }

        return OMX_ErrorNone;
    }

    case OMX_IndexParamVideoProfileLevelQuerySupported:
    {
        OMX_VIDEO_PARAM_PROFILELEVELTYPE *profileLevel =
            (OMX_VIDEO_PARAM_PROFILELEVELTYPE *) params;

        if (profileLevel->nPortIndex != kInputPortIndex) {
            ALOGE("Invalid port index: %ld", profileLevel->nPortIndex);
            return OMX_ErrorUnsupportedIndex;
        }

        size_t index = profileLevel->nProfileIndex;
        size_t nProfileLevels =
            sizeof(kProfileLevels) / sizeof(kProfileLevels[0]);
        if (index >= nProfileLevels) {
            return OMX_ErrorNoMore;
        }

        profileLevel->eProfile = kProfileLevels[index].mProfile;
        profileLevel->eLevel = kProfileLevels[index].mLevel;
        return OMX_ErrorNone;
    }

    case OMX_IndexParamEnableAndroidBuffers:
    {
        EnableAndroidNativeBuffersParams *peanbp = (EnableAndroidNativeBuffersParams *)params;
        peanbp->enable = iUseAndroidNativeBuffer[OMX_DirOutput];
        ALOGI("internalGetParameter, OMX_IndexParamEnableAndroidBuffers %d",peanbp->enable);
        return OMX_ErrorNone;
    }

    case OMX_IndexParamGetAndroidNativeBuffer:
    {
        GetAndroidNativeBufferUsageParams *pganbp;

        pganbp = (GetAndroidNativeBufferUsageParams *)params;
        if(mDecoderSwFlag || mIOMMUEnabled) {
            pganbp->nUsage = GRALLOC_USAGE_SW_READ_OFTEN |GRALLOC_USAGE_SW_WRITE_OFTEN;
        } else {
            pganbp->nUsage = GRALLOC_USAGE_VIDEO_BUFFER | GRALLOC_USAGE_SW_READ_OFTEN |GRALLOC_USAGE_SW_WRITE_OFTEN;
        }
        ALOGI("internalGetParameter, OMX_IndexParamGetAndroidNativeBuffer %x",pganbp->nUsage);
        return OMX_ErrorNone;
    }

    default:
        return SprdSimpleOMXComponent::internalGetParameter(index, params);
    }
}

OMX_ERRORTYPE SPRDAVCDecoder::internalSetParameter(
    OMX_INDEXTYPE index, const OMX_PTR params) {
    switch (index) {
    case OMX_IndexParamStandardComponentRole:
    {
        const OMX_PARAM_COMPONENTROLETYPE *roleParams =
            (const OMX_PARAM_COMPONENTROLETYPE *)params;

        if (strncmp((const char *)roleParams->cRole,
                    "video_decoder.avc",
                    OMX_MAX_STRINGNAME_SIZE - 1)) {
            return OMX_ErrorUndefined;
        }

        return OMX_ErrorNone;
    }

    case OMX_IndexParamVideoPortFormat:
    {
        OMX_VIDEO_PARAM_PORTFORMATTYPE *formatParams =
            (OMX_VIDEO_PARAM_PORTFORMATTYPE *)params;

        if (formatParams->nPortIndex > kOutputPortIndex) {
            return OMX_ErrorUndefined;
        }

        if (formatParams->nIndex != 0) {
            return OMX_ErrorNoMore;
        }

        return OMX_ErrorNone;
    }

    case OMX_IndexParamEnableAndroidBuffers:
    {
        EnableAndroidNativeBuffersParams *peanbp = (EnableAndroidNativeBuffersParams *)params;
        PortInfo *pOutPort = editPortInfo(1);
        if (peanbp->enable == OMX_FALSE) {
            ALOGI("internalSetParameter, disable AndroidNativeBuffer");
            iUseAndroidNativeBuffer[OMX_DirOutput] = OMX_FALSE;

            pOutPort->mDef.format.video.eColorFormat = OMX_COLOR_FormatYUV420SemiPlanar;
        } else {
            ALOGI("internalSetParameter, enable AndroidNativeBuffer");
            iUseAndroidNativeBuffer[OMX_DirOutput] = OMX_TRUE;

            pOutPort->mDef.format.video.eColorFormat = OMX_COLOR_FormatYUV420SemiPlanar;
        }
        return OMX_ErrorNone;
    }

    case OMX_IndexParamPortDefinition:
    {
        OMX_PARAM_PORTDEFINITIONTYPE *defParams =
            (OMX_PARAM_PORTDEFINITIONTYPE *)params;

        if (defParams->nPortIndex > 1
                || defParams->nSize
                != sizeof(OMX_PARAM_PORTDEFINITIONTYPE)) {
            return OMX_ErrorUndefined;
        }

        PortInfo *port = editPortInfo(defParams->nPortIndex);

        if (defParams->nBufferSize != port->mDef.nBufferSize) {
            CHECK_GE(defParams->nBufferSize, port->mDef.nBufferSize);
            port->mDef.nBufferSize = defParams->nBufferSize;
        }

        if (defParams->nBufferCountActual
                != port->mDef.nBufferCountActual) {
            CHECK_GE(defParams->nBufferCountActual,
                     port->mDef.nBufferCountMin);

            port->mDef.nBufferCountActual = defParams->nBufferCountActual;
        }

        memcpy(&port->mDef.format.video, &defParams->format.video, sizeof(OMX_VIDEO_PORTDEFINITIONTYPE));
        if(defParams->nPortIndex == kOutputPortIndex) {
            port->mDef.format.video.nStride = port->mDef.format.video.nFrameWidth;
            port->mDef.format.video.nSliceHeight = port->mDef.format.video.nFrameHeight;
            mWidth = port->mDef.format.video.nFrameWidth;
            mHeight = port->mDef.format.video.nFrameHeight;
            mCropWidth = mWidth;
            mCropHeight = mHeight;
            port->mDef.nBufferSize =(((mWidth + 15) & -16)* ((mHeight + 15) & -16) * 3) / 2;
            mPictureSize = port->mDef.nBufferSize;
            change_ddr_freq();
        }

        if (!((mWidth < 1280 && mHeight < 720) || (mWidth < 720 && mHeight < 1280))) {
            PortInfo *port = editPortInfo(kInputPortIndex);
            if(port->mDef.nBufferSize < 384*1024)
                port->mDef.nBufferSize = 384*1024;
        } else if (!((mWidth < 720 && mHeight < 480) || (mWidth < 480 && mHeight < 720))) {
            PortInfo *port = editPortInfo(kInputPortIndex);
            if(port->mDef.nBufferSize < 256*1024)
                port->mDef.nBufferSize = 256*1024;
        }

        return OMX_ErrorNone;
    }

    default:
        return SprdSimpleOMXComponent::internalSetParameter(index, params);
    }
}

OMX_ERRORTYPE SPRDAVCDecoder::internalUseBuffer(
    OMX_BUFFERHEADERTYPE **header,
    OMX_U32 portIndex,
    OMX_PTR appPrivate,
    OMX_U32 size,
    OMX_U8 *ptr,
    BufferPrivateStruct* bufferPrivate) {

    *header = new OMX_BUFFERHEADERTYPE;
    (*header)->nSize = sizeof(OMX_BUFFERHEADERTYPE);
    (*header)->nVersion.s.nVersionMajor = 1;
    (*header)->nVersion.s.nVersionMinor = 0;
    (*header)->nVersion.s.nRevision = 0;
    (*header)->nVersion.s.nStep = 0;
    (*header)->pBuffer = ptr;
    (*header)->nAllocLen = size;
    (*header)->nFilledLen = 0;
    (*header)->nOffset = 0;
    (*header)->pAppPrivate = appPrivate;
    (*header)->pPlatformPrivate = NULL;
    (*header)->pInputPortPrivate = NULL;
    (*header)->pOutputPortPrivate = NULL;
    (*header)->hMarkTargetComponent = NULL;
    (*header)->pMarkData = NULL;
    (*header)->nTickCount = 0;
    (*header)->nTimeStamp = 0;
    (*header)->nFlags = 0;
    (*header)->nOutputPortIndex = portIndex;
    (*header)->nInputPortIndex = portIndex;

    if(portIndex == OMX_DirOutput) {
        (*header)->pOutputPortPrivate = new BufferCtrlStruct;
        CHECK((*header)->pOutputPortPrivate != NULL);
        BufferCtrlStruct* pBufCtrl= (BufferCtrlStruct*)((*header)->pOutputPortPrivate);
        pBufCtrl->iRefCount = 1; //init by1
        if(mAllocateBuffers) {
            if(bufferPrivate != NULL) {
                pBufCtrl->pMem = ((BufferPrivateStruct*)bufferPrivate)->pMem;
                pBufCtrl->phyAddr = ((BufferPrivateStruct*)bufferPrivate)->phyAddr;
                pBufCtrl->bufferSize = ((BufferPrivateStruct*)bufferPrivate)->bufferSize;
                pBufCtrl->bufferFd = 0;
            } else {
                pBufCtrl->pMem = NULL;
                pBufCtrl->phyAddr = NULL;
                pBufCtrl->bufferSize = 0;
                pBufCtrl->bufferFd = 0;
            }
        } else {
            bool iommu_is_enable = MemoryHeapIon::Mm_iommu_is_enabled();
            if (iommu_is_enable) {
                int picPhyAddr = 0, bufferSize = 0;
                native_handle_t *pNativeHandle = (native_handle_t *)((*header)->pBuffer);
                struct private_handle_t *private_h = (struct private_handle_t *)pNativeHandle;
                MemoryHeapIon::Get_mm_iova(private_h->share_fd,(int*)&picPhyAddr, &bufferSize);

                pBufCtrl->pMem = NULL;
                pBufCtrl->bufferFd = private_h->share_fd;
                pBufCtrl->phyAddr = picPhyAddr;
                pBufCtrl->bufferSize = bufferSize;
            } else {
                pBufCtrl->pMem = NULL;
                pBufCtrl->bufferFd = 0;
                pBufCtrl->phyAddr = 0;
                pBufCtrl->bufferSize = 0;
            }
        }
    }

    PortInfo *port = editPortInfo(portIndex);

    port->mBuffers.push();

    BufferInfo *buffer =
        &port->mBuffers.editItemAt(port->mBuffers.size() - 1);
    ALOGI("internalUseBuffer, header=%p, pBuffer=%p, size=%d",*header, ptr, size);
    buffer->mHeader = *header;
    buffer->mOwnedByUs = false;

    if (port->mBuffers.size() == port->mDef.nBufferCountActual) {
        port->mDef.bPopulated = OMX_TRUE;
        checkTransitions();
    }

    return OMX_ErrorNone;
}

OMX_ERRORTYPE SPRDAVCDecoder::allocateBuffer(
    OMX_BUFFERHEADERTYPE **header,
    OMX_U32 portIndex,
    OMX_PTR appPrivate,
    OMX_U32 size) {
    switch(portIndex)
    {
    case OMX_DirInput:
        return SprdSimpleOMXComponent::allocateBuffer(header, portIndex, appPrivate, size);

    case OMX_DirOutput:
    {
        mAllocateBuffers = true;
        if(mDecoderSwFlag) {
            return SprdSimpleOMXComponent::allocateBuffer(header, portIndex, appPrivate, size);
        } else {
            MemoryHeapIon* pMem = NULL;
            int phyAddr = 0;
            int bufferSize = 0;
            unsigned char* pBuffer = NULL;
            OMX_U32 size64word = (size + 1024*4 - 1) & ~(1024*4 - 1);

            if (mIOMMUEnabled) {
                pMem = new MemoryHeapIon(SPRD_ION_DEV, size64word, MemoryHeapBase::NO_CACHING, ION_HEAP_ID_MASK_SYSTEM);
            } else {
                pMem = new MemoryHeapIon(SPRD_ION_DEV, size64word, MemoryHeapBase::NO_CACHING, ION_HEAP_ID_MASK_MM);
            }

            if(pMem->getHeapID() < 0) {
                ALOGE("Failed to alloc outport pmem buffer");
                return OMX_ErrorInsufficientResources;
            }

            if (mIOMMUEnabled) {
                if(pMem->get_mm_iova(&phyAddr, &bufferSize)) {
                    ALOGE("get_mm_iova fail");
                    return OMX_ErrorInsufficientResources;
                }
            } else {
                if(pMem->get_phy_addr_from_ion(&phyAddr, &bufferSize)) {
                    ALOGE("get_phy_addr_from_ion fail");
                    return OMX_ErrorInsufficientResources;
                }
            }

            pBuffer = (unsigned char*)(pMem->base());
            BufferPrivateStruct* bufferPrivate = new BufferPrivateStruct();
            bufferPrivate->pMem = pMem;
            bufferPrivate->phyAddr = phyAddr;
            bufferPrivate->bufferSize = bufferSize;
            ALOGI("allocateBuffer, allocate buffer from pmem, pBuffer: 0x%x, phyAddr: 0x%x, size: %d", pBuffer, phyAddr, bufferSize);

            SprdSimpleOMXComponent::useBuffer(header, portIndex, appPrivate, bufferSize, pBuffer, bufferPrivate);
            delete bufferPrivate;

            return OMX_ErrorNone;
        }
    }

    default:
        return OMX_ErrorUnsupportedIndex;

    }
}

OMX_ERRORTYPE SPRDAVCDecoder::freeBuffer(
    OMX_U32 portIndex,
    OMX_BUFFERHEADERTYPE *header) {
    switch(portIndex)
    {
    case OMX_DirInput:
        return SprdSimpleOMXComponent::freeBuffer(portIndex, header);

    case OMX_DirOutput:
    {
        BufferCtrlStruct* pBufCtrl= (BufferCtrlStruct*)(header->pOutputPortPrivate);
        if(pBufCtrl != NULL) {
            if(pBufCtrl->pMem != NULL) {
                ALOGI("freeBuffer, phyAddr: 0x%x", pBufCtrl->phyAddr);
                if (mIOMMUEnabled) {
                    pBufCtrl->pMem->free_mm_iova(pBufCtrl->phyAddr, pBufCtrl->bufferSize);
                }
                pBufCtrl->pMem.clear();
            }
            return SprdSimpleOMXComponent::freeBuffer(portIndex, header);
        } else {
            ALOGE("freeBuffer, pBufCtrl==NULL");
            return OMX_ErrorUndefined;
        }
    }

    default:
        return OMX_ErrorUnsupportedIndex;
    }
}

OMX_ERRORTYPE SPRDAVCDecoder::getConfig(
    OMX_INDEXTYPE index, OMX_PTR params) {
    switch (index) {
    case OMX_IndexConfigCommonOutputCrop:
    {
        OMX_CONFIG_RECTTYPE *rectParams = (OMX_CONFIG_RECTTYPE *)params;

        if (rectParams->nPortIndex != 1) {
            return OMX_ErrorUndefined;
        }

        rectParams->nLeft = mCropLeft;
        rectParams->nTop = mCropTop;
        rectParams->nWidth = mCropWidth;
        rectParams->nHeight = mCropHeight;

        return OMX_ErrorNone;
    }

    default:
        return OMX_ErrorUnsupportedIndex;
    }
}

void dump_bs( uint8* pBuffer,int32 aInBufSize) {
    FILE *fp = fopen("/data/video_es.m4v","ab");
    fwrite(pBuffer,1,aInBufSize,fp);
    fclose(fp);
}

void dump_yuv( uint8* pBuffer,int32 aInBufSize) {
    FILE *fp = fopen("/data/video.yuv","ab");
    fwrite(pBuffer,1,aInBufSize,fp);
    fclose(fp);
}

void SPRDAVCDecoder::onQueueFilled(OMX_U32 portIndex) {
    if (mSignalledError || mOutputPortSettingsChange != NONE) {
        return;
    }

    if (mEOSStatus == OUTPUT_FRAMES_FLUSHED) {
        return;
    }

    if(mChangeToSwDec) {

        mChangeToSwDec = false;

        ALOGI("%s, %d, change to sw decoder", __FUNCTION__, __LINE__);

        releaseDecoder();

        if(!openDecoder("libomx_avcdec_sw_sprd.so")) {
            ALOGE("onQueueFilled, open  libomx_avcdec_sw_sprd.so failed.");
            notify(OMX_EventError, OMX_ErrorDynamicResourcesUnavailable, 0, NULL);
            mSignalledError = true;
            mDecoderSwFlag = false;
            return;
        }

        if(initDecoder() != OK) {
            ALOGE("onQueueFilled, init sw decoder failed.");
            notify(OMX_EventError, OMX_ErrorDynamicResourcesUnavailable, 0, NULL);
            mSignalledError = true;
            return;
        }
    }

    List<BufferInfo *> &inQueue = getPortQueue(kInputPortIndex);
    List<BufferInfo *> &outQueue = getPortQueue(kOutputPortIndex);

    while ((mEOSStatus != INPUT_DATA_AVAILABLE || !inQueue.empty())
            && outQueue.size() != 0) {

        if (mEOSStatus == INPUT_EOS_SEEN) {
            drainAllOutputBuffers();
            return;
        }

        BufferInfo *inInfo = *inQueue.begin();
        OMX_BUFFERHEADERTYPE *inHeader = inInfo->mHeader;

        List<BufferInfo *>::iterator itBuffer = outQueue.begin();
        OMX_BUFFERHEADERTYPE *outHeader = NULL;
        BufferCtrlStruct *pBufCtrl = NULL;
        uint32 count = 0;
        do {
            if(count >= outQueue.size()) {
                ALOGI("onQueueFilled, get outQueue buffer, return, count=%d, queue_size=%d",count, outQueue.size());
                return;
            }

            outHeader = (*itBuffer)->mHeader;
            pBufCtrl= (BufferCtrlStruct*)(outHeader->pOutputPortPrivate);
            if(pBufCtrl == NULL) {
                ALOGE("onQueueFilled, pBufCtrl == NULL, fail");
                notify(OMX_EventError, OMX_ErrorUndefined, 0, NULL);
                mSignalledError = true;
                return;
            }

            itBuffer++;
            count++;
        }
        while(pBufCtrl->iRefCount > 0);

//        ALOGI("%s, %d, mBuffer=0x%x, outHeader=0x%x, iRefCount=%d", __FUNCTION__, __LINE__, *itBuffer, outHeader, pBufCtrl->iRefCount);
        ALOGI("%s, %d, outHeader:0x%x, inHeader: 0x%x, len: %d, nOffset: %d, time: %lld, EOS: %d",
              __FUNCTION__, __LINE__,outHeader,inHeader, inHeader->nFilledLen,inHeader->nOffset, inHeader->nTimeStamp,inHeader->nFlags & OMX_BUFFERFLAG_EOS);

        ++mPicId;
        if (inHeader->nFlags & OMX_BUFFERFLAG_EOS) {
//bug253058 , the last frame size may be not zero, it need to be decoded.
//            inQueue.erase(inQueue.begin());
//           inInfo->mOwnedByUs = false;
//            notifyEmptyBufferDone(inHeader);
            mEOSStatus = INPUT_EOS_SEEN;
//            continue;
        }

        if(inHeader->nFilledLen == 0) {
            inInfo->mOwnedByUs = false;
            inQueue.erase(inQueue.begin());
            inInfo = NULL;
            notifyEmptyBufferDone(inHeader);
            inHeader = NULL;
            continue;
        }

        MMDecInput dec_in;
        MMDecOutput dec_out;

        uint8_t *bitstream = inHeader->pBuffer + inHeader->nOffset;
        int32_t bufferSize = inHeader->nFilledLen;

        if (mPbuf_stream_v != NULL) {
            memcpy(mPbuf_stream_v, bitstream, bufferSize);
        }
        dec_in.pStream = (uint8 *) mPbuf_stream_v;
        dec_in.pStream_phy = (uint32) mPbuf_stream_p;
        dec_in.dataLen = bufferSize;
        dec_in.beLastFrm = 0;
        dec_in.expected_IVOP = mNeedIVOP;
        dec_in.beDisplayed = 1;
        dec_in.err_pkt_num = 0;

        dec_out.frameEffective = 0;

        ALOGV("%s, %d, dec_in.dataLen: %d, mPicId: %d", __FUNCTION__, __LINE__, dec_in.dataLen, mPicId);

        outHeader->nTimeStamp = inHeader->nTimeStamp;
        outHeader->nFlags = inHeader->nFlags;

        unsigned int picPhyAddr = 0;
        if(!mDecoderSwFlag) {
            pBufCtrl= (BufferCtrlStruct*)(outHeader->pOutputPortPrivate);
            if(pBufCtrl->phyAddr != 0) {
                picPhyAddr = pBufCtrl->phyAddr;
            } else {
                if (mIOMMUEnabled) {
                    ALOGE("onQueueFilled, pBufCtrl->phyAddr == 0, fail");
                    notify(OMX_EventError, OMX_ErrorUndefined, 0, NULL);
                    mSignalledError = true;
                    return;
                } else {
                    native_handle_t *pNativeHandle = (native_handle_t *)outHeader->pBuffer;
                    struct private_handle_t *private_h = (struct private_handle_t *)pNativeHandle;
                    int bufferSize = 0;
                    MemoryHeapIon::Get_phy_addr_from_ion(private_h->share_fd,(int*)&picPhyAddr, &bufferSize);
                    pBufCtrl->phyAddr = picPhyAddr;
                }
            }
        }

        ALOGV("%s, %d, outHeader: 0x%x, pBuffer: 0x%x, phyAddr: 0x%x",__FUNCTION__, __LINE__, outHeader, outHeader->pBuffer, picPhyAddr);
        GraphicBufferMapper &mapper = GraphicBufferMapper::get();
        if(iUseAndroidNativeBuffer[OMX_DirOutput]) {
            OMX_PARAM_PORTDEFINITIONTYPE *def = &editPortInfo(OMX_DirOutput)->mDef;
            int width = def->format.video.nStride;
            int height = def->format.video.nSliceHeight;
            Rect bounds(width, height);
            void *vaddr;
            int usage;

            usage = GRALLOC_USAGE_SW_READ_OFTEN|GRALLOC_USAGE_SW_WRITE_OFTEN;

            if(mapper.lock((const native_handle_t*)outHeader->pBuffer, usage, bounds, &vaddr)) {
                ALOGE("onQueueFilled, mapper.lock fail %x",outHeader->pBuffer);
                return ;
            }
            ALOGV("%s, %d, pBuffer: 0x%x, vaddr: 0x%x", __FUNCTION__, __LINE__, outHeader->pBuffer,vaddr);
            uint8 *yuv = (uint8 *)(vaddr + outHeader->nOffset);
            ALOGV("%s, %d, yuv: %0x, mPicId: %d, outHeader: %0x, outHeader->pBuffer: %0x, outHeader->nTimeStamp: %lld",
                  __FUNCTION__, __LINE__, yuv, mPicId,outHeader, outHeader->pBuffer, outHeader->nTimeStamp);
            (*mH264Dec_SetCurRecPic)(mHandle, yuv, (uint8 *)picPhyAddr, (void *)outHeader, mPicId);
        } else {
            (*mH264Dec_SetCurRecPic)(mHandle, outHeader->pBuffer, (uint8 *)picPhyAddr, (void *)outHeader, mPicId);
        }

//        dump_bs( mPbuf_stream_v, dec_in.dataLen);

        int64_t start_decode = systemTime();
        MMDecRet decRet = (*mH264DecDecode)(mHandle, &dec_in,&dec_out);
        int64_t end_decode = systemTime();
        ALOGI("%s, %d, decRet: %d, %dms, dec_out.frameEffective: %d, needIVOP: %d", __FUNCTION__, __LINE__, decRet, (unsigned int)((end_decode-start_decode) / 1000000L), dec_out.frameEffective, mNeedIVOP);

        if(iUseAndroidNativeBuffer[OMX_DirOutput]) {
            if(mapper.unlock((const native_handle_t*)outHeader->pBuffer)) {
                ALOGE("onQueueFilled, mapper.unlock fail %x",outHeader->pBuffer);
            }
        }

        if( decRet == MMDEC_OK) {
            mNeedIVOP = false;
        } else {
            mNeedIVOP = true;
            if (decRet == MMDEC_MEMORY_ERROR) {
                ALOGE("failed to allocate memory.");
                notify(OMX_EventError, OMX_ErrorInsufficientResources, 0, NULL);
                mSignalledError = true;
                return;
            } else if (decRet == MMDEC_NOT_SUPPORTED) {
                ALOGE("failed to support this format.");
                notify(OMX_EventError, OMX_ErrorFormatNotDetected, 0, NULL);
                mSignalledError = true;
                return;
            } else if (decRet == MMDEC_STREAM_ERROR) {
                ALOGE("failed to decode video frame, stream error");
//                notify(OMX_EventError, OMX_ErrorStreamCorrupt, 0, NULL);
            } else if (decRet == MMDEC_HW_ERROR) {
                ALOGE("failed to decode video frame, hardware error");
//                notify(OMX_EventError, OMX_ErrorHardware, 0, NULL);
            } else {
                ALOGI("now, we don't take care of the decoder return: %d", decRet);
            }
        }

        H264SwDecInfo decoderInfo;
        MMDecRet ret;
        ret = (*mH264DecGetInfo)(mHandle, &decoderInfo);
        if(ret == MMDEC_OK) {
            if (!((decoderInfo.picWidth<= mMaxWidth&& decoderInfo.picHeight<= mMaxHeight)
                    || (decoderInfo.picWidth <= mMaxHeight && decoderInfo.picHeight <= mMaxWidth))) {
                ALOGE("[%d,%d] is out of range [%d, %d], failed to support this format.",
                      decoderInfo.picWidth, decoderInfo.picHeight, mMaxWidth, mMaxHeight);
                notify(OMX_EventError, OMX_ErrorFormatNotDetected, 0, NULL);
                mSignalledError = true;
                return;
            }

            if (handlePortSettingChangeEvent(&decoderInfo)) {
                return;
            } else if(mChangeToSwDec == true) {
                return;
            }

            if (decoderInfo.croppingFlag &&
                    handleCropRectEvent(&decoderInfo.cropParams)) {
                return;
            }
        } else {
            inInfo->mOwnedByUs = false;
            inQueue.erase(inQueue.begin());
            inInfo = NULL;
            notifyEmptyBufferDone(inHeader);
            inHeader = NULL;

            continue;
        }

	bufferSize = dec_in.dataLen;
        CHECK_LE(bufferSize, inHeader->nFilledLen);
        inHeader->nOffset += bufferSize;
        inHeader->nFilledLen -= bufferSize;

        if (inHeader->nFilledLen <= 0) {
            inHeader->nOffset = 0;
            inInfo->mOwnedByUs = false;
            inQueue.erase(inQueue.begin());
            inInfo = NULL;
            notifyEmptyBufferDone(inHeader);
            inHeader = NULL;
        }

        while (!outQueue.empty() &&
                mHeadersDecoded &&
                dec_out.frameEffective) {
            ALOGI("%s, %d, dec_out.pBufferHeader: %0x, dec_out.mPicId: %d", __FUNCTION__, __LINE__, dec_out.pBufferHeader, dec_out.mPicId);
            int32_t picId = dec_out.mPicId;//decodedPicture.picId;
            drainOneOutputBuffer(picId, dec_out.pBufferHeader);
            dec_out.frameEffective = false;
        }
    }
}

bool SPRDAVCDecoder::handlePortSettingChangeEvent(const H264SwDecInfo *info) {
//    ALOGI("%s, %d, mWidth: %d, mHeight: %d,  info->picWidth: %d,info->picHeight:%d, mPictureSize:%d ",
//                __FUNCTION__, __LINE__,mWidth, mHeight,  info->picWidth, info->picHeight, mPictureSize);

#if 0
    if(!mDecoderSwFlag) {
        ALOGI("%s, %d, picWidth: %d, picHeight: %d, numRef: %d, profile: 0x%x",
              __FUNCTION__, __LINE__,info->picWidth, info->picHeight, info->numRefFrames, info->profile);
        if ((!((info->picWidth <= 720 && info->picHeight <= 576) || (info->picWidth <= 576 && info->picHeight <= 720))) || (info->profile == 0x64) || (info->profile == 0x4d)) {
            mDecoderSwFlag = true;
            mChangeToSwDec = true;
        }
    }
#endif

    OMX_PARAM_PORTDEFINITIONTYPE *def = &editPortInfo(kOutputPortIndex)->mDef;
    if ((mWidth != info->picWidth) || (mHeight != info->picHeight) ||
            (info->numRefFrames > def->nBufferCountActual-(2+1+info->has_b_frames))) {
        ALOGI("%s, %d, mWidth: %d, mHeight: %d, info->picWidth: %d, info->picHeight: %d",
              __FUNCTION__, __LINE__,mWidth, mHeight, info->picWidth, info->picHeight);
        mWidth  = info->picWidth;
        mHeight = info->picHeight;
        mPictureSize = mWidth * mHeight * 3 / 2;
        mCropWidth = mWidth;
        mCropHeight = mHeight;
        change_ddr_freq();

        if (info->numRefFrames > def->nBufferCountActual-(2+1+info->has_b_frames)) {
            ALOGI("%s, %d, info->numRefFrames: %d, info->has_b_frames: %d, def->nBufferCountActual: %d", __FUNCTION__, __LINE__, info->numRefFrames, info->has_b_frames, def->nBufferCountActual);
            def->nBufferCountActual = info->numRefFrames + (2+1+info->has_b_frames);
        }

        updatePortDefinitions();
        (*mH264Dec_ReleaseRefBuffers)(mHandle);
        notify(OMX_EventPortSettingsChanged, kOutputPortIndex, 0, NULL);
        mOutputPortSettingsChange = AWAITING_DISABLED;
        return true;
    }

    return false;
}

bool SPRDAVCDecoder::handleCropRectEvent(const CropParams *crop) {
    if (mCropLeft != crop->cropLeftOffset ||
            mCropTop != crop->cropTopOffset ||
            mCropWidth != crop->cropOutWidth ||
            mCropHeight != crop->cropOutHeight) {
        mCropLeft = crop->cropLeftOffset;
        mCropTop = crop->cropTopOffset;
        mCropWidth = crop->cropOutWidth;
        mCropHeight = crop->cropOutHeight;

        notify(OMX_EventPortSettingsChanged, 1,
               OMX_IndexConfigCommonOutputCrop, NULL);

        return true;
    }
    return false;
}

void SPRDAVCDecoder::drainOneOutputBuffer(int32_t picId, void* pBufferHeader) {

    List<BufferInfo *> &outQueue = getPortQueue(kOutputPortIndex);

    List<BufferInfo *>::iterator it = outQueue.begin();
    while ((*it)->mHeader != (OMX_BUFFERHEADERTYPE*)pBufferHeader && it != outQueue.end()) {
        ++it;
    }
    CHECK((*it)->mHeader == (OMX_BUFFERHEADERTYPE*)pBufferHeader);

    BufferInfo *outInfo = *it;
    OMX_BUFFERHEADERTYPE *outHeader = outInfo->mHeader;

    outHeader->nFilledLen = mPictureSize;

    ALOGI("%s, %d, outHeader: %0x, outHeader->pBuffer: %0x, outHeader->nOffset: %d, outHeader->nFlags: %d, outHeader->nTimeStamp: %lld",
          __FUNCTION__, __LINE__, outHeader , outHeader->pBuffer, outHeader->nOffset, outHeader->nFlags, outHeader->nTimeStamp);

//    LOGI("%s, %d, outHeader->nTimeStamp: %d, outHeader->nFlags: %d, mPictureSize: %d", __FUNCTION__, __LINE__, outHeader->nTimeStamp, outHeader->nFlags, mPictureSize);
//   LOGI("%s, %d, out: %0x", __FUNCTION__, __LINE__, outHeader->pBuffer + outHeader->nOffset);

//    dump_yuv(data, mPictureSize);
    outInfo->mOwnedByUs = false;
    outQueue.erase(it);
    outInfo = NULL;

    BufferCtrlStruct* pOutBufCtrl= (BufferCtrlStruct*)(outHeader->pOutputPortPrivate);
    pOutBufCtrl->iRefCount++;
    notifyFillBufferDone(outHeader);
}

bool SPRDAVCDecoder::drainAllOutputBuffers() {
    ALOGI("%s, %d", __FUNCTION__, __LINE__);

    List<BufferInfo *> &outQueue = getPortQueue(kOutputPortIndex);
    BufferInfo *outInfo;
    OMX_BUFFERHEADERTYPE *outHeader;

    int32_t picId;
    void* pBufferHeader;

    while (!outQueue.empty() && mEOSStatus != OUTPUT_FRAMES_FLUSHED) {

        if (mHeadersDecoded &&
                MMDEC_OK == (*mH264Dec_GetLastDspFrm)(mHandle, &pBufferHeader, &picId) ) {
            List<BufferInfo *>::iterator it = outQueue.begin();
            while ((*it)->mHeader != (OMX_BUFFERHEADERTYPE*)pBufferHeader && it != outQueue.end()) {
                ++it;
            }
            CHECK((*it)->mHeader == (OMX_BUFFERHEADERTYPE*)pBufferHeader);
            outInfo = *it;
            outQueue.erase(it);
            outHeader = outInfo->mHeader;
            outHeader->nFilledLen = mPictureSize;
        } else {
            outInfo = *outQueue.begin();
            outQueue.erase(outQueue.begin());
            outHeader = outInfo->mHeader;
            outHeader->nTimeStamp = 0;
            outHeader->nFilledLen = 0;
            outHeader->nFlags = OMX_BUFFERFLAG_EOS;
            mEOSStatus = OUTPUT_FRAMES_FLUSHED;
        }

        outInfo->mOwnedByUs = false;
        BufferCtrlStruct* pOutBufCtrl= (BufferCtrlStruct*)(outHeader->pOutputPortPrivate);
        pOutBufCtrl->iRefCount++;
        notifyFillBufferDone(outHeader);
    }

    return true;
}

void SPRDAVCDecoder::onPortFlushCompleted(OMX_U32 portIndex) {
    if (portIndex == kInputPortIndex) {
        mEOSStatus = INPUT_DATA_AVAILABLE;
        mNeedIVOP = true;
    }
}

void SPRDAVCDecoder::onPortEnableCompleted(OMX_U32 portIndex, bool enabled) {
    switch (mOutputPortSettingsChange) {
    case NONE:
        break;

    case AWAITING_DISABLED:
    {
        CHECK(!enabled);
        mOutputPortSettingsChange = AWAITING_ENABLED;
        break;
    }

    default:
    {
        CHECK_EQ((int)mOutputPortSettingsChange, (int)AWAITING_ENABLED);
        CHECK(enabled);
        mOutputPortSettingsChange = NONE;
        break;
    }
    }
}

void SPRDAVCDecoder::onPortFlushPrepare(OMX_U32 portIndex) {
    if(portIndex == OMX_DirOutput) {
        (*mH264Dec_ReleaseRefBuffers)(mHandle);
    }
}

void SPRDAVCDecoder::updatePortDefinitions() {
    OMX_PARAM_PORTDEFINITIONTYPE *outDef = &editPortInfo(kOutputPortIndex)->mDef;
    outDef->format.video.nFrameWidth = mWidth;
    outDef->format.video.nFrameHeight = mHeight;
    outDef->format.video.nStride = outDef->format.video.nFrameWidth;
    outDef->format.video.nSliceHeight = outDef->format.video.nFrameHeight;

    outDef->nBufferSize =
        (outDef->format.video.nStride * outDef->format.video.nSliceHeight * 3) / 2;

    OMX_PARAM_PORTDEFINITIONTYPE *inDef = &editPortInfo(kInputPortIndex)->mDef;
    inDef->format.video.nFrameWidth = mWidth;
    inDef->format.video.nFrameHeight = mHeight;
    // input port is compressed, hence it has no stride
    inDef->format.video.nStride = 0;
    inDef->format.video.nSliceHeight = 0;
}


// static
int32_t SPRDAVCDecoder::ExtMemAllocWrapper(
    void* aUserData, unsigned int size_extra) {
    return static_cast<SPRDAVCDecoder *>(aUserData)->VSP_malloc_cb(size_extra);
}

// static
int32_t SPRDAVCDecoder::BindFrameWrapper(void *aUserData, void *pHeader) {
    return static_cast<SPRDAVCDecoder *>(aUserData)->VSP_bind_cb(pHeader);
}

// static
int32_t SPRDAVCDecoder::UnbindFrameWrapper(void *aUserData, void *pHeader) {
    return static_cast<SPRDAVCDecoder *>(aUserData)->VSP_unbind_cb(pHeader);
}

int SPRDAVCDecoder::VSP_malloc_cb(unsigned int size_extra) {

    ALOGI("%s, %d, mDecoderSwFlag: %d, mPictureSize: %d, size_extra: %d", __FUNCTION__, __LINE__, mDecoderSwFlag, mPictureSize, size_extra);
    MMCodecBuffer extra_mem[MAX_MEM_TYPE];

    if (mDecoderSwFlag) {
        if (mCodecExtraBuffer != NULL) {
            free(mCodecExtraBuffer);
            mCodecExtraBuffer = NULL;
        }
        mCodecExtraBuffer = (uint8 *)malloc(size_extra);

        extra_mem[SW_CACHABLE].common_buffer_ptr = mCodecExtraBuffer;
        extra_mem[SW_CACHABLE].common_buffer_ptr_phy = 0;
        extra_mem[SW_CACHABLE].size = size_extra;
    } else {


        if (mIOMMUEnabled) {
            mPmem_extra = new MemoryHeapIon(SPRD_ION_DEV, size_extra, MemoryHeapBase::NO_CACHING, ION_HEAP_ID_MASK_SYSTEM);
        } else {
            mPmem_extra = new MemoryHeapIon(SPRD_ION_DEV, size_extra, MemoryHeapBase::NO_CACHING, ION_HEAP_ID_MASK_MM);
        }
        int fd = mPmem_extra->getHeapID();
        if(fd >= 0) {
            int ret,phy_addr, buffer_size;

            if (mIOMMUEnabled) {
                ret = mPmem_extra->get_mm_iova(&phy_addr, &buffer_size);
            } else {
                ret = mPmem_extra->get_phy_addr_from_ion(&phy_addr, &buffer_size);
            }
            if(ret < 0) {
                ALOGE ("mPmem_extra: get phy addr fail %d",ret);
                return -1;
            }

            mPbuf_extra_p =phy_addr;
            mPbuf_extra_size = buffer_size;
            mPbuf_extra_v = (uint8 *)mPmem_extra->base();
            ALOGI("pmem %p - %p - %d", mPbuf_extra_p, mPbuf_extra_v, mPbuf_extra_size);

            extra_mem[HW_NO_CACHABLE].common_buffer_ptr =(uint8 *) mPbuf_extra_v;
            extra_mem[HW_NO_CACHABLE].common_buffer_ptr_phy = (uint32)mPbuf_extra_p;
            extra_mem[HW_NO_CACHABLE].size = size_extra;
        } else {
            ALOGE ("mPmem_extra: getHeapID fail %d", fd);
            return -1;
        }
    }

    (*mH264DecMemInit)(((SPRDAVCDecoder *)this)->mHandle, extra_mem);

    mHeadersDecoded = true;

    return 0;
}

int SPRDAVCDecoder::VSP_bind_cb(void *pHeader) {
    BufferCtrlStruct *pBufCtrl = (BufferCtrlStruct *)(((OMX_BUFFERHEADERTYPE *)pHeader)->pOutputPortPrivate);
    ALOGI("VSP_bind_cb, ref frame: 0x%x, %x; iRefCount=%d",
          ((OMX_BUFFERHEADERTYPE *)pHeader)->pBuffer, pHeader,pBufCtrl->iRefCount);
    pBufCtrl->iRefCount++;
    return 0;
}

int SPRDAVCDecoder::VSP_unbind_cb(void *pHeader) {
    BufferCtrlStruct *pBufCtrl = (BufferCtrlStruct *)(((OMX_BUFFERHEADERTYPE *)pHeader)->pOutputPortPrivate);

    ALOGI("VSP_unbind_cb, ref frame: 0x%x, %x; iRefCount=%d",
          ((OMX_BUFFERHEADERTYPE *)pHeader)->pBuffer, pHeader,pBufCtrl->iRefCount);

    if (pBufCtrl->iRefCount  > 0) {
        pBufCtrl->iRefCount--;
    }

    return 0;
}

OMX_ERRORTYPE SPRDAVCDecoder::getExtensionIndex(
    const char *name, OMX_INDEXTYPE *index) {

    ALOGI("getExtensionIndex, name: %s",name);
    if(strcmp(name, SPRD_INDEX_PARAM_ENABLE_ANB) == 0) {
        ALOGI("getExtensionIndex:%s",SPRD_INDEX_PARAM_ENABLE_ANB);
        *index = (OMX_INDEXTYPE) OMX_IndexParamEnableAndroidBuffers;
        return OMX_ErrorNone;
    } else if (strcmp(name, SPRD_INDEX_PARAM_GET_ANB) == 0) {
        ALOGI("getExtensionIndex:%s",SPRD_INDEX_PARAM_GET_ANB);
        *index = (OMX_INDEXTYPE) OMX_IndexParamGetAndroidNativeBuffer;
        return OMX_ErrorNone;
    }	else if (strcmp(name, SPRD_INDEX_PARAM_USE_ANB) == 0) {
        ALOGI("getExtensionIndex:%s",SPRD_INDEX_PARAM_USE_ANB);
        *index = OMX_IndexParamUseAndroidNativeBuffer2;
        return OMX_ErrorNone;
    }

    return OMX_ErrorNotImplemented;
}

bool SPRDAVCDecoder::openDecoder(const char* libName) {
    if(mLibHandle) {
        dlclose(mLibHandle);
    }

    ALOGI("openDecoder, lib: %s", libName);

    mLibHandle = dlopen(libName, RTLD_NOW);
    if(mLibHandle == NULL) {
        ALOGE("openDecoder, can't open lib: %s",libName);
        return false;
    }

    mH264DecGetNALType = (FT_H264DecGetNALType)dlsym(mLibHandle, "H264DecGetNALType");
    if(mH264DecGetNALType == NULL) {
        ALOGE("Can't find H264DecGetNALType in %s",libName);
        dlclose(mLibHandle);
        mLibHandle = NULL;
        return false;
    }

    mH264GetCodecCapability = (FT_H264GetCodecCapability)dlsym(mLibHandle, "H264GetCodecCapability");
    if(mH264GetCodecCapability == NULL) {
        ALOGE("Can't find H264GetCodecCapability in %s",libName);
        dlclose(mLibHandle);
        mLibHandle = NULL;
        return false;
    }

    mH264DecGetInfo = (FT_H264DecGetInfo)dlsym(mLibHandle, "H264DecGetInfo");
    if(mH264DecGetInfo == NULL) {
        ALOGE("Can't find H264DecGetInfo in %s",libName);
        dlclose(mLibHandle);
        mLibHandle = NULL;
        return false;
    }

    mH264DecInit = (FT_H264DecInit)dlsym(mLibHandle, "H264DecInit");
    if(mH264DecInit == NULL) {
        ALOGE("Can't find H264DecInit in %s",libName);
        dlclose(mLibHandle);
        mLibHandle = NULL;
        return false;
    }

    mH264DecDecode = (FT_H264DecDecode)dlsym(mLibHandle, "H264DecDecode");
    if(mH264DecDecode == NULL) {
        ALOGE("Can't find H264DecDecode in %s",libName);
        dlclose(mLibHandle);
        mLibHandle = NULL;
        return false;
    }

    mH264DecRelease = (FT_H264DecRelease)dlsym(mLibHandle, "H264DecRelease");
    if(mH264DecRelease == NULL) {
        ALOGE("Can't find H264DecRelease in %s",libName);
        dlclose(mLibHandle);
        mLibHandle = NULL;
        return false;
    }

    mH264Dec_SetCurRecPic = (FT_H264Dec_SetCurRecPic)dlsym(mLibHandle, "H264Dec_SetCurRecPic");
    if(mH264Dec_SetCurRecPic == NULL) {
        ALOGE("Can't find H264Dec_SetCurRecPic in %s",libName);
        dlclose(mLibHandle);
        mLibHandle = NULL;
        return false;
    }

    mH264Dec_GetLastDspFrm = (FT_H264Dec_GetLastDspFrm)dlsym(mLibHandle, "H264Dec_GetLastDspFrm");
    if(mH264Dec_GetLastDspFrm == NULL) {
        ALOGE("Can't find H264Dec_GetLastDspFrm in %s",libName);
        dlclose(mLibHandle);
        mLibHandle = NULL;
        return false;
    }

    mH264Dec_ReleaseRefBuffers = (FT_H264Dec_ReleaseRefBuffers)dlsym(mLibHandle, "H264Dec_ReleaseRefBuffers");
    if(mH264Dec_ReleaseRefBuffers == NULL) {
        ALOGE("Can't find H264Dec_ReleaseRefBuffers in %s",libName);
        dlclose(mLibHandle);
        mLibHandle = NULL;
        return false;
    }

    mH264DecMemInit = (FT_H264DecMemInit)dlsym(mLibHandle, "H264DecMemInit");
    if(mH264DecMemInit == NULL) {
        ALOGE("Can't find H264DecMemInit in %s",libName);
        dlclose(mLibHandle);
        mLibHandle = NULL;
        return false;
    }

    return true;
}

}  // namespace android

android::SprdOMXComponent *createSprdOMXComponent(
    const char *name, const OMX_CALLBACKTYPE *callbacks,
    OMX_PTR appData, OMX_COMPONENTTYPE **component) {
    return new android::SPRDAVCDecoder(name, callbacks, appData, component);
}
