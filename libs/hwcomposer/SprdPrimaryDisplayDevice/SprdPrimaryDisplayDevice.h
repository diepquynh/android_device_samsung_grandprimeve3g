/*
 * Copyright (C) 2010 The Android Open Source Project
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


/******************************************************************************
 **                   Edit    History                                         *
 **---------------------------------------------------------------------------*
 ** DATE          Module              DESCRIPTION                             *
 ** 22/09/2013    Hardware Composer   Responsible for processing some         *
 **                                   Hardware layers. These layers comply    *
 **                                   with display controller specification,  *
 **                                   can be displayed directly, bypass       *
 **                                   SurfaceFligner composition. It will     *
 **                                   improve system performance.             *
 ******************************************************************************
 ** File: SprdPrimaryDisplayDevice.h  DESCRIPTION                             *
 **                                   Manage the PrimaryDisplayDevice         *
 **                                   including prepare and commit            *
 ******************************************************************************
 ******************************************************************************
 ** Author:         zhongjun.chen@spreadtrum.com                              *
 *****************************************************************************/

#ifndef _SPRD_PRIMARY_DISPLAY_DEVICE_H_
#define _SPRD_PRIMARY_DISPLAY_DEVICE_H_

#include <hardware/hardware.h>
#include <hardware/hwcomposer.h>
#include <fcntl.h>
#include <errno.h>

#include <EGL/egl.h>

#include <utils/RefBase.h>
#include <cutils/properties.h>
#include <cutils/atomic.h>
#include <cutils/log.h>

#include "SprdHWLayerList.h"
#include "SprdOverlayPlane.h"
#include "SprdPrimaryPlane.h"
#include "SprdVsyncEvent.h"
#include "SprdFrameBufferHAL.h"
#include "../SprdDisplayDevice.h"
#include "../AndroidFence.h"

#ifdef OVERLAY_COMPOSER_GPU
#include "../OverlayComposer/OverlayComposer.h"
#endif

#include "../SprdUtil.h"
#include "../dump.h"

using namespace android;

class SprdHWLayerList;
class SprdUtil;

class SprdPrimaryDisplayDevice
{
public:
    SprdPrimaryDisplayDevice();

    ~SprdPrimaryDisplayDevice();

    /*
     *  Initialize the SprdPrimaryDisplayDevice member and return
     *  FrameBufferInfo.
     * */
    bool Init(FrameBufferInfo **fbInfo);

    /*
     *  Traversal layer list, and find layers which comply with SprdDisplayPlane
     *  and mark them as HWC_OVERLAY.
     * */
    int prepare(hwc_display_contents_1_t *list);

    /*
     *  Post layers to SprdDisplayPlane.
     * */
    int commit(hwc_display_contents_1_t* list);

    /*
     *  set up the Android Procs callback to Primary
     *  Display Decice.
     * */
    void setVsyncEventProcs(const hwc_procs_t *procs);

    /*
     *  Primary Display Device event control interface.
     *  Make vsync event enable or disable.
     * */
    void eventControl(int enabled);

    /*
     *  Display configure attribution.
     * */
    int getDisplayAttributes(DisplayAttributes *dpyAttributes);

    /*
     *  Recycle DispalyPlane buffer for saving memory.
     * */
    int reclaimPlaneBuffer(SprdHWLayer *YUVLayer);

private:
    FrameBufferInfo   *mFBInfo;
    SprdHWLayerList   *mLayerList;
    SprdOverlayPlane  *mOverlayPlane;
    SprdPrimaryPlane  *mPrimaryPlane;
#ifdef OVERLAY_COMPOSER_GPU
    sp<OverlayNativeWindow> mWindow;
    sp<OverlayComposer> mOverlayComposer;
#endif
    sp<SprdVsyncEvent>  mVsyncEvent;
    SprdUtil          *mUtil;
    bool mPostFrameBuffer;
    int mHWCDisplayFlag;
    int mDebugFlag;
    int mDumpFlag;

    inline SprdHWLayerList *getHWLayerList()
    {
        return mLayerList;
    }

    inline sp<SprdVsyncEvent> getVsyncEventHandle()
    {
        return mVsyncEvent;
    }

    /*
     *  And then attach these HWC_OVERLAY layers to SprdDisplayPlane.
     * */
    int attachToDisplayPlane(int DisplayFlag);
};

#endif
