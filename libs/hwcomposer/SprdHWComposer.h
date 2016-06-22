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
 ** File: SprdHWComposer.h            DESCRIPTION                             *
 **                                   comunicate with SurfaceFlinger and      *
 **                                   other class objects of HWComposer       *
 ******************************************************************************
 ******************************************************************************
 ** Author:         zhongjun.chen@spreadtrum.com                              *
 *****************************************************************************/


#ifndef _SPRD_HWCOMPOSER_H
#define _SPRD_HWCOMPOSER_H

#include <hardware/hardware.h>
#include <hardware/hwcomposer.h>
#include <fcntl.h>
#include <errno.h>

#include <EGL/egl.h>

#include <utils/RefBase.h>
#include <cutils/properties.h>
#include <cutils/atomic.h>
#include <cutils/log.h>

#include "SprdPrimaryDisplayDevice/SprdPrimaryDisplayDevice.h"
#include "SprdVirtualDisplayDevice/SprdVirtualDisplayDevice.h"
#include "SprdExternalDisplayDevice/SprdExternalDisplayDevice.h"
#include "SprdDisplayDevice.h"

#include "dump.h"

using namespace android;


class SprdHWComposer: public hwc_composer_device_1_t
{
public:
    SprdHWComposer()
        : mPrimaryDisplay(0),
          mExternalDisplay(0),
          mVirtualDisplay(0),
          mFBInfo(0),
          mInitFlag(0),
          mDebugFlag(0),
          mDumpFlag(0)
    {

    }

    ~SprdHWComposer();

    /*
     *  Allocate and initialize the local objects used by HWComposer
     * */
    bool Init();

    /*
     *  Traversal display device, and find layers which comply with display device.
     *  and mark them as HWC_OVERLAY.
     * */
    int prepareDisplays(size_t numDisplays, hwc_display_contents_1_t **displays);

    /*
     *  Post layers to display device.
     * */
    int commitDisplays(size_t numDisplays, hwc_display_contents_1_t **displays);

    /*
     *  Blanks or unblanks a display's screen.
     *  Turns the screen off when blank is nonzero, on when blank is zero.
     * */
    int blank(int disp, int blank);

    /*
     *  Used to retrieve information about the h/w composer.
     * */
    int query(int what, int* value);

    void dump(char *buff, int buff_len);

    /*
     *  returns handles for the configurations available
     *  on the connected display. These handles must remain valid
     *  as long as the display is connected.
     * */
    int getDisplayConfigs(int disp, uint32_t* configs, size_t* numConfigs);

    /*
     *  returns attributes for a specific config of a
     *  connected display. The config parameter is one of
     *  the config handles returned by getDisplayConfigs.
     * */
    int getDisplayAttributes(int disp, uint32_t config, const uint32_t* attributes, int32_t* value);

    /*
     *  Registor a callback from Android Framework.
     * */
    void registerProcs(hwc_procs_t const* procs);

    /*
     *  Control vsync event, enable or disable.
     * */
    bool eventControl(int disp, int enabled);


private:
    SprdPrimaryDisplayDevice  *mPrimaryDisplay;
    SprdExternalDisplayDevice *mExternalDisplay;
    SprdVirtualDisplayDevice  *mVirtualDisplay;
    FrameBufferInfo *mFBInfo;
    DisplayAttributes mDisplayAttributes[MAX_DISPLAYS];
    int mInitFlag;
    int mDebugFlag;
    int mDumpFlag;

    void resetDisplayAttributes();
};

#endif
