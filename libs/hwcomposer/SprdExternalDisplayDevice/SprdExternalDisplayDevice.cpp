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
 ** File:SprdExternalDisplayDevice.cpp DESCRIPTION                            *
 **                                   Manager External Display device.        *
 ******************************************************************************
 ******************************************************************************
 ** Author:         zhongjun.chen@spreadtrum.com                              *
 *****************************************************************************/

#include "SprdExternalDisplayDevice.h"

using namespace android;

SprdExternalDisplayDevice:: SprdExternalDisplayDevice()
    : mDebugFlag(0),
      mDumpFlag(0)
{

}

SprdExternalDisplayDevice:: ~SprdExternalDisplayDevice()
{

}

int SprdExternalDisplayDevice:: getDisplayAttributes(DisplayAttributes *dpyAttributes)
{
    float refreshRate = 60.0;

    if (dpyAttributes == NULL)
    {
        ALOGE("Input parameter is NULL");
        return -1;
    }

    dpyAttributes->vsync_period = 0;
    dpyAttributes->xres = 0;
    dpyAttributes->yres = 0;
    dpyAttributes->stride = 0;
    dpyAttributes->xdpi = 0;
    dpyAttributes->ydpi = 0;
    dpyAttributes->connected = false;

    return 0;
}

int SprdExternalDisplayDevice:: prepare(hwc_display_contents_1_t *list)
{
    queryDebugFlag(&mDebugFlag);

    if (list == NULL)
    {
        ALOGI_IF(mDebugFlag, "commit: External Display Device maybe closed");
        return 0;
    }

    return 0;
}

int SprdExternalDisplayDevice:: commit(hwc_display_contents_1_t *list)
{
    hwc_layer_1_t *FBTargetLayer = NULL;

    queryDebugFlag(&mDebugFlag);

    if (list == NULL)
    {
        ALOGI_IF(mDebugFlag, "commit: External Display Device maybe closed");
        return 0;
    }

    waitAcquireFence(list);

    syncReleaseFence(list, DISPLAY_EXTERNAL);

    FBTargetLayer = &(list->hwLayers[list->numHwLayers - 1]);
    if (FBTargetLayer == NULL)
    {
        ALOGE("FBTargetLayer is NULL");
        return -1;
    }

    const native_handle_t *pNativeHandle = FBTargetLayer->handle;
    struct private_handle_t *privateH = (struct private_handle_t *)pNativeHandle;

    ALOGI_IF(mDebugFlag, "Start Displaying ExternalDisplay FramebufferTarget layer");

    if (FBTargetLayer->acquireFenceFd >= 0)
    {
        String8 name("HWCFBTExternal::Post");

        FenceWaitForever(name, FBTargetLayer->acquireFenceFd);

        if (FBTargetLayer->acquireFenceFd >= 0)
        {
            close(FBTargetLayer->acquireFenceFd);
            FBTargetLayer->acquireFenceFd = -1;
        }
    }

    closeAcquireFDs(list);

    createRetiredFence(list);

    return 0;
}
