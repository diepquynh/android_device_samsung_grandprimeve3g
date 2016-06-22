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
 ** File: SprdHWComposer.cpp          DESCRIPTION                             *
 **                                   comunicate with SurfaceFlinger and      *
 **                                   other class objects of HWComposer       *
 ******************************************************************************
 ******************************************************************************
 ** Author:         zhongjun.chen@spreadtrum.com                              *
 *****************************************************************************/

#include "SprdHWComposer.h"
#include "AndroidFence.h"


using namespace android;


void SprdHWComposer:: resetDisplayAttributes()
{
    for (int i = 0; i < MAX_DISPLAYS; i ++)
    {
        DisplayAttributes *dpyAttr = &(mDisplayAttributes[i]);
        dpyAttr->vsync_period = 0;
        dpyAttr->xres = 0;
        dpyAttr->yres = 0;
        dpyAttr->stride = 0;
        dpyAttr->xdpi = 0;
        dpyAttr->ydpi = 0;
        dpyAttr->connected = false;
    }
}

bool SprdHWComposer:: Init()
{
    resetDisplayAttributes();


    /*
     *  SprdPrimaryDisplayDevice information
     * */
    mPrimaryDisplay = new SprdPrimaryDisplayDevice();
    if (mPrimaryDisplay == NULL) {
        ALOGE("new SprdPrimaryDisplayDevice failed");
        return false;
    }

    if (!(mPrimaryDisplay->Init(&mFBInfo)))
    {
        ALOGE("mPrimaryDisplayDevice init failed");
        return false;
    }

    mPrimaryDisplay->getDisplayAttributes(&(mDisplayAttributes[DISPLAY_PRIMARY]));


    /*
     *  SprdExternalDisplayDevice information
     * */
    mExternalDisplay = new SprdExternalDisplayDevice();
    if (mExternalDisplay == NULL) {
        ALOGE("new mExternalDisplayDevice failed");
        return false;
    }

    mExternalDisplay->getDisplayAttributes(&(mDisplayAttributes[DISPLAY_EXTERNAL]));


    /*
     *  SprdVirtualDisplayDevice information
     * */
    mVirtualDisplay = new SprdVirtualDisplayDevice();
    if (mVirtualDisplay == NULL) {
        ALOGE("new mVirtualDisplayDevice failed");
        return false;
    }

    mVirtualDisplay->getDisplayAttributes(&(mDisplayAttributes[DISPLAY_VIRTUAL]));

    openSprdFence();

    mInitFlag = 1;

    return true;
}

SprdHWComposer:: ~SprdHWComposer()
{
    closeSprdFence();

    if (mPrimaryDisplay) {
        delete mPrimaryDisplay;
        mPrimaryDisplay = NULL;
    }

    if (mExternalDisplay) {
        delete mExternalDisplay;
        mExternalDisplay = NULL;
    }

    if (mVirtualDisplay) {
        delete mVirtualDisplay;
        mVirtualDisplay = NULL;
    }

    mInitFlag = 0;
}


int SprdHWComposer:: prepareDisplays(size_t numDisplays, hwc_display_contents_1_t **displays)
{
    int ret = 0;

    for(unsigned int i = 0; i < numDisplays; i++)
    {
        hwc_display_contents_1_t *display = displays[i];

        switch(i)
        {
            case DISPLAY_PRIMARY:
                mPrimaryDisplay->prepare(display);
                break;
            case DISPLAY_EXTERNAL:
                mExternalDisplay->prepare(display);
                break;
            case DISPLAY_VIRTUAL:
                mVirtualDisplay->prepare(display);
                break;
            default:
                ret = -EINVAL;
        }
    }

    return ret;
}

int SprdHWComposer:: commitDisplays(size_t numDisplays, hwc_display_contents_1_t **displays)
{
    int ret = 0;

    for(unsigned int i = 0; i < numDisplays; i++)
    {
        hwc_display_contents_1_t *display = displays[i];

        switch(i)
        {
            case DISPLAY_PRIMARY:
                mPrimaryDisplay->commit(display);
                break;
            case DISPLAY_EXTERNAL:
                mExternalDisplay->commit(display);
                break;
            case DISPLAY_VIRTUAL:
                mVirtualDisplay->commit(display);
                break;
            default:
                ret = -EINVAL;
        }
    }

    return ret;
}

int SprdHWComposer:: blank(int disp, int blank)
{
    queryDebugFlag(&mDebugFlag);
    ALOGI_IF(mDebugFlag, "%s : %s display:%d", __func__,
             (blank == 1) ? "Blanking" : "UnBlanking", disp);

    if (blank)
    {
        /*
         *  Here, we need free up all the Overlay Plane and
         *  Primary plane.
         * */
    }

    /*
     *  DisplayC need implementing this feature.
     * */
#if 0
    switch(disp)
    {
        case DISPLAY_PRIMARY:
            if (blank)
            {
                ioctl(mFBInfo->fbfd, FBIOBLANK, FB_BLANK_POWERDOWN);
            }
            else
            {
                ioctl(mFBInfo->fbfd, FBIOBLANK, FB_BLANK_UNBLANK);
            }
            break;
        case DISPLAY_EXTERNAL:
        case DISPLAY_VIRTUAL:
            if (blank)
            {

            }
            break;
        default:
            return -EINVAL;
    }
#endif

    return 0;
}

int SprdHWComposer:: query(int what, int* value)
{
#ifdef HWC_SUPPORT
    *value = 1;
#else
    *value = 0;
#endif
    return 0;
}

void SprdHWComposer:: dump(char *buff, int buff_len)
{
}

int SprdHWComposer:: getDisplayConfigs(int disp, uint32_t* configs, size_t* numConfigs)
{
    int ret = -1;

    switch(disp)
    {
        case DISPLAY_PRIMARY:
            if (*numConfigs > 0)
            {
                configs[0] = 0;
                *numConfigs = 1;
            }
            ret = 0;
            break;
        case DISPLAY_EXTERNAL:
            ret = -1;
            if (mDisplayAttributes[DISPLAY_EXTERNAL].connected)
            {
                if (*numConfigs > 0)
                {
                    configs[0] = 0;
                    *numConfigs = 1;
                }
            }
            ret = 0;
            break;
        default:
            return ret;
    }

    return ret;
}

int SprdHWComposer:: getDisplayAttributes(int disp, uint32_t config, const uint32_t* attributes, int32_t* value)
{
    if (DISPLAY_EXTERNAL == disp && !(mDisplayAttributes[disp].connected))
    {
        ALOGD("External Display Device is not connected");
        return -1;
    }

    if (DISPLAY_VIRTUAL == disp && !(mDisplayAttributes[disp].connected))
    {
        ALOGD("VIRTUAL Display Device is not connected");
        return -1;
    }

    static const uint32_t DISPLAY_ATTRIBUTES[] = {
        HWC_DISPLAY_VSYNC_PERIOD,
        HWC_DISPLAY_WIDTH,
        HWC_DISPLAY_HEIGHT,
        HWC_DISPLAY_DPI_X,
        HWC_DISPLAY_DPI_Y,
        HWC_DISPLAY_NO_ATTRIBUTE,
    };

    const int NUM_DISPLAY_ATTRIBUTES = sizeof(DISPLAY_ATTRIBUTES) / (sizeof(DISPLAY_ATTRIBUTES[0]));

    DisplayAttributes *dpyAttr = &(mDisplayAttributes[disp]);

    for (int i = 0; i < NUM_DISPLAY_ATTRIBUTES -1; i++)
    {
        switch(attributes[i])
        {
            case HWC_DISPLAY_VSYNC_PERIOD:
                value[i] = dpyAttr->vsync_period;
                ALOGI("getDisplayAttributes: disp:%d vsync_period:%d", disp, dpyAttr->vsync_period);
                break;
            case HWC_DISPLAY_WIDTH:
                value[i] = dpyAttr->xres;
                ALOGI("getDisplayAttributes: disp:%d width:%d", disp, dpyAttr->xres);
                break;
            case HWC_DISPLAY_HEIGHT:
                value[i] = dpyAttr->yres;
                ALOGI("getDisplayAttributes: disp:%d height:%d", disp, dpyAttr->yres);
                break;
            case HWC_DISPLAY_DPI_X:
                value[i] = (int32_t)(dpyAttr->xdpi);
                ALOGI("getDisplayAttributes: disp:%d xdpi:%f", disp, dpyAttr->xdpi / 1000.0);
                break;
            case HWC_DISPLAY_DPI_Y:
                value[i] = (int32_t)(dpyAttr->ydpi);
                ALOGI("getDisplayAttributes: disp:%d ydpi:%f", disp, dpyAttr->ydpi / 1000.0);
                break;
            default:
                ALOGE("Unknown Display Attributes:%d", attributes[i]);
                return -EINVAL;
        }
    }

    return 0;
}

void SprdHWComposer:: registerProcs(hwc_procs_t const* procs)
{

    /*
     *  At present, the Android callback procs is just
     *  used by Primary Display Device.
     *  Other Display Device do not generate vsync event.
     * */
    mPrimaryDisplay->setVsyncEventProcs(const_cast<hwc_procs_t *>(procs));
}

bool SprdHWComposer:: eventControl(int disp, int enabled)
{

    /*
     *  At present, only Primary Display Device support
     *  Hardware vsync event.
     * */
    mPrimaryDisplay->eventControl(enabled);

    return true;
}





/*
 *  HWC module info
 * */

static int hwc_eventControl(hwc_composer_device_1* dev, int disp, int event, int enabled)
{
    int status = -EINVAL;
    bool ret = false;

    SprdHWComposer *HWC = static_cast<SprdHWComposer*>(dev);
    if (HWC == NULL)
    {
        ALOGE("Can NOT get SprdHWComposer reference");
        return status;
    }

    switch(event)
    {
        case HWC_EVENT_VSYNC:
            ret = HWC->eventControl(disp, enabled);
            if (!ret)
            {
                ALOGE("Vsync event control failed");
                status = -EPERM;
                return status;
            }
            break;

        default:
            ALOGE("unsupported event");
            status = -EPERM;
            return status;
    }

    return 0;
}

static int hwc_blank(hwc_composer_device_1 *dev, int disp, int blank)
{
    int status = -EINVAL;

    SprdHWComposer *HWC = static_cast<SprdHWComposer*>(dev);
    if (HWC == NULL)
    {
        ALOGE("Can NOT get SprdHWComposer reference");
        return status;
    }

    status = HWC->blank(disp, blank);

    return status;
}

static int hwc_query(hwc_composer_device_1 *dev, int what, int* value)
{
    int status = -EINVAL;

    SprdHWComposer *HWC = static_cast<SprdHWComposer*>(dev);
    if (HWC == NULL)
    {
        ALOGE("Can NOT get SprdHWComposer reference");
        return status;
    }

    status = HWC->query(what, value);

    return status;
}

static void hwc_dump(hwc_composer_device_1 *dev, char *buff, int buff_len)
{
    SprdHWComposer *HWC = static_cast<SprdHWComposer*>(dev);
    if (HWC == NULL)
    {
        ALOGE("Can NOT get SprdHWComposer reference");
        return ;
    }

    HWC->dump(buff, buff_len);
}

static int hwc_getDisplayConfigs(hwc_composer_device_1 *dev, int disp, uint32_t* configs, size_t* numConfigs)
{
    int status = -EINVAL;

    SprdHWComposer *HWC = static_cast<SprdHWComposer*>(dev);
    if (HWC == NULL)
    {
        ALOGE("Can NOT get SprdHWComposer reference");
        return status;
    }

    status = HWC->getDisplayConfigs(disp, configs, numConfigs);

    return status;
}

static int hwc_getDisplayAttributes(hwc_composer_device_1 *dev, int disp, uint32_t config, const uint32_t* attributes, int32_t* value)
{
    int status = -EINVAL;

    SprdHWComposer *HWC = static_cast<SprdHWComposer*>(dev);
    if (HWC == NULL)
    {
        ALOGE("Can NOT get SprdHWComposer reference");
        return status;
    }

    status = HWC->getDisplayAttributes(disp, config, attributes, value);

    return status;
}

static int hwc_prepareDisplays(hwc_composer_device_1 *dev, size_t numDisplays, hwc_display_contents_1_t **displays)
{
    int status = -EINVAL;

    SprdHWComposer *HWC = static_cast<SprdHWComposer*>(dev);
    if (HWC == NULL)
    {
        ALOGE("Can NOT get SprdHWComposer reference");
        return status;
    }

    status = HWC->prepareDisplays(numDisplays, displays);

    return status;
}

static int hwc_setDisplays(hwc_composer_device_1 *dev, size_t numDisplays, hwc_display_contents_1_t **displays)
{
    int status = -EINVAL;

    SprdHWComposer *HWC = static_cast<SprdHWComposer*>(dev);
    if (HWC == NULL)
    {
        ALOGE("Can NOT get SprdHWComposer reference");
        return status;
    }

    status = HWC->commitDisplays(numDisplays, displays);

    return status;
}

static int hwc_device_close(struct hw_device_t *dev)
{
    SprdHWComposer *HWC = (SprdHWComposer*)(dev);
    if (HWC != NULL)
    {
        delete HWC;
        HWC = NULL;
    }

    return 0;
};

static void hwc_registerProcs(hwc_composer_device_1 *dev, hwc_procs_t const* procs)
{
    SprdHWComposer *HWC = static_cast<SprdHWComposer*>(dev);
    if (HWC == NULL)
    {
        ALOGE("Can NOT get SprdHWComposer reference");
        return;
    }

    HWC->registerProcs(procs);
}

static int hwc_device_open(const struct hw_module_t* module, const char* name, struct hw_device_t** device)
{
    int status = -EINVAL;

    if (strcmp(name, HWC_HARDWARE_COMPOSER))
    {
        ALOGE("The module name is not HWC_HARDWARE_COMPOSER");
        return status;
    }

    SprdHWComposer *HWC = new SprdHWComposer();
    if (HWC == NULL) {
        ALOGE("Can NOT create SprdHWComposer object");
        status = -ENOMEM;
        return status;
    }

    bool ret = HWC->Init();
    if (!ret)
    {
        ALOGE("Init HWComposer failed");
        delete HWC;
        HWC = NULL;
        status = -ENOMEM;
        return status;
    }

    HWC->hwc_composer_device_1_t::common.tag = HARDWARE_DEVICE_TAG;
    HWC->hwc_composer_device_1_t::common.version = HWC_DEVICE_API_VERSION_1_3;
    HWC->hwc_composer_device_1_t::common.module = const_cast<hw_module_t*>(module);
    HWC->hwc_composer_device_1_t::common.close = hwc_device_close;

    HWC->hwc_composer_device_1_t::prepare = hwc_prepareDisplays;
    HWC->hwc_composer_device_1_t::set = hwc_setDisplays;
    HWC->hwc_composer_device_1_t::eventControl = hwc_eventControl;
    HWC->hwc_composer_device_1_t::blank = hwc_blank;
    HWC->hwc_composer_device_1_t::query = hwc_query;
    HWC->hwc_composer_device_1_t::registerProcs = hwc_registerProcs;
    HWC->hwc_composer_device_1_t::dump = hwc_dump;
    HWC->hwc_composer_device_1_t::getDisplayConfigs = hwc_getDisplayConfigs;
    HWC->hwc_composer_device_1_t::getDisplayAttributes = hwc_getDisplayAttributes;

    *device = &HWC->hwc_composer_device_1_t::common;

    status = 0;

    return status;
}

static struct hw_module_methods_t hwc_module_methods = {
    open: hwc_device_open
};

hwc_module_t HAL_MODULE_INFO_SYM = {
    common: {
        tag: HARDWARE_MODULE_TAG,
        version_major: 2,
        version_minor: 0,
        id: HWC_HARDWARE_MODULE_ID,
        name: "SPRD HWComposer Module",
        author: "The Android Open Source Project",
        methods: &hwc_module_methods,
        dso: 0,
        reserved: {0},
    }
};
