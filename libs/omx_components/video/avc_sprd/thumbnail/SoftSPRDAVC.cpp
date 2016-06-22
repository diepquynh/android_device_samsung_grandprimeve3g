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

//#define LOG_NDEBUG 0
#define LOG_TAG "SoftSPRDAVC"
#include <utils/Log.h>

#include "SoftSPRDAVC.h"

#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/MediaErrors.h>
#include <media/IOMX.h>

#include <dlfcn.h>

#include "avc_dec_api.h"

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

SoftSPRDAVC::SoftSPRDAVC(
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
      mPicId(0),
      mHeadersDecoded(false),
      mEOSStatus(INPUT_DATA_AVAILABLE),
      mOutputPortSettingsChange(NONE),
      mSignalledError(false),
      mStopDecode(false),
      mLibHandle(NULL),
      mNeedIVOP(true),
      mStreamBuffer(NULL),
      mCodecInterBuffer(NULL),
      mCodecExtraBuffer(NULL),
      mH264DecInit(NULL),
      mH264DecGetInfo(NULL),
      mH264DecDecode(NULL),
      mH264DecRelease(NULL),
      mH264Dec_SetCurRecPic(NULL),
      mH264Dec_GetLastDspFrm(NULL),
      mH264Dec_ReleaseRefBuffers(NULL),
      mH264DecMemInit(NULL) {
    CHECK_EQ(openDecoder("libomx_avcdec_sw_sprd.so"), true);
    initPorts();
    CHECK_EQ(initDecoder(), (status_t)OK);
}

SoftSPRDAVC::~SoftSPRDAVC() {
    releaseDecoder();

    delete mHandle;
    mHandle = NULL;

    List<BufferInfo *> &outQueue = getPortQueue(kOutputPortIndex);
    List<BufferInfo *> &inQueue = getPortQueue(kInputPortIndex);
    CHECK(outQueue.empty());
    CHECK(inQueue.empty());
}

void SoftSPRDAVC::initPorts() {
    OMX_PARAM_PORTDEFINITIONTYPE def;
    InitOMXParams(&def);

    def.nPortIndex = kInputPortIndex;
    def.eDir = OMX_DirInput;
    def.nBufferCountMin = kNumInputBuffers;
    def.nBufferCountActual = def.nBufferCountMin;
    def.nBufferSize = 8192;
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
    def.nBufferCountMin = kNumOutputBuffers;
    def.nBufferCountActual = def.nBufferCountMin;
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
    def.format.video.eColorFormat = OMX_COLOR_FormatYUV420Planar;
    def.format.video.pNativeWindow = NULL;

    def.nBufferSize =
        (def.format.video.nFrameWidth * def.format.video.nFrameHeight * 3) / 2;

    addPort(def);
}

status_t SoftSPRDAVC::initDecoder() {
    memset(mHandle, 0, sizeof(tagAVCHandle));

    mHandle->userdata = (void *)this;
    mHandle->VSP_bindCb = NULL;
    mHandle->VSP_unbindCb = NULL;
    mHandle->VSP_extMemCb = ExtMemAllocWrapper;

    mStreamBuffer = (uint8 *)malloc(H264_DECODER_STREAM_BUFFER_SIZE);

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
    video_format.uv_interleaved = 0;

    if ((*mH264DecInit)(mHandle, &codec_buf,&video_format) != MMDEC_OK)
    {
        ALOGE("Failed to init AVCDEC");
        return OMX_ErrorUndefined;
    }

    return OMX_ErrorNone;
}

void SoftSPRDAVC::releaseDecoder()
{
    (*mH264DecRelease)(mHandle);

    if (mStreamBuffer != NULL)
    {
        free(mStreamBuffer);
        mStreamBuffer = NULL;
    }

    if (mCodecInterBuffer != NULL)
    {
        free(mCodecInterBuffer);
        mCodecInterBuffer = NULL;
    }

    if (mCodecExtraBuffer != NULL)
    {
        free(mCodecExtraBuffer);
        mCodecExtraBuffer = NULL;
    }

    if(mLibHandle)
    {
        dlclose(mLibHandle);
        mLibHandle = NULL;
    }
}

OMX_ERRORTYPE SoftSPRDAVC::internalGetParameter(
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

            formatParams->eCompressionFormat = OMX_VIDEO_CodingUnused;
            formatParams->eColorFormat = OMX_COLOR_FormatYUV420Planar;
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

    default:
        return SprdSimpleOMXComponent::internalGetParameter(index, params);
    }
}

OMX_ERRORTYPE SoftSPRDAVC::internalSetParameter(
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
        }

        return OMX_ErrorNone;
    }

    default:
        return SprdSimpleOMXComponent::internalSetParameter(index, params);
    }
}

OMX_ERRORTYPE SoftSPRDAVC::getConfig(
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

void SoftSPRDAVC::onQueueFilled(OMX_U32 portIndex) {
    if (mSignalledError || mOutputPortSettingsChange != NONE) {
        return;
    }

    if (mEOSStatus == OUTPUT_FRAMES_FLUSHED) {
        return;
    }

    List<BufferInfo *> &inQueue = getPortQueue(kInputPortIndex);
    List<BufferInfo *> &outQueue = getPortQueue(kOutputPortIndex);

    while (!mStopDecode && (mEOSStatus != INPUT_DATA_AVAILABLE || !inQueue.empty())
            && outQueue.size() != 0) {

        if (mEOSStatus == INPUT_EOS_SEEN) {
            drainAllOutputBuffers();
            return;
        }

        BufferInfo *inInfo = *inQueue.begin();
        OMX_BUFFERHEADERTYPE *inHeader = inInfo->mHeader;

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

        int32 add_startcode_len = 0;

        dec_in.pStream = (uint8 *) mStreamBuffer;
        dec_in.pStream_phy = 0;
        dec_in.dataLen = inHeader->nFilledLen;
        int32_t bufferSize = dec_in.dataLen;

//       if (!memcmp((uint8 *)(inHeader->pBuffer + inHeader->nOffset), "\x00\x00\x00\x01", 4))
        uint8 *p = (uint8 *)(inHeader->pBuffer + inHeader->nOffset);

        if((p[0] != 0x0) || (p[1] != 0x0) || (p[2] != 0x0) || (p[3] != 0x1))
        {
            ALOGI("%s, %d, p[0]: %x, p[1]: %x, p[2]: %x, p[3]: %x", __FUNCTION__, __LINE__, p[0], p[1], p[2], p[3]);

            ((uint8 *) mStreamBuffer)[0] = 0x0;
            ((uint8 *) mStreamBuffer)[1] = 0x0;
            ((uint8 *) mStreamBuffer)[2] = 0x0;
            ((uint8 *) mStreamBuffer)[3] = 0x1;

            add_startcode_len = 4;
            dec_in.dataLen += add_startcode_len;
        }
        memcpy((void *)mStreamBuffer+add_startcode_len, inHeader->pBuffer + inHeader->nOffset, inHeader->nFilledLen);

        dec_in.beLastFrm = 0;
        dec_in.expected_IVOP = mNeedIVOP;
        dec_in.beDisplayed = 1;
        dec_in.err_pkt_num = 0;

        dec_out.frameEffective = 0;

        BufferInfo *outInfo = *outQueue.begin();
        OMX_BUFFERHEADERTYPE *outHeader = outInfo->mHeader;
        outHeader->nTimeStamp = inHeader->nTimeStamp;
        outHeader->nFlags = inHeader->nFlags;
        outHeader->nFilledLen = mPictureSize;

        ALOGI("%s, %d, outHeader:0x%x, inHeader: 0x%x, len: %d, nOffset: %d, time: %lld, EOS: %d",
              __FUNCTION__, __LINE__,outHeader,inHeader, inHeader->nFilledLen,inHeader->nOffset, inHeader->nTimeStamp,inHeader->nFlags & OMX_BUFFERFLAG_EOS);

        uint8 *yuv = (uint8 *)(outHeader->pBuffer + outHeader->nOffset);
//        ALOGI("%s, %d, yuv: %0x, mPicId: %d, outHeader->pBuffer: %0x, outHeader->nOffset: %d, outHeader->nFlags: %d, outHeader->nTimeStamp: %lld",
//              __FUNCTION__, __LINE__, yuv, mPicId,outHeader->pBuffer, outHeader->nOffset, outHeader->nFlags, outHeader->nTimeStamp);
        (*mH264Dec_SetCurRecPic)(mHandle, yuv, NULL, (void *)outHeader, mPicId);

#if 0
        dump_bs( dec_in.pStream, dec_in.dataLen);
#endif

        int64_t start_decode = systemTime();
        MMDecRet decRet = (*mH264DecDecode)(mHandle, &dec_in,&dec_out);
        int64_t end_decode = systemTime();
        ALOGI("%s, %d, decRet: %d, %dms, dec_out.frameEffective: %d, needIVOP: %d", __FUNCTION__, __LINE__, decRet, (unsigned int)((end_decode-start_decode) / 1000000L), dec_out.frameEffective, mNeedIVOP);

        if( decRet == MMDEC_OK) {
            mNeedIVOP = false;
        } else if (decRet == MMDEC_NOT_SUPPORTED)
        {
            ALOGE("failed to support this format.");
            notify(OMX_EventError, OMX_ErrorFormatNotDetected, 0, NULL);
        } else if (decRet == MMDEC_MEMORY_ERROR)
        {
            ALOGE("failed to allocate memory.");
            notify(OMX_EventError, OMX_ErrorInsufficientResources, 0, NULL);
        } else
        {
            ALOGI("now, we don't take care of the decoder return: %d", decRet);
        }

        H264SwDecInfo decoderInfo;
        MMDecRet ret;
        ret = (*mH264DecGetInfo)(mHandle, &decoderInfo);
        if(ret == MMDEC_OK) {
            if (!((decoderInfo.picWidth<= 1920&& decoderInfo.picHeight<= 1088)
                    || (decoderInfo.picWidth <= 1088 && decoderInfo.picHeight <= 1920))) {
                ALOGE("[%d,%d] is out of range [1920, 1088], failed to support this format.",
                      decoderInfo.picWidth, decoderInfo.picHeight);
                notify(OMX_EventError, OMX_ErrorFormatNotDetected, 0, NULL);
                mSignalledError = true;
                return;
            }

            if (handlePortSettingChangeEvent(&decoderInfo)) {
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

        CHECK_LE(bufferSize, inHeader->nFilledLen);

        ALOGI("%s, %d, bufferSize: %d, inHeader->nFilledLen: %d", __FUNCTION__, __LINE__, bufferSize, inHeader->nFilledLen);
        inHeader->nOffset += bufferSize;
        inHeader->nFilledLen -= bufferSize;

        if (inHeader->nFilledLen == 0) {
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
            mStopDecode = true;
        }
    }
}

bool SoftSPRDAVC::handlePortSettingChangeEvent(const H264SwDecInfo *info) {
    if (mWidth != info->picWidth || mHeight != info->picHeight) {
        mWidth  = info->picWidth;
        mHeight = info->picHeight;
        mPictureSize = mWidth * mHeight * 3 / 2;
        ALOGI("%s, %d, mWidth: %d, mHeight: %d, mPictureSize: %d", __FUNCTION__, __LINE__,mWidth, mHeight, mPictureSize);
        mCropWidth = mWidth;
        mCropHeight = mHeight;
        updatePortDefinitions();
        notify(OMX_EventPortSettingsChanged, 1, 0, NULL);
        mOutputPortSettingsChange = AWAITING_DISABLED;
        return true;
    }

    return false;
}

bool SoftSPRDAVC::handleCropRectEvent(const CropParams *crop) {
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

void SoftSPRDAVC::drainOneOutputBuffer(int32_t picId, void* pBufferHeader) {

    List<BufferInfo *> &outQueue = getPortQueue(kOutputPortIndex);

    List<BufferInfo *>::iterator it = outQueue.begin();
    while ((*it)->mHeader != (OMX_BUFFERHEADERTYPE*)pBufferHeader) {
        ++it;
    }

    BufferInfo *outInfo = *it;
    OMX_BUFFERHEADERTYPE *outHeader = outInfo->mHeader;

    outHeader->nFilledLen = mPictureSize;

    ALOGV("%s, %d, outHeader: %0x, outHeader->pBuffer: %0x, outHeader->nOffset: %d, outHeader->nFlags: %d, outHeader->nTimeStamp: %lld",
          __FUNCTION__, __LINE__, outHeader , outHeader->pBuffer, outHeader->nOffset, outHeader->nFlags, outHeader->nTimeStamp);

#if 0
    dump_yuv(data, mPictureSize);
#endif
    outInfo->mOwnedByUs = false;
    outQueue.erase(it);
    outInfo = NULL;

    notifyFillBufferDone(outHeader);
}

bool SoftSPRDAVC::drainAllOutputBuffers() {
    ALOGI("%s, %d", __FUNCTION__, __LINE__);

    List<BufferInfo *> &outQueue = getPortQueue(kOutputPortIndex);
    int32_t picId;
    void* pBufferHeader;

    while (!outQueue.empty()) {
        BufferInfo *outInfo = *outQueue.begin();
        outQueue.erase(outQueue.begin());
        OMX_BUFFERHEADERTYPE *outHeader = outInfo->mHeader;
        if (mHeadersDecoded &&
                MMDEC_OK == (*mH264Dec_GetLastDspFrm)(mHandle, &pBufferHeader, &picId) ) {
            outHeader->nFilledLen = mPictureSize;
        } else {
            outHeader->nTimeStamp = 0;
            outHeader->nFilledLen = 0;
            outHeader->nFlags = OMX_BUFFERFLAG_EOS;
            mEOSStatus = OUTPUT_FRAMES_FLUSHED;
        }

        outInfo->mOwnedByUs = false;
        notifyFillBufferDone(outHeader);
    }
    return true;
}

void SoftSPRDAVC::onPortFlushCompleted(OMX_U32 portIndex) {
    if (portIndex == kInputPortIndex) {
        mEOSStatus = INPUT_DATA_AVAILABLE;
        mNeedIVOP = true;
    }
}

void SoftSPRDAVC::onPortEnableCompleted(OMX_U32 portIndex, bool enabled) {
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

void SoftSPRDAVC::updatePortDefinitions() {
    OMX_PARAM_PORTDEFINITIONTYPE *def = &editPortInfo(0)->mDef;
    def->format.video.nFrameWidth = mWidth;
    def->format.video.nFrameHeight = mHeight;
    def->format.video.nStride = def->format.video.nFrameWidth;
    def->format.video.nSliceHeight = def->format.video.nFrameHeight;

    def = &editPortInfo(1)->mDef;
    def->format.video.nFrameWidth = mWidth;
    def->format.video.nFrameHeight = mHeight;
    def->format.video.nStride = def->format.video.nFrameWidth;
    def->format.video.nSliceHeight = def->format.video.nFrameHeight;

    def->nBufferSize =
        (def->format.video.nFrameWidth
         * def->format.video.nFrameHeight * 3) / 2;
}

// static
int32_t SoftSPRDAVC::ExtMemAllocWrapper(
    void *aUserData, unsigned int size_extra) {
    return static_cast<SoftSPRDAVC *>(aUserData)->VSP_malloc_cb(size_extra);
}

int SoftSPRDAVC::VSP_malloc_cb(unsigned int size_extra) {
    MMCodecBuffer ExtraBuffer[MAX_MEM_TYPE];

    if (mCodecExtraBuffer != NULL)
    {
        free(mCodecExtraBuffer);
        mCodecExtraBuffer = NULL;
    }
    mCodecExtraBuffer = (uint8 *)malloc(size_extra);

    ALOGI("%s, %d, mPictureSize: %d, size_extra: %d, mCodecExtraBuffer: %0x",
          __FUNCTION__, __LINE__, mPictureSize, size_extra, mCodecExtraBuffer);

    if (mCodecExtraBuffer == NULL)
    {
        return -1;
    }

    ExtraBuffer[SW_CACHABLE].common_buffer_ptr = mCodecExtraBuffer;
    ExtraBuffer[SW_CACHABLE].size = size_extra;

    (*mH264DecMemInit)(((SoftSPRDAVC *)this)->mHandle, ExtraBuffer);

    mHeadersDecoded = true;

    return 0;
}


bool SoftSPRDAVC::openDecoder(const char* libName)
{
    if(mLibHandle) {
        dlclose(mLibHandle);
    }

    ALOGI("openDecoder, lib: %s",libName);

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
    return new android::SoftSPRDAVC(name, callbacks, appData, component);
}
