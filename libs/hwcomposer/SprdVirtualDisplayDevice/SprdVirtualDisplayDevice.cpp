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
 ** File:SprdVirtualDisplayDevice.cpp DESCRIPTION                             *
 **                                   Manager Virtual Display device.         *
 ******************************************************************************
 ******************************************************************************
 ** Author:         zhongjun.chen@spreadtrum.com                              *
 *****************************************************************************/

#include "SprdVirtualDisplayDevice.h"


using namespace android;

SprdVirtualDisplayDevice:: SprdVirtualDisplayDevice()
    : mDebugFlag(0),
      mDumpFlag(0)
{

}

SprdVirtualDisplayDevice:: ~SprdVirtualDisplayDevice()
{

}

int SprdVirtualDisplayDevice:: getDisplayAttributes(DisplayAttributes *dpyAttributes)
{
    return 0;
}

int SprdVirtualDisplayDevice:: prepare(hwc_display_contents_1_t *list)
{
    queryDebugFlag(&mDebugFlag);

    if (list == NULL)
    {
        ALOGI_IF(mDebugFlag, "prepre: Virtual Display Device maybe closed");
        return 0;
    }


    return 0;
}

int SprdVirtualDisplayDevice:: commit(hwc_display_contents_1_t *list)
{
    int releaseFenceFd = -1;
    hwc_layer_1_t *FBTargetLayer = NULL;

    queryDebugFlag(&mDebugFlag);

    if (list == NULL)
    {
        ALOGI_IF(mDebugFlag, "commit: Virtual Display Device maybe closed");
        return 0;
    }

    FBTargetLayer = &(list->hwLayers[list->numHwLayers - 1]);
    if (FBTargetLayer == NULL)
    {
        ALOGE("VirtualDisplay FBTLayer is NULL");
        return -1;
    }

    const native_handle_t *pNativeHandle = FBTargetLayer->handle;
    struct private_handle_t *privateH = (struct private_handle_t *)pNativeHandle;

    ALOGI_IF(mDebugFlag, "Start Display VirtualDisplay FBT layer");

    if (FBTargetLayer->acquireFenceFd >= 0)
    {
        String8 name("HWCFBTVirtual::Post");

        FenceWaitForever(name, FBTargetLayer->acquireFenceFd);

        if (FBTargetLayer->acquireFenceFd >= 0)
        {
            close(FBTargetLayer->acquireFenceFd);
            FBTargetLayer->acquireFenceFd = -1;
        }
    }

    closeAcquireFDs(list);

    /*
     *  Virtual display just have outbufAcquireFenceFd.
     *  We do not touch this outbuf, and do not need
     *  wait this fence, so just send this acquireFence
     *  back to SurfaceFlinger as retireFence.
     * */
    list->retireFenceFd = list->outbufAcquireFenceFd;

    return 0;
}
