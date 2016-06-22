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
#define LOG_TAG "SPRDAVCEncoder"
#include <utils/Log.h>

#include "avc_enc_api.h"

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

#include "SPRDAVCEncoder.h"
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

typedef struct LevelConversion {
    OMX_U32 omxLevel;
    AVCLevel avcLevel;
} LevelConcersion;

static LevelConversion ConversionTable[] = {
    { OMX_VIDEO_AVCLevel1,  AVC_LEVEL1_B },
    { OMX_VIDEO_AVCLevel1b, AVC_LEVEL1   },
    { OMX_VIDEO_AVCLevel11, AVC_LEVEL1_1 },
    { OMX_VIDEO_AVCLevel12, AVC_LEVEL1_2 },
    { OMX_VIDEO_AVCLevel13, AVC_LEVEL1_3 },
    { OMX_VIDEO_AVCLevel2,  AVC_LEVEL2 },
#if 1
    // encoding speed is very poor if video
    // resolution is higher than CIF
    { OMX_VIDEO_AVCLevel21, AVC_LEVEL2_1 },
    { OMX_VIDEO_AVCLevel22, AVC_LEVEL2_2 },
    { OMX_VIDEO_AVCLevel3,  AVC_LEVEL3   },
    { OMX_VIDEO_AVCLevel31, AVC_LEVEL3_1 },
    { OMX_VIDEO_AVCLevel32, AVC_LEVEL3_2 },
    { OMX_VIDEO_AVCLevel4,  AVC_LEVEL4   },
    { OMX_VIDEO_AVCLevel41, AVC_LEVEL4_1 },
    { OMX_VIDEO_AVCLevel42, AVC_LEVEL4_2 },
    { OMX_VIDEO_AVCLevel5,  AVC_LEVEL5   },
    { OMX_VIDEO_AVCLevel51, AVC_LEVEL5_1 },
#endif
};

static status_t ConvertOmxAvcLevelToAvcSpecLevel(
    OMX_U32 omxLevel, AVCLevel *avcLevel) {
    for (size_t i = 0, n = sizeof(ConversionTable)/sizeof(ConversionTable[0]);
            i < n; ++i) {
        if (omxLevel == ConversionTable[i].omxLevel) {
            *avcLevel = ConversionTable[i].avcLevel;
            return OK;
        }
    }

    ALOGE("ConvertOmxAvcLevelToAvcSpecLevel: %d level not supported",
          (int32_t)omxLevel);

    return BAD_VALUE;
}

static status_t ConvertAvcSpecLevelToOmxAvcLevel(
    AVCLevel avcLevel, OMX_U32 *omxLevel) {
    for (size_t i = 0, n = sizeof(ConversionTable)/sizeof(ConversionTable[0]);
            i < n; ++i) {
        if (avcLevel == ConversionTable[i].avcLevel) {
            *omxLevel = ConversionTable[i].omxLevel;
            return OK;
        }
    }

    ALOGE("ConvertAvcSpecLevelToOmxAvcLevel: %d level not supported",
          (int32_t) avcLevel);

    return BAD_VALUE;
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
inline static void ConvertYUV420PlanarToYUV420SemiPlanar(uint8_t *inyuv, uint8_t* outyuv,
        int32_t width_org, int32_t height_org, int32_t width_dst, int32_t height_dst) {

    int32_t inYsize = width_org * height_org;
    uint32_t *outy =  (uint32_t *) outyuv;
    uint16_t *incb = (uint16_t *) (inyuv + inYsize);
    uint16_t *incr = (uint16_t *) (inyuv + inYsize + (inYsize >> 2));

    /* Y copying */
    memcpy(outy, inyuv, inYsize);

    /* U & V copying, Make sure uv data is in their right position*/
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



static int RGB_r_y[256];
static int RGB_r_cb[256];
static int RGB_r_cr_b_cb[256];
static int RGB_g_y[256];
static int RGB_g_cb[256];
static int RGB_g_cr[256];
static int RGB_b_y[256];
static int RGB_b_cr[256];
static  bool mConventFlag = false;

//init the convert table, the Transformation matrix is as:
// Y  =  ((66 * (_r)  + 129 * (_g)  + 25    * (_b)) >> 8) + 16
// Cb = ((-38 * (_r) - 74   * (_g)  + 112  * (_b)) >> 8) + 128
// Cr =  ((112 * (_r) - 94   * (_g)  - 18    * (_b)) >> 8) + 128
inline static void inittable()
{
    ALOGI("init table");
    int i = 0;
    for(i = 0; i < 256; i++) {
        RGB_r_y[i] =  ((66 * i) >> 8);
        RGB_r_cb[i] = ((38 * i) >> 8);
        RGB_r_cr_b_cb[i] = ((112 * i) >> 8 );
        RGB_g_y[i] = ((129 * i) >> 8) + 16 ;
        RGB_g_cb[i] = ((74 * i) >> 8) + 128 ;
        RGB_g_cr[i] = ((94 * i) >> 8) + 128;
        RGB_b_y[i] =  ((25 * i) >> 8);
        RGB_b_cr[i] = ((18 * i) >> 8);
    }
}
inline static void ConvertARGB888ToYUV420SemiPlanar(uint8_t *inrgb, uint8_t* outyuv,
        int32_t width_org, int32_t height_org, int32_t width_dst, int32_t height_dst) {
#define RGB2Y(_r, _g, _b)    (  *(RGB_r_y +_r)      +   *(RGB_g_y+_g)   +    *(RGB_b_y+_b))
#define RGB2CB(_r, _g, _b)   ( -*(RGB_r_cb +_r)     -   *(RGB_g_cb+_g)  +    *(RGB_r_cr_b_cb+_b))
#define RGB2CR(_r, _g, _b)   (  *(RGB_r_cr_b_cb +_r)-   *(RGB_g_cr+_g)  -    *(RGB_b_cr+_b))
    uint8_t *argb_ptr = inrgb;
    uint8_t *y_p = outyuv;
    uint8_t *vu_p = outyuv + width_dst * height_dst;

    if (NULL == inrgb || NULL ==  outyuv)
        return;
    if (0 != (width_org & 1) || 0 != (height_org & 1))
        return;
    if(!mConventFlag) {
        mConventFlag = true;
        inittable();
    }
    ALOGI("rgb2yuv start");
    uint8_t *y_ptr;
    uint8_t *vu_ptr;
    int64_t start_encode = systemTime();
    uint32 i ;
    uint32 j = height_org + 1;
    while(--j) {
        //the width_dst may be bigger than width_org,
        //make start byte in every line of Y and CbCr align
        y_ptr = y_p;
        y_p += width_dst;
        if (!(j & 1))  {
            vu_ptr = vu_p;
            vu_p += width_dst;
            i  = width_org / 2 + 1;
            while(--i) {
                //format abgr, litter endian
                *y_ptr++    = RGB2Y(*argb_ptr, *(argb_ptr+1), *(argb_ptr+2));
                *vu_ptr++ =  RGB2CR(*argb_ptr, *(argb_ptr+1), *(argb_ptr+2));
                *vu_ptr++  = RGB2CB(*argb_ptr, *(argb_ptr+1), *(argb_ptr+2));
                *y_ptr++    = RGB2Y(*(argb_ptr + 4), *(argb_ptr+5), *(argb_ptr+6));
                argb_ptr += 8;
            }
        } else {
            i  = width_org + 1;
            while(--i) {
                //format abgr, litter endian
                *y_ptr++ = RGB2Y(*argb_ptr, *(argb_ptr+1), *(argb_ptr+2));
                argb_ptr += 4;
            }
        }
    }
    int64_t end_encode = systemTime();
    ALOGI("rgb2yuv time: %d",(unsigned int)((end_encode-start_encode) / 1000000L));
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

SPRDAVCEncoder::SPRDAVCEncoder(
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
      mAVCEncProfile(AVC_BASELINE),
      mAVCEncLevel(AVC_LEVEL2),
      mNumInputFrames(-1),
      mPrevTimestampUs(-1),
      mStarted(false),
      mSpsPpsHeaderReceived(false),
      mReadyForNextFrame(true),
      mSawInputEOS(false),
      mSignalledError(false),
      mStoreMetaData(OMX_FALSE),
      mIOMMUEnabled(false),
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
      mHandle(new tagAVCHandle),
      mEncConfig(new MMEncConfig),
      mEncParams(new tagAVCEncParam),
      mSliceGroup(NULL),
      mSetFreqCount(0),
      mLibHandle(NULL),
      mH264EncGetCodecCapability(NULL),
      mH264EncPreInit(NULL),
      mH264EncInit(NULL),
      mH264EncSetConf(NULL),
      mH264EncGetConf(NULL),
      mH264EncStrmEncode(NULL),
      mH264EncGenHeader(NULL),
      mH264EncRelease(NULL) {

    ALOGI("Construct SPRDAVCEncoder, this: %0x", (void *)this);

    CHECK(mHandle != NULL);
    memset(mHandle, 0, sizeof(tagAVCHandle));

    mHandle->videoEncoderData = NULL;
    mHandle->userData = this;

    memset(&mEncInfo, 0, sizeof(mEncInfo));

    CHECK_EQ(openEncoder("libomx_avcenc_hw_sprd.so"), true);

    ALOGI("%s, %d, name: %s", __FUNCTION__, __LINE__, name);

    mIOMMUEnabled = MemoryHeapIon::Mm_iommu_is_enabled();
    ALOGI("%s, is IOMMU enabled: %d", __FUNCTION__, mIOMMUEnabled);

    MMCodecBuffer InterMemBfr;
    int32 size_inter = H264ENC_INTERNAL_BUFFER_SIZE;

    mPbuf_inter = (uint8 *)malloc(size_inter);
    InterMemBfr.common_buffer_ptr = (uint8 *)mPbuf_inter;
    InterMemBfr.common_buffer_ptr_phy= 0;
    InterMemBfr.size = size_inter;

    CHECK_EQ((*mH264EncPreInit)(mHandle, &InterMemBfr), MMENC_OK);

    CHECK_EQ ((*mH264EncGetCodecCapability)(mHandle, &mCapability), MMENC_OK);

    initPorts();
    ALOGI("Construct SPRDAVCEncoder, Capability: profile %d, level %d, max wh=%d %d",
          mCapability.profile, mCapability.level, mCapability.max_width, mCapability.max_height);

#ifdef SPRD_DUMP_YUV
    mFile_yuv = fopen("/data/video.yuv", "wb");
#endif

#ifdef SPRD_DUMP_BS
    mFile_bs = fopen("/data/video.h264", "wb");
#endif
}

SPRDAVCEncoder::~SPRDAVCEncoder() {
    ALOGI("Destruct SPRDAVCEncoder, this: %0x", (void *)this);

    releaseEncoder();

    releaseResource();

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

OMX_ERRORTYPE SPRDAVCEncoder::initEncParams() {
    CHECK(mEncConfig != NULL);
    memset(mEncConfig, 0, sizeof(MMEncConfig));

    CHECK(mEncParams != NULL);
    memset(mEncParams, 0, sizeof(tagAVCEncParam));
    mEncParams->rate_control = AVC_ON;
    mEncParams->initQP = 0;
    mEncParams->init_CBP_removal_delay = 1600;

    mEncParams->intramb_refresh = 0;
    mEncParams->auto_scd = AVC_ON;
    mEncParams->out_of_band_param_set = AVC_ON;
    mEncParams->poc_type = 2;
    mEncParams->log2_max_poc_lsb_minus_4 = 12;
    mEncParams->delta_poc_zero_flag = 0;
    mEncParams->offset_poc_non_ref = 0;
    mEncParams->offset_top_bottom = 0;
    mEncParams->num_ref_in_cycle = 0;
    mEncParams->offset_poc_ref = NULL;

    mEncParams->num_ref_frame = 1;
    mEncParams->num_slice_group = 1;
    mEncParams->fmo_type = 0;

    mEncParams->db_filter = AVC_ON;
    mEncParams->disable_db_idc = 0;

    mEncParams->alpha_offset = 0;
    mEncParams->beta_offset = 0;
    mEncParams->constrained_intra_pred = AVC_OFF;

    mEncParams->data_par = AVC_OFF;
    mEncParams->fullsearch = AVC_OFF;
    mEncParams->search_range = 16;
    mEncParams->sub_pel = AVC_OFF;
    mEncParams->submb_pred = AVC_OFF;
    mEncParams->rdopt_mode = AVC_OFF;
    mEncParams->bidir_pred = AVC_OFF;

    mEncParams->use_overrun_buffer = AVC_OFF;

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
    size_extra += (406*2*sizeof(uint32));
    size_extra += 1024;
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
        if (mIOMMUEnabled) {
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
            mPbuf_extra_p = (int32)phy_addr;
            mPbuf_extra_size = (int32)size;
        }
    }

    unsigned int size_stream = ONEFRAME_BITSTREAM_BFR_SIZE;
    if (mIOMMUEnabled) {
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
        if (mIOMMUEnabled) {
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
            mPbuf_stream_p = (int32)phy_addr;
            mPbuf_stream_size = (int32)size;
        }
    }

    ExtraMemBfr.common_buffer_ptr = mPbuf_extra_v;
    ExtraMemBfr.common_buffer_ptr_phy = (void*)mPbuf_extra_p;
    ExtraMemBfr.size	= size_extra;

    StreamMemBfr.common_buffer_ptr = mPbuf_stream_v;
    StreamMemBfr.common_buffer_ptr_phy = (void *)mPbuf_stream_p;
    StreamMemBfr.size	= size_stream;

    mEncInfo.is_h263 = 0;
    mEncInfo.frame_width = mVideoWidth;
    mEncInfo.frame_height = mVideoHeight;
//    mEncInfo.uv_interleaved = 1;
    mEncInfo.yuv_format = MMENC_YUV420SP_NV21;
    mEncInfo.time_scale = 1000;
#ifdef ANTI_SHAKE
    mEncInfo.b_anti_shake = 1;
#else
    mEncInfo.b_anti_shake = 0;
#endif

    if ((*mH264EncInit)(mHandle, &ExtraMemBfr,&StreamMemBfr, &mEncInfo)) {
        ALOGE("Failed to init mp4enc");
        return OMX_ErrorUndefined;
    }

    if ((*mH264EncGetConf)(mHandle, mEncConfig)) {
        ALOGE("Failed to get default encoding parameters");
        return OMX_ErrorUndefined;
    }

    mEncConfig->h263En = 0;
    mEncConfig->RateCtrlEnable = 1;
    mEncConfig->targetBitRate = mVideoBitRate;
    mEncConfig->FrameRate = mVideoFrameRate;
    mEncConfig->QP_IVOP = 28;
    mEncConfig->QP_PVOP = 28;
    mEncConfig->vbv_buf_size = mVideoBitRate/2;
    mEncConfig->profileAndLevel = 1;

    if ((*mH264EncSetConf)(mHandle, mEncConfig)) {
        ALOGE("Failed to set default encoding parameters");
        return OMX_ErrorUndefined;
    }

    mEncParams->width = mVideoWidth;
    mEncParams->height = mVideoHeight;
    mEncParams->bitrate = mVideoBitRate;
    mEncParams->frame_rate = 1000 * mVideoFrameRate;  // In frames/ms!
    mEncParams->CPB_size = (uint32_t) (mVideoBitRate >> 1);

    // Set IDR frame refresh interval
    if (mIDRFrameRefreshIntervalInSec < 0) {
        mEncParams->idr_period = -1;
    } else if (mIDRFrameRefreshIntervalInSec == 0) {
        mEncParams->idr_period = 1;  // All I frames
    } else {
        mEncParams->idr_period =
            (mIDRFrameRefreshIntervalInSec * mVideoFrameRate);
    }

    // Set profile and level
    mEncParams->profile = mAVCEncProfile;
    mEncParams->level = mAVCEncLevel;

    return OMX_ErrorNone;
}

OMX_ERRORTYPE SPRDAVCEncoder::initEncoder() {
    CHECK(!mStarted);

    OMX_ERRORTYPE errType = OMX_ErrorNone;
    if (OMX_ErrorNone != (errType = initEncParams())) {
        ALOGE("Failed to initialized encoder params");
        mSignalledError = true;
        notify(OMX_EventError, OMX_ErrorUndefined, 0, 0);
        return errType;
    }

    mNumInputFrames = -2;  // 1st two buffers contain SPS and PPS
    mSpsPpsHeaderReceived = false;
    mReadyForNextFrame = true;
    mStarted = true;

    return OMX_ErrorNone;
}

OMX_ERRORTYPE SPRDAVCEncoder::releaseEncoder() {

    (*mH264EncRelease)(mHandle);

    if (mPbuf_inter != NULL)
    {
        free(mPbuf_inter);
        mPbuf_inter = NULL;
    }

    return OMX_ErrorNone;
}

OMX_ERRORTYPE SPRDAVCEncoder::releaseResource() {

    if (mPbuf_extra_v != NULL)
    {
        if (mIOMMUEnabled) {
            mPmem_extra->free_mm_iova(mPbuf_extra_p, mPbuf_extra_size);
        }
        mPmem_extra.clear();
        mPbuf_extra_v = NULL;
        mPbuf_extra_p = 0;
        mPbuf_extra_size = 0;
    }

    if (mPbuf_stream_v != NULL)
    {
        if (mIOMMUEnabled) {
            mPmem_stream->free_mm_iova(mPbuf_stream_p, mPbuf_stream_size);
        }
        mPmem_stream.clear();
        mPbuf_stream_v = NULL;
        mPbuf_stream_p = 0;
        mPbuf_stream_size = 0;
    }

    if (mPbuf_yuv_v != NULL) {
        if (mIOMMUEnabled) {
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

    delete mEncParams;
    mEncParams = NULL;

    delete mEncConfig;
    mEncConfig = NULL;

    delete mHandle;
    mHandle = NULL;

    mStarted = false;

    return OMX_ErrorNone;
}

void SPRDAVCEncoder::initPorts() {
    OMX_PARAM_PORTDEFINITIONTYPE def;
    InitOMXParams(&def);

    const size_t kInputBufferSize = (((mVideoWidth+15)&(~15))  * ((mVideoHeight+15)&(~15))  * 3) >> 1;

    // 31584 is PV's magic number.  Not sure why.
    const size_t kOutputBufferSize =
        (kInputBufferSize > 31584) ? kInputBufferSize: 31584;

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

    def.format.video.cMIMEType = const_cast<char *>("video/avc");
    def.format.video.eCompressionFormat = OMX_VIDEO_CodingAVC;
    def.format.video.eColorFormat = OMX_COLOR_FormatUnused;
    def.format.video.xFramerate = (0 << 16);  // Q16 format
    def.format.video.nBitrate = mVideoBitRate;
    def.format.video.nFrameWidth = mVideoWidth;
    def.format.video.nFrameHeight = mVideoHeight;
    def.format.video.nStride = mVideoWidth;
    def.format.video.nSliceHeight = mVideoHeight;

    addPort(def);
}

OMX_ERRORTYPE SPRDAVCEncoder::internalGetParameter(
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

        if (formatParams->nIndex > 2) {
            return OMX_ErrorNoMore;
        }

        if (formatParams->nPortIndex == 0) {
            formatParams->eCompressionFormat = OMX_VIDEO_CodingUnused;
            if (formatParams->nIndex == 0) {
                formatParams->eColorFormat = OMX_COLOR_FormatYUV420Planar;
            } else if (formatParams->nIndex == 1) {
                formatParams->eColorFormat = OMX_COLOR_FormatYUV420SemiPlanar;
            } else {
                formatParams->eColorFormat = OMX_COLOR_FormatAndroidOpaque;
            }
        } else {
            formatParams->eCompressionFormat = OMX_VIDEO_CodingAVC;
            formatParams->eColorFormat = OMX_COLOR_FormatUnused;
        }

        return OMX_ErrorNone;
    }

    case OMX_IndexParamVideoAvc:
    {
        OMX_VIDEO_PARAM_AVCTYPE *avcParams =
            (OMX_VIDEO_PARAM_AVCTYPE *)params;

        if (avcParams->nPortIndex != 1) {
            return OMX_ErrorUndefined;
        }

        avcParams->eProfile = OMX_VIDEO_AVCProfileBaseline;
        OMX_U32 omxLevel = AVC_LEVEL2;
        if (OMX_ErrorNone !=
                ConvertAvcSpecLevelToOmxAvcLevel(mAVCEncLevel, &omxLevel)) {
            return OMX_ErrorUndefined;
        }

        avcParams->eLevel = (OMX_VIDEO_AVCLEVELTYPE) omxLevel;
        avcParams->nRefFrames = 1;
        avcParams->nBFrames = 0;
        avcParams->bUseHadamard = OMX_TRUE;
        avcParams->nAllowedPictureTypes =
            (OMX_VIDEO_PictureTypeI | OMX_VIDEO_PictureTypeP);
        avcParams->nRefIdx10ActiveMinus1 = 0;
        avcParams->nRefIdx11ActiveMinus1 = 0;
        avcParams->bWeightedPPrediction = OMX_FALSE;
        avcParams->bEntropyCodingCABAC = OMX_FALSE;
        avcParams->bconstIpred = OMX_FALSE;
        avcParams->bDirect8x8Inference = OMX_FALSE;
        avcParams->bDirectSpatialTemporal = OMX_FALSE;
        avcParams->nCabacInitIdc = 0;
        return OMX_ErrorNone;
    }

    case OMX_IndexParamVideoProfileLevelQuerySupported:
    {
        OMX_VIDEO_PARAM_PROFILELEVELTYPE *profileLevel =
            (OMX_VIDEO_PARAM_PROFILELEVELTYPE *)params;

        if (profileLevel->nPortIndex != 1) {
            return OMX_ErrorUndefined;
        }

        const size_t size =
            sizeof(ConversionTable) / sizeof(ConversionTable[0]);

        if (profileLevel->nProfileIndex >= size) {
            return OMX_ErrorNoMore;
        }

        if (mCapability.profile == AVC_BASELINE) {
            profileLevel->eProfile = OMX_VIDEO_AVCProfileBaseline;
        } else if (mCapability.profile == AVC_MAIN) {
            profileLevel->eProfile = OMX_VIDEO_AVCProfileMain;
        } else if (mCapability.profile == AVC_HIGH) {
            profileLevel->eProfile = OMX_VIDEO_AVCProfileHigh;
        } else {
            profileLevel->eProfile = OMX_VIDEO_AVCProfileBaseline;
        }

        if (ConversionTable[profileLevel->nProfileIndex].avcLevel > mCapability.level) {
            return OMX_ErrorUnsupportedIndex;
        } else {
            profileLevel->eLevel = ConversionTable[profileLevel->nProfileIndex].omxLevel;
        }
        //ALOGI("Query supported profile level = %d, %d",profileLevel->eProfile, profileLevel->eLevel);
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

OMX_ERRORTYPE SPRDAVCEncoder::internalSetParameter(
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
                     def->format.video.eColorFormat != OMX_COLOR_FormatYUV420SemiPlanar &&
                     def->format.video.eColorFormat != OMX_COLOR_FormatAndroidOpaque)) {
                return OMX_ErrorUndefined;
            }
        } else {
            if (def->format.video.eCompressionFormat != OMX_VIDEO_CodingAVC ||
                    (def->format.video.eColorFormat != OMX_COLOR_FormatUnused)) {
                return OMX_ErrorUndefined;
            }
        }

        // As encoder we should modify bufferSize on Input port
        // Make sure we have enough input date for input buffer
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
                    "video_encoder.avc",
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

        if (formatParams->nIndex > 2) {
            return OMX_ErrorNoMore;
        }

        if (formatParams->nPortIndex == 0) {
            if (formatParams->eCompressionFormat != OMX_VIDEO_CodingUnused ||
                    ((formatParams->nIndex == 0 &&
                      formatParams->eColorFormat != OMX_COLOR_FormatYUV420Planar) ||
                     (formatParams->nIndex == 1 &&
                      formatParams->eColorFormat != OMX_COLOR_FormatYUV420SemiPlanar) ||
                     (formatParams->nIndex == 2 &&
                      formatParams->eColorFormat != OMX_COLOR_FormatAndroidOpaque) )) {
                return OMX_ErrorUndefined;
            }
            mVideoColorFormat = formatParams->eColorFormat;
        } else {
            if (formatParams->eCompressionFormat != OMX_VIDEO_CodingAVC ||
                    formatParams->eColorFormat != OMX_COLOR_FormatUnused) {
                return OMX_ErrorUndefined;
            }
        }

        return OMX_ErrorNone;
    }

    case OMX_IndexParamVideoAvc:
    {
        OMX_VIDEO_PARAM_AVCTYPE *avcType =
            (OMX_VIDEO_PARAM_AVCTYPE *)params;

        if (avcType->nPortIndex != 1) {
            return OMX_ErrorUndefined;
        }

        // PV's AVC encoder only supports baseline profile
        if (avcType->eProfile != OMX_VIDEO_AVCProfileBaseline ||
                avcType->nRefFrames != 1 ||
                avcType->nBFrames != 0 ||
                avcType->bUseHadamard != OMX_TRUE ||
                (avcType->nAllowedPictureTypes & OMX_VIDEO_PictureTypeB) != 0 ||
                avcType->nRefIdx10ActiveMinus1 != 0 ||
                avcType->nRefIdx11ActiveMinus1 != 0 ||
                avcType->bWeightedPPrediction != OMX_FALSE ||
                avcType->bEntropyCodingCABAC != OMX_FALSE ||
                avcType->bconstIpred != OMX_FALSE ||
                avcType->bDirect8x8Inference != OMX_FALSE ||
                avcType->bDirectSpatialTemporal != OMX_FALSE ||
                avcType->nCabacInitIdc != 0) {
            return OMX_ErrorUndefined;
        }

        if (OK != ConvertOmxAvcLevelToAvcSpecLevel(avcType->eLevel, &mAVCEncLevel)) {
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


OMX_ERRORTYPE SPRDAVCEncoder::getExtensionIndex(
    const char *name, OMX_INDEXTYPE *index)
{
    if(strcmp(name, "OMX.google.android.index.storeMetaDataInBuffers") == 0) {
        *index = (OMX_INDEXTYPE) OMX_IndexParamStoreMetaDataBuffer;
        return OMX_ErrorNone;
    }

    return SprdSimpleOMXComponent::getExtensionIndex(name, index);
}

void SPRDAVCEncoder::onQueueFilled(OMX_U32 portIndex) {
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

        // Combine SPS and PPS and place them in the very first output buffer
        // SPS and PPS are separated by start code 0x00000001
        // Assume that we have exactly one SPS and exactly one PPS.
        if (!mSpsPpsHeaderReceived && mNumInputFrames <= 0) {
            MMEncOut sps_header, pps_header;
            int ret;

            memset(&sps_header, 0, sizeof(MMEncOut));
            memset(&pps_header, 0, sizeof(MMEncOut));

            ++mNumInputFrames;
            ret = (*mH264EncGenHeader)(mHandle, &sps_header, 1);
            outHeader->nFilledLen = sps_header.strmSize;
            ALOGI("%s, %d, sps_header.strmSize: %d", __FUNCTION__, __LINE__, sps_header.strmSize);

            {   //added by xiaowei, 2013.10.08, for bug 220340.
                uint8 *p = (uint8 *)(sps_header.pOutBuf);
                ALOGI("sps: %0x, %0x, %0x, %0x, %0x, %0x, %0x, %0x, %0x, %0x, %0x,%0x, %0x, %0x, %0x, %0x",
                      p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9],p[10], p[11], p[12], p[13], p[14], p[15]);

                p[0] = p[1] = p[2] = 0x0;
                p[3] = 0x1;
            }

#ifdef SPRD_DUMP_BS
            if (mFile_bs != NULL) {
                fwrite(sps_header.pOutBuf, 1, sps_header.strmSize, mFile_bs);
            }
#endif

            memcpy(outPtr, sps_header.pOutBuf, sps_header.strmSize);
            outPtr+= sps_header.strmSize;

            ++mNumInputFrames;
            ret = (*mH264EncGenHeader)(mHandle, &pps_header, 0);
            ALOGI("%s, %d, pps_header.strmSize: %d", __FUNCTION__, __LINE__, pps_header.strmSize);

            {   //added by xiaowei, 2013.10.08, for bug 220340.
                uint8 *p = (uint8 *)(pps_header.pOutBuf);
                ALOGI("pps: %0x, %0x, %0x, %0x, %0x, %0x, %0x, %0x,",
                      p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);

                p[0] = p[1] = p[2] = 0x0;
                p[3] = 0x1;
            }

#ifdef SPRD_DUMP_BS
            if (mFile_bs != NULL) {
                fwrite(pps_header.pOutBuf, 1, pps_header.strmSize, mFile_bs);
            }
#endif

            outHeader->nFilledLen += pps_header.strmSize;
            memcpy(outPtr, pps_header.pOutBuf, pps_header.strmSize);

            mSpsPpsHeaderReceived = true;
            CHECK_EQ(0, mNumInputFrames);  // 1st video frame is 0
            outHeader->nFlags = OMX_BUFFERFLAG_CODECCONFIG;
            outQueue.erase(outQueue.begin());
            outInfo->mOwnedByUs = false;
            notifyFillBufferDone(outHeader);
            return;
        }

        ALOGV("%s, %d, inHeader->nFilledLen: %d, mStoreMetaData: %d, mVideoColorFormat: 0x%x",
              __FUNCTION__, __LINE__, inHeader->nFilledLen, mStoreMetaData, mVideoColorFormat);

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
            memset(&vid_in, 0, sizeof(MMEncIn));
            memset(&vid_out, 0, sizeof(MMEncOut));
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
                        if (mIOMMUEnabled) {
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
                        mPbuf_yuv_p = (int32)phy_addr;
                        mPbuf_yuv_size = (int32)buffer_size;
                    }

                    py = mPbuf_yuv_v;
                    py_phy = (uint8_t*)mPbuf_yuv_p;

                    GraphicBufferMapper &mapper = GraphicBufferMapper::get();
                    buffer_handle_t buf = *((buffer_handle_t *)(inputData + 4));
                    Rect bounds((mVideoWidth+15)&(~15), (mVideoHeight+15)&(~15));

                    void* vaddr;
                    if (mapper.lock(buf, GRALLOC_USAGE_SW_READ_OFTEN|GRALLOC_USAGE_SW_WRITE_NEVER, bounds, &vaddr)) {
                        return;
                    }

                    if (mVideoColorFormat == OMX_COLOR_FormatYUV420Planar) {
                        ConvertYUV420PlanarToYUV420SemiPlanar((uint8_t*)vaddr, py, mVideoWidth, mVideoHeight,
                                                              (mVideoWidth + 15) & (~15), (mVideoHeight + 15) & (~15));
                    } else if(mVideoColorFormat == OMX_COLOR_FormatAndroidOpaque) {
                        ConvertARGB888ToYUV420SemiPlanar((uint8_t*)vaddr, py, mVideoWidth, mVideoHeight, (mVideoWidth+15)&(~15), (mVideoHeight+15)&(~15));
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
                    mPbuf_yuv_p = (int32)phy_addr;
                    mPbuf_yuv_size = (int32)buffer_size;
                }

                py = mPbuf_yuv_v;
                py_phy = (uint8_t*)mPbuf_yuv_p;

                if (mVideoColorFormat == OMX_COLOR_FormatYUV420Planar) {
                    ConvertYUV420PlanarToYUV420SemiPlanar(inputData, py, mVideoWidth, mVideoHeight,
                                                          (mVideoWidth + 15) & (~15), (mVideoHeight + 15) & (~15));
                } else if(mVideoColorFormat == OMX_COLOR_FormatAndroidOpaque) {
                    ConvertARGB888ToYUV420SemiPlanar(inputData, py, mVideoWidth, mVideoHeight, (mVideoWidth+15)&(~15), (mVideoHeight+15)&(~15));
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

            int64_t start_encode = systemTime();
            int ret = (*mH264EncStrmEncode)(mHandle, &vid_in, &vid_out);
            int64_t end_encode = systemTime();
            ALOGI("H264EncStrmEncode[%lld] %dms, in {%p-%p, %dx%d}, out {%p-%d}, wh{%d, %d}, xy{%d, %d}",
                  mNumInputFrames, (unsigned int)((end_encode-start_encode) / 1000000L), py, py_phy,
                  mVideoWidth, mVideoHeight, vid_out.pOutBuf, vid_out.strmSize, width, height, x, y);
            if ((vid_out.strmSize < 0) || (ret != MMENC_OK)) {
                ALOGE("Failed to encode frame %lld, ret=%d", mNumInputFrames, ret);
#if 0  //removed by xiaowei, 20131017, for cr224544              
                mSignalledError = true;
                notify(OMX_EventError, OMX_ErrorUndefined, 0, 0);
#endif
            } else {

                ALOGI("%s, %d, out_stream_ptr: %p", __FUNCTION__, __LINE__, outPtr);

                {   //added by xiaowei, 2013.10.08, for bug 220340.
                    uint8 *p = (uint8 *)(vid_out.pOutBuf);
                    ALOGI("frame: %0x, %0x, %0x, %0x, %0x, %0x, %0x, %0x,",
                          p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);

                    p[0] = p[1] = p[2] = 0x0;
                    p[3] = 0x1;
                }
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
            ALOGI("saw EOS");
            outHeader->nFlags = OMX_BUFFERFLAG_EOS;
        }

        inQueue.erase(inQueue.begin());
        inInfo->mOwnedByUs = false;
        notifyEmptyBufferDone(inHeader);

        CHECK(!mInputBufferInfoVec.empty());
        InputBufferInfo *inputBufInfo = mInputBufferInfoVec.begin();
        if (dataLength > 0 || (inHeader->nFlags & OMX_BUFFERFLAG_EOS)) //add this judgement by xiaowei, 20131017, for cr224544
        {
            outQueue.erase(outQueue.begin());
            outHeader->nTimeStamp = inputBufInfo->mTimeUs;
            outHeader->nFlags |= (inputBufInfo->mFlags | OMX_BUFFERFLAG_ENDOFFRAME);
            outHeader->nFilledLen = dataLength;
            outInfo->mOwnedByUs = false;
            notifyFillBufferDone(outHeader);
        }
        mInputBufferInfoVec.erase(mInputBufferInfoVec.begin());
    }
}

bool SPRDAVCEncoder::openEncoder(const char* libName)
{
    if(mLibHandle) {
        dlclose(mLibHandle);
    }

    ALOGI("openEncoder, lib: %s", libName);

    mLibHandle = dlopen(libName, RTLD_NOW);
    if(mLibHandle == NULL) {
        ALOGE("openEncoder, can't open lib: %s",libName);
        return false;
    }

    mH264EncGetCodecCapability = (FT_H264EncGetCodecCapability)dlsym(mLibHandle, "H264EncGetCodecCapability");
    if(mH264EncGetCodecCapability == NULL) {
        ALOGE("Can't find H264EncGetCodecCapability in %s",libName);
        dlclose(mLibHandle);
        mLibHandle = NULL;
        return false;
    }

    mH264EncPreInit = (FT_H264EncPreInit)dlsym(mLibHandle, "H264EncPreInit");
    if(mH264EncPreInit == NULL) {
        ALOGE("Can't find mH264EncPreInit in %s",libName);
        dlclose(mLibHandle);
        mLibHandle = NULL;
        return false;
    }

    mH264EncInit = (FT_H264EncInit)dlsym(mLibHandle, "H264EncInit");
    if(mH264EncInit == NULL) {
        ALOGE("Can't find H264EncInit in %s",libName);
        dlclose(mLibHandle);
        mLibHandle = NULL;
        return false;
    }

    mH264EncSetConf = (FT_H264EncSetConf)dlsym(mLibHandle, "H264EncSetConf");
    if(mH264EncSetConf == NULL) {
        ALOGE("Can't find H264EncSetConf in %s",libName);
        dlclose(mLibHandle);
        mLibHandle = NULL;
        return false;
    }

    mH264EncGetConf = (FT_H264EncGetConf)dlsym(mLibHandle, "H264EncGetConf");
    if(mH264EncGetConf == NULL) {
        ALOGE("Can't find H264EncGetConf in %s",libName);
        dlclose(mLibHandle);
        mLibHandle = NULL;
        return false;
    }

    mH264EncStrmEncode = (FT_H264EncStrmEncode)dlsym(mLibHandle, "H264EncStrmEncode");
    if(mH264EncStrmEncode == NULL) {
        ALOGE("Can't find H264EncStrmEncode in %s",libName);
        dlclose(mLibHandle);
        mLibHandle = NULL;
        return false;
    }

    mH264EncGenHeader = (FT_H264EncGenHeader)dlsym(mLibHandle, "H264EncGenHeader");
    if(mH264EncGenHeader == NULL) {
        ALOGE("Can't find H264EncGenHeader in %s",libName);
        dlclose(mLibHandle);
        mLibHandle = NULL;
        return false;
    }

    mH264EncRelease = (FT_H264EncRelease)dlsym(mLibHandle, "H264EncRelease");
    if(mH264EncRelease == NULL) {
        ALOGE("Can't find H264EncRelease in %s",libName);
        dlclose(mLibHandle);
        mLibHandle = NULL;
    }

    return true;
}

}  // namespace android

android::SprdOMXComponent *createSprdOMXComponent(
    const char *name, const OMX_CALLBACKTYPE *callbacks,
    OMX_PTR appData, OMX_COMPONENTTYPE **component) {
    return new android::SPRDAVCEncoder(name, callbacks, appData, component);
}
