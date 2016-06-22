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

#include <cutils/log.h>
#include <linux/fb.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/cdefs.h>
#include <sys/types.h>

#include <system/graphics.h>
#include "gralloc_priv.h"
#include "gsp_hal.h"
//#include "scale_rotate.h"


static int debugenable = 0;


static int32_t gsp_hal_layer0_params_check (GSP_LAYER0_CONFIG_INFO_T *layer0_info)
{
    float coef_factor_w = 0.0;
    float coef_factor_h = 0.0;
    uint32_t pixel_cnt = 0x1000000;//max 16M pixel

    if(layer0_info->layer_en == 0) {
        return GSP_NO_ERR;
    }

    if(layer0_info->clip_rect.st_x & 0x1
            ||layer0_info->clip_rect.st_y & 0x1
            ||layer0_info->clip_rect.rect_w & 0x1
            ||layer0_info->clip_rect.rect_h & 0x1
            ||layer0_info->des_rect.st_x & 0x1
            ||layer0_info->des_rect.st_y & 0x1
            ||layer0_info->des_rect.rect_w & 0x1
            ||layer0_info->des_rect.rect_h & 0x1) {
        ALOGE("param check err: Line:%d\n", __LINE__);
        return GSP_HAL_PARAM_CHECK_ERR;
    }

    //source check
    if((layer0_info->pitch & 0xfffff000UL)// pitch > 4095
            ||((layer0_info->clip_rect.st_x + layer0_info->clip_rect.rect_w) > layer0_info->pitch) //
            ||((layer0_info->clip_rect.st_y + layer0_info->clip_rect.rect_h) & 0xfffff000UL) // > 4095
      ) {
        ALOGE("param check err: Line:%d\n", __LINE__);
        return GSP_HAL_PARAM_CHECK_ERR;
    }
    //destination check
    if(((layer0_info->des_rect.st_x + layer0_info->des_rect.rect_w) & 0xfffff000UL) // > 4095
            ||((layer0_info->des_rect.st_y + layer0_info->des_rect.rect_h) & 0xfffff000UL) // > 4095
      ) {

        ALOGE("param check err: Line:%d\n", __LINE__);
        return GSP_HAL_PARAM_CHECK_ERR;
    }

    if(layer0_info->rot_angle >= GSP_ROT_ANGLE_MAX_NUM) {

        ALOGE("param check err: Line:%d\n", __LINE__);
        return GSP_HAL_PARAM_CHECK_ERR;
    }

    //scaling range check
    if(layer0_info->rot_angle == GSP_ROT_ANGLE_90
            ||layer0_info->rot_angle == GSP_ROT_ANGLE_270
            ||layer0_info->rot_angle == GSP_ROT_ANGLE_90_M
            ||layer0_info->rot_angle == GSP_ROT_ANGLE_270_M) {
        coef_factor_w = layer0_info->clip_rect.rect_h*1.0/layer0_info->des_rect.rect_w;
        coef_factor_h = layer0_info->clip_rect.rect_w*1.0/layer0_info->des_rect.rect_h;
        //coef_factor_w = CEIL(layer0_info->clip_rect.rect_h*100,layer0_info->des_rect.rect_w);
        //coef_factor_h = CEIL(layer0_info->clip_rect.rect_w*100,layer0_info->des_rect.rect_h);
    } else {
        coef_factor_w = layer0_info->clip_rect.rect_w*1.0/layer0_info->des_rect.rect_w;
        coef_factor_h = layer0_info->clip_rect.rect_h*1.0/layer0_info->des_rect.rect_h;
        //coef_factor_w = CEIL(layer0_info->clip_rect.rect_w*100,layer0_info->des_rect.rect_w);
        //coef_factor_h = CEIL(layer0_info->clip_rect.rect_h*100,layer0_info->des_rect.rect_h);
    }
    if(coef_factor_w < 0.25 //larger than 4 times
            ||coef_factor_h < 0.25 //larger than 4 times
            ||coef_factor_w > 16.0 //smaller than 1/16
            ||coef_factor_h > 16.0 //smaller than 1/16
            ||(coef_factor_w > 1.0 && coef_factor_h < 1.0) //one direction scaling down, the other scaling up
            ||(coef_factor_h > 1.0 && coef_factor_w < 1.0) //one direction scaling down, the other scaling up
      ) {
        ALOGE("param check err: (%dx%d)-Rot:%d->(%dx%d),Line:%d\n",
              layer0_info->clip_rect.rect_w,
              layer0_info->clip_rect.rect_h,
              layer0_info->rot_angle,
              layer0_info->des_rect.rect_w,
              layer0_info->des_rect.rect_h,
              __LINE__);

        ALOGE("param check err: coef_factor_w:%f,coef_factor_h:%f,Line:%d\n",coef_factor_w,coef_factor_h,__LINE__);
        return GSP_HAL_PARAM_CHECK_ERR;
    }

    /*
        //source buffer size check
        pixel_cnt = (layer0_info->clip_rect.st_y + layer0_info->clip_rect.rect_h) * layer0_info->pitch;
        switch(layer0_info->img_format)
        {
        case GSP_SRC_FMT_ARGB888:
        case GSP_SRC_FMT_RGB888:
        {
            //y buffer check
            if(pixel_cnt*4 > (GSP_IMG_SRC1_ADDR_Y - GSP_IMG_SRC0_ADDR_Y))
            {
                GSP_ASSERT();
            }
        }
        break;
        case GSP_SRC_FMT_ARGB565:
        {
            //alpha buffer check
            if(pixel_cnt > GSP_IMG_SRC0_ADDR_AV_SIZE)
            {
                GSP_ASSERT();
            }
        }
        case GSP_SRC_FMT_RGB565:
        {
            //y buffer check
            if(pixel_cnt*2 > (GSP_IMG_SRC0_ADDR_AV - GSP_IMG_SRC0_ADDR_Y))
            {
                GSP_ASSERT();
            }
        }
        break;
        case GSP_SRC_FMT_YUV420_2P:
        {
            //y buffer check
            if(pixel_cnt > GSP_IMG_SRC0_ADDR_Y_SIZE)
            {
                GSP_ASSERT();
            }
            //uv buffer check
            if(pixel_cnt/2 > GSP_IMG_SRC0_ADDR_UV_SIZE)
            {
                GSP_ASSERT();
            }
        }
        break;
        case GSP_SRC_FMT_YUV420_3P:
        {
            //y buffer check
            if(pixel_cnt > GSP_IMG_SRC0_ADDR_Y_SIZE)
            {
                GSP_ASSERT();
            }
            //u buffer check
            if(pixel_cnt/4 > GSP_IMG_SRC0_ADDR_UV_SIZE)
            {
                GSP_ASSERT();
            }
            //v buffer check
            if(pixel_cnt/4 > GSP_IMG_SRC0_ADDR_AV_SIZE)
            {
                GSP_ASSERT();
            }
        }
        break;
        case GSP_SRC_FMT_YUV400_1P:
        {
            //y buffer check
            if(pixel_cnt > GSP_IMG_SRC0_ADDR_Y_SIZE)
            {
                GSP_ASSERT();
            }
        }
        break;
        case GSP_SRC_FMT_YUV422_2P:
        {
            //y buffer check
            if(pixel_cnt > GSP_IMG_SRC0_ADDR_Y_SIZE)
            {
                GSP_ASSERT();
            }
            //uv buffer check
            if(pixel_cnt > GSP_IMG_SRC0_ADDR_UV_SIZE)
            {
                GSP_ASSERT();
            }
        }
        break;
        case GSP_SRC_FMT_8BPP:
        {
            //alpha buffer check
            if(pixel_cnt > GSP_IMG_SRC0_ADDR_Y_SIZE)
            {
                GSP_ASSERT();
            }
        }
        break;
        default:
            GSP_ASSERT();
            break;

        }
    */
    return GSP_NO_ERR;
}


static int32_t gsp_hal_layer1_params_check(GSP_LAYER1_CONFIG_INFO_T *layer1_info)
{
    uint32_t pixel_cnt = 0x1000000;//max 16M pixel

    if(layer1_info->layer_en == 0) {
        return GSP_NO_ERR;
    }

    if(layer1_info->clip_rect.st_x & 0x1
            ||layer1_info->clip_rect.st_y & 0x1
            ||layer1_info->clip_rect.rect_w & 0x1
            ||layer1_info->clip_rect.rect_h & 0x1
            ||layer1_info->des_pos.pos_pt_x & 0x1
            ||layer1_info->des_pos.pos_pt_y & 0x1) {
        ALOGE("param check err: Line:%d\n", __LINE__);
        return GSP_HAL_PARAM_CHECK_ERR;
    }

    //source check
    if( (layer1_info->pitch & 0xf000UL)// pitch > 4095
            ||((layer1_info->clip_rect.st_x + layer1_info->clip_rect.rect_w) > layer1_info->pitch) //
            ||((layer1_info->clip_rect.st_y + layer1_info->clip_rect.rect_h) & 0xfffff000UL) // > 4095
      ) {

        ALOGE("param check err: Line:%d\n", __LINE__);
        return GSP_HAL_PARAM_CHECK_ERR;
    }

    if(layer1_info->rot_angle >= GSP_ROT_ANGLE_MAX_NUM) {

        ALOGE("param check err: Line:%d\n", __LINE__);
        return GSP_HAL_PARAM_CHECK_ERR;
    }

    /*
        //source buffer size check
        pixel_cnt = (layer1_info->clip_rect.st_y + layer1_info->clip_rect.rect_h) * layer1_info->pitch;
        switch(layer1_info->img_format)
        {
        case GSP_SRC_FMT_ARGB888:
        case GSP_SRC_FMT_RGB888:
        {
            //y buffer check
            if(pixel_cnt*4 > (GSP_IMG_DST_ADDR_Y - GSP_IMG_SRC1_ADDR_Y))
            {
                GSP_ASSERT();
            }
        }
        break;
        case GSP_SRC_FMT_ARGB565:
        {
            //alpha buffer check
            if(pixel_cnt > GSP_IMG_SRC1_ADDR_AV_SIZE)
            {
                GSP_ASSERT();
            }
        }
        case GSP_SRC_FMT_RGB565:
        {
            //y buffer check
            if(pixel_cnt*2 > (GSP_IMG_SRC1_ADDR_AV - GSP_IMG_SRC1_ADDR_Y))
            {
                GSP_ASSERT();
            }
        }
        break;
        case GSP_SRC_FMT_YUV420_2P:
        {
            //y buffer check
            if(pixel_cnt > GSP_IMG_SRC1_ADDR_Y_SIZE)
            {
                GSP_ASSERT();
            }
            //uv buffer check
            if(pixel_cnt/2 > GSP_IMG_SRC1_ADDR_UV_SIZE)
            {
                GSP_ASSERT();
            }
        }
        break;
        case GSP_SRC_FMT_YUV420_3P:
        {
            //y buffer check
            if(pixel_cnt > GSP_IMG_SRC1_ADDR_Y_SIZE)
            {
                GSP_ASSERT();
            }
            //u buffer check
            if(pixel_cnt/4 > GSP_IMG_SRC1_ADDR_UV_SIZE)
            {
                GSP_ASSERT();
            }
            //v buffer check
            if(pixel_cnt/4 > GSP_IMG_SRC1_ADDR_AV_SIZE)
            {
                GSP_ASSERT();
            }
        }
        break;
        case GSP_SRC_FMT_YUV400_1P:
        {
            //y buffer check
            if(pixel_cnt > GSP_IMG_SRC1_ADDR_Y_SIZE)
            {
                GSP_ASSERT();
            }
        }
        break;
        case GSP_SRC_FMT_YUV422_2P:
        {
            //y buffer check
            if(pixel_cnt > GSP_IMG_SRC1_ADDR_Y_SIZE)
            {
                GSP_ASSERT();
            }
            //uv buffer check
            if(pixel_cnt > GSP_IMG_SRC1_ADDR_UV_SIZE)
            {
                GSP_ASSERT();
            }
        }
        break;
        case GSP_SRC_FMT_8BPP:
        {
            //alpha buffer check
            if(pixel_cnt > GSP_IMG_SRC1_ADDR_Y_SIZE)
            {
                GSP_ASSERT();
            }
        }
        break;
        default:
            GSP_ASSERT();
            break;

        }
        */
    return GSP_NO_ERR;
}
static int32_t gsp_hal_misc_params_check(GSP_CONFIG_INFO_T *gsp_cfg_info)
{
    if((gsp_cfg_info->misc_info.gsp_clock & (~3))
            ||(gsp_cfg_info->misc_info.ahb_clock & (~3))) {
        ALOGE("param check err: gsp_clock or ahb_clock larger than 3! Line:%d\n", __LINE__);
        return GSP_HAL_PARAM_CHECK_ERR;
    }
    if(gsp_cfg_info->layer0_info.layer_en == 1 && gsp_cfg_info->layer0_info.pallet_en == 1
            && gsp_cfg_info->layer1_info.layer_en == 1 && gsp_cfg_info->layer1_info.pallet_en == 1) {
        ALOGE("param check err: both layer pallet are enable! Line:%d\n", __LINE__);
        return GSP_HAL_PARAM_CHECK_ERR;
    }
    return GSP_NO_ERR;
}

static int32_t gsp_hal_layerdes_params_check(GSP_CONFIG_INFO_T *gsp_cfg_info)
{
    uint32_t pixel_cnt = 0x1000000;//max 16M pixel
    uint32_t max_h0 = 4096;//max 4k
    uint32_t max_h1 = 4096;//max 4k
    uint32_t max_h = 4096;//max 4k

    GSP_LAYER0_CONFIG_INFO_T    *layer0_info = &gsp_cfg_info->layer0_info;
    GSP_LAYER1_CONFIG_INFO_T    *layer1_info = &gsp_cfg_info->layer1_info;
    GSP_LAYER_DES_CONFIG_INFO_T *layer_des_info = &gsp_cfg_info->layer_des_info;

    if((layer0_info->layer_en == 0) && (layer1_info->layer_en == 0)) {

        ALOGE("param check err: Line:%d\n", __LINE__);
        return GSP_HAL_PARAM_CHECK_ERR;
    }

    if((layer_des_info->pitch & 0xfffff000UL)) { // des pitch > 4095

        ALOGE("param check err: Line:%d\n", __LINE__);
        return GSP_HAL_PARAM_CHECK_ERR;
    }
    if(layer0_info->layer_en == 1) {
        if((layer0_info->des_rect.st_x + layer0_info->des_rect.rect_w) > layer_des_info->pitch) {

            ALOGE("param check err: Line:%d\n", __LINE__);
            return GSP_HAL_PARAM_CHECK_ERR;
        }
    }

    if(layer1_info->layer_en == 1) {
        if((layer1_info->des_pos.pos_pt_x + layer1_info->clip_rect.rect_w > layer_des_info->pitch)
                &&(layer1_info->rot_angle == GSP_ROT_ANGLE_0
                   ||layer1_info->rot_angle == GSP_ROT_ANGLE_180
                   ||layer1_info->rot_angle == GSP_ROT_ANGLE_0_M
                   ||layer1_info->rot_angle == GSP_ROT_ANGLE_180_M)) {

            ALOGE("param check err: Line:%d\n", __LINE__);
            return GSP_HAL_PARAM_CHECK_ERR;
        } else if((layer1_info->des_pos.pos_pt_x + layer1_info->clip_rect.rect_h > layer_des_info->pitch)
                  &&(layer1_info->rot_angle == GSP_ROT_ANGLE_90
                     ||layer1_info->rot_angle == GSP_ROT_ANGLE_270
                     ||layer1_info->rot_angle == GSP_ROT_ANGLE_90_M
                     ||layer1_info->rot_angle == GSP_ROT_ANGLE_270_M)) {

            ALOGE("param check err: Line:%d\n", __LINE__);
            return GSP_HAL_PARAM_CHECK_ERR;
        }
    }

    if((GSP_DST_FMT_YUV420_2P <= layer_des_info->img_format) && (layer_des_info->img_format <= GSP_DST_FMT_YUV422_2P)) { //des color is yuv
        if((layer0_info->des_rect.st_x & 0x01)
                ||(layer0_info->des_rect.st_y & 0x01)
                ||(layer1_info->des_pos.pos_pt_x & 0x01)
                ||(layer1_info->des_pos.pos_pt_y & 0x01)) { //des start point at odd address

            ALOGE("param check err: Line:%d\n", __LINE__);
            return GSP_HAL_PARAM_CHECK_ERR;
        }
    }

    if(layer_des_info->compress_r8_en == 1
            && layer_des_info->img_format != GSP_DST_FMT_RGB888) {

        ALOGE("param check err: Line:%d\n", __LINE__);
        return GSP_HAL_PARAM_CHECK_ERR;
    }
    /*
        //destination buffer size check
        max_h0 = layer0_info->des_rect.st_y + layer0_info->des_rect.rect_h;
        if((layer1_info->clip_rect.rect_w > layer1_info->clip_rect.rect_h)
                && (layer1_info->rot_angle == GSP_ROT_ANGLE_90
                    ||layer1_info->rot_angle == GSP_ROT_ANGLE_270
                    ||layer1_info->rot_angle == GSP_ROT_ANGLE_90_M
                    ||layer1_info->rot_angle == GSP_ROT_ANGLE_270_M))
        {
            max_h1 = layer1_info->des_pos.pos_pt_y + layer1_info->clip_rect.rect_w;
        }
        else
        {
            max_h1 = layer1_info->des_pos.pos_pt_y + layer1_info->clip_rect.rect_h;
        }
        max_h = (max_h0 > max_h1)?max_h0:max_h1;
        pixel_cnt = max_h * layer_des_info->pitch;

        switch(layer_des_info->img_format)
        {
        case GSP_DST_FMT_ARGB888:
        case GSP_DST_FMT_RGB888:
        {
            //y buffer check
            if(pixel_cnt*4 > (GSP_IMG_DST_ADDR_END - GSP_IMG_DST_ADDR_Y))
            {
                GSP_ASSERT();
            }
        }
        break;
        case GSP_DST_FMT_ARGB565:
        {
            //alpha buffer check
            if(pixel_cnt > GSP_IMG_DST_ADDR_AV_SIZE)
            {
                GSP_ASSERT();
            }
        }
        case GSP_DST_FMT_RGB565:
        {
            //y buffer check
            if(pixel_cnt*2 > (GSP_IMG_DST_ADDR_AV - GSP_IMG_DST_ADDR_Y))
            {
                GSP_ASSERT();
            }
        }
        break;
        case GSP_DST_FMT_YUV420_2P:
        {
            //y buffer check
            if(pixel_cnt > GSP_IMG_DST_ADDR_Y_SIZE)
            {
                GSP_ASSERT();
            }
            //uv buffer check
            if(pixel_cnt/2 > GSP_IMG_DST_ADDR_UV_SIZE)
            {
                GSP_ASSERT();
            }
        }
        break;
        case GSP_DST_FMT_YUV420_3P:
        {
            //y buffer check
            if(pixel_cnt > GSP_IMG_DST_ADDR_Y_SIZE)
            {
                GSP_ASSERT();
            }
            //u buffer check
            if(pixel_cnt/4 > GSP_IMG_DST_ADDR_UV_SIZE)
            {
                GSP_ASSERT();
            }
            //v buffer check
            if(pixel_cnt/4 > GSP_IMG_DST_ADDR_AV_SIZE)
            {
                GSP_ASSERT();
            }
        }
        break;
        case GSP_DST_FMT_YUV422_2P:
        {
            //y buffer check
            if(pixel_cnt > GSP_IMG_DST_ADDR_Y_SIZE)
            {
                GSP_ASSERT();
            }
            //uv buffer check
            if(pixel_cnt > GSP_IMG_DST_ADDR_UV_SIZE)
            {
                GSP_ASSERT();
            }
        }
        break;
        default:
            GSP_ASSERT();
            break;
        }
        */
    return GSP_NO_ERR;
}

/*
func:gsp_hal_params_check
desc:check gsp config params before config to kernel
return:0 means success,other means failed
*/
static int32_t gsp_hal_params_check(GSP_CONFIG_INFO_T *gsp_cfg_info)
{
    if(gsp_hal_layer0_params_check(&gsp_cfg_info->layer0_info)
            ||gsp_hal_layer1_params_check(&gsp_cfg_info->layer1_info)
            ||gsp_hal_misc_params_check(gsp_cfg_info)
            ||gsp_hal_layerdes_params_check(gsp_cfg_info)) {
        return GSP_HAL_PARAM_CHECK_ERR;
    }
    return GSP_NO_ERR;
}


/*
func:gsp_hal_open
desc:open GSP device
return: -1 means failed,other success
notes: a thread can't open the device again unless it close first
*/
int32_t gsp_hal_open(void)
{
    int32_t gsp_fd = -1;

    gsp_fd = open("/dev/sprd_gsp", O_RDWR, 0);
    if (-1 == gsp_fd) {
        ALOGE("open gsp device failed! Line:%d \n", __LINE__);
    }

    return gsp_fd;
}


/*
func:gsp_hal_close
desc:close GSP device
return: -1 means failed,0 success
notes:
*/
int32_t gsp_hal_close(int32_t gsp_fd)
{
    if(gsp_fd == -1) {
        return GSP_HAL_PARAM_ERR;
    }

    if (close(gsp_fd)) {
        if (close(gsp_fd)) {
            ALOGE("gsp_rotation err : close gsp_fd . Line:%d \n", __LINE__);
            return -1;
        }
    }

    return 0;
}

/*
func:gsp_hal_config
desc:set GSP device config parameters
return: -1 means failed,0 success
notes:
*/
int32_t gsp_hal_config(int32_t gsp_fd,GSP_CONFIG_INFO_T *gsp_cfg_info)
{
    int32_t ret = 0;

    if(gsp_fd == -1) {
        return GSP_HAL_PARAM_ERR;
    }

    //software params check
    ret = gsp_hal_params_check(gsp_cfg_info);
    if(ret) {
        ALOGE("gsp param check err,exit without config gsp reg: Line:%d\n", __LINE__);
        return ret;
    }

    ret = ioctl(gsp_fd, GSP_IO_SET_PARAM, gsp_cfg_info);
    if(0 == ret) { //gsp hw check params err
        ALOGI_IF(debugenable,"gsp set params ok, trigger now. Line:%d \n", __LINE__);
        //ALOGI_IF(debugenable,"gsp set params ok, trigger now. Line:%d \n", __LINE__);
    } else {
        ALOGE("hwcomposer gsp set params err:%d  . Line:%d \n",ret, __LINE__);
        //ret = -1;
    }
    return ret;
}



/*
func:gsp_hal_trigger
desc:trigger GSP to run
return: -1 means failed,0 success
notes:
*/
int32_t gsp_hal_trigger(int32_t gsp_fd)
{
    int32_t ret = GSP_NO_ERR;

    if(gsp_fd == -1) {
        return GSP_HAL_PARAM_ERR;
    }

    ret = ioctl(gsp_fd, GSP_IO_TRIGGER_RUN, 1);
    if(0 == ret) {
        //ALOGE("gsp trigger ok, Line:%d \n", __LINE__);
        ALOGI_IF(debugenable,"gsp trigger ok, Line:%d \n", __LINE__);
    } else {
        ALOGE("gsp trigger err:%d  . Line:%d \n",ret, __LINE__);
        //ret = -1;
    }

    return ret;
}


/*
func:gsp_hal_waitdone
desc:wait GSP finish
return: -1 means thread interrupt by signal,0 means GSP done successfully
notes:
*/
int32_t gsp_hal_waitdone(int32_t gsp_fd)
{
    int32_t ret = GSP_NO_ERR;

    if(gsp_fd == -1) {
        return GSP_HAL_PARAM_ERR;
    }

    ret = ioctl(gsp_fd, GSP_IO_WAIT_FINISH, 1);
    if(0 == ret) {
        //ALOGE("gsp wait finish ok, return. Line:%d \n", __LINE__);
        ALOGI_IF(debugenable,"gsp wait finish ok, return. Line:%d \n", __LINE__);
    } else {
        ALOGE("gsp wait finish err:%d  . Line:%d\n",ret, __LINE__);
        //ret = -1;
    }

    return ret;
}


/*
func:GSP_Proccess
desc:all the GSP function can be complete in this function, such as CFC,scaling,blend,rotation and mirror,clipping.
note:1 the source and destination image buffer should be physical-coherent memory space,
       the caller should ensure the in and out buffer size is large enough, or the GSP will access cross the buffer border,
       these two will rise unexpected exception.
     2 this function will be block until GSP process over.
return: 0 success, other err
*/
int32_t GSP_Proccess(GSP_CONFIG_INFO_T *pgsp_cfg_info)
{
    int32_t ret = 0;
    int32_t gsp_fd = -1;

    gsp_fd = gsp_hal_open();
    if(-1 == gsp_fd) {
        ALOGE("%s:%d,opend gsp failed \n", __func__, __LINE__);
        return GSP_HAL_PARAM_ERR;
    }

    ret = gsp_hal_config(gsp_fd,pgsp_cfg_info);
    if(0 != ret) {
        ALOGE("%s:%d,cfg gsp failed \n", __func__, __LINE__);
        goto exit;
    }
    ret = gsp_hal_trigger(gsp_fd);
    if(0 != ret) {
        ALOGE("%s:%d,trigger gsp failed \n", __func__, __LINE__);
        goto exit;
    }

    ret = gsp_hal_waitdone(gsp_fd);
    if(0 != ret) {
        ALOGE("%s:%d,wait done gsp failed \n", __func__, __LINE__);
        goto exit;
    }


exit:
    //ret = gsp_hal_close(gsp_fd);
    gsp_hal_close(gsp_fd);
    return ret;

}


#if 1//ndef _HWCOMPOSER_USE_GSP_BLEND
/*
GSP is used to copy osd and scaling video, DDR data band need much more than sc8830 can provide,
result in dispC underflow!!
*/



/*
func:HAL2Kernel_RotSrcColorFormatConvert
*/
static GSP_LAYER_SRC_DATA_FMT_E HAL2Kernel_RotSrcColorFormatConvert(HW_ROTATION_DATA_FORMAT_E data_format)
{
    switch(data_format) {
    case HW_ROTATION_DATA_YUV422:
        return GSP_SRC_FMT_YUV422_2P;
        break;
    case HW_ROTATION_DATA_YUV420:
        return GSP_SRC_FMT_YUV420_2P;
        break;
    case HW_ROTATION_DATA_RGB888:
        return GSP_SRC_FMT_RGB888;
        break;
    case HW_ROTATION_DATA_RGB565:
        return GSP_SRC_FMT_RGB565;
        break;
    case HW_ROTATION_DATA_YUV400:
        return GSP_SRC_FMT_YUV400_1P;
        break;
    case HW_ROTATION_DATA_RGB555:
    case HW_ROTATION_DATA_RGB666:
    default:
        return GSP_SRC_FMT_MAX_NUM;
        break;
    }
    return GSP_SRC_FMT_MAX_NUM;
}

/*
func:HAL2Kernel_DesColorFormatConvert
*/
static GSP_LAYER_DST_DATA_FMT_E HAL2Kernel_RotDesColorFormatConvert(HW_ROTATION_DATA_FORMAT_E data_format)
{
    switch(data_format) {
    case HW_ROTATION_DATA_YUV422:
        return GSP_DST_FMT_YUV422_2P;
        break;
    case HW_ROTATION_DATA_YUV420:
        return GSP_DST_FMT_YUV420_2P;
        break;
    case HW_ROTATION_DATA_RGB888:
        return GSP_DST_FMT_RGB888;
        break;
    case HW_ROTATION_DATA_RGB565:
        return GSP_DST_FMT_RGB565;
        break;
    case HW_ROTATION_DATA_YUV400:
    case HW_ROTATION_DATA_RGB555:
    case HW_ROTATION_DATA_RGB666:
    default:
        return GSP_DST_FMT_MAX_NUM;
        break;
    }
    return GSP_DST_FMT_MAX_NUM;
}



static GSP_ROT_ANGLE_E HAL2Kernel_RotMirrConvert(int degree)
{
    GSP_ROT_ANGLE_E result_degree = GSP_ROT_ANGLE_MAX_NUM;

    switch(degree) {
    case -1:
        result_degree = GSP_ROT_ANGLE_0_M;
        break;
    case 90:
        result_degree = GSP_ROT_ANGLE_90;
        break;
    case 180:
        result_degree = GSP_ROT_ANGLE_180;
        break;
    case 270:
        result_degree = GSP_ROT_ANGLE_270;
        break;

    case -90:
        result_degree = GSP_ROT_ANGLE_90_M;
        break;
    case -180:
        result_degree = GSP_ROT_ANGLE_180_M;
        break;
    case -270:
        result_degree = GSP_ROT_ANGLE_270_M;
        break;
    default:
        ALOGE("Camera_rotation err : angle %d not supported. Line:%d ", degree, __LINE__);
    case 0:
        result_degree = GSP_ROT_ANGLE_0;
        break;
    }
    return result_degree;
}


/*
func:HAL2Kernel_RotSrcColorFormatConvert
*/
static GSP_LAYER_SRC_DATA_FMT_E HAL2Kernel_ScaleSrcColorFormatConvert(HW_SCALE_DATA_FORMAT_E input_fmt)
{
    switch(input_fmt) {
    case HW_SCALE_DATA_YUV422:
        return GSP_SRC_FMT_YUV422_2P;
        break;
    case HW_SCALE_DATA_YUV420:
        return GSP_SRC_FMT_YUV420_2P;
        break;
    case HW_SCALE_DATA_YUV420_3FRAME:
        return GSP_SRC_FMT_YUV420_3P;
        break;
    case HW_SCALE_DATA_RGB888:
        return GSP_SRC_FMT_RGB888;
        break;
    case HW_SCALE_DATA_RGB565:
        return GSP_SRC_FMT_RGB565;
        break;
    case HW_SCALE_DATA_YUV400:
        return GSP_SRC_FMT_YUV400_1P;
        break;
    default:
        return GSP_SRC_FMT_MAX_NUM;
        break;
    }
    return GSP_SRC_FMT_MAX_NUM;
}

/*
func:HAL2Kernel_ScaleDesColorFormatConvertsssss
*/
static GSP_LAYER_DST_DATA_FMT_E HAL2Kernel_ScaleDesColorFormatConvert(HW_SCALE_DATA_FORMAT_E output_fmt)
{
    switch(output_fmt) {
    case HW_SCALE_DATA_YUV422:
        return GSP_DST_FMT_YUV422_2P;
        break;
    case HW_SCALE_DATA_YUV420:
        return GSP_DST_FMT_YUV420_2P;
        break;
    case HW_SCALE_DATA_YUV420_3FRAME:
        return GSP_DST_FMT_YUV420_3P;
        break;
    case HW_SCALE_DATA_RGB888:
        return GSP_DST_FMT_RGB888;
        break;
    case HW_SCALE_DATA_RGB565:
        return GSP_DST_FMT_RGB565;
        break;
    case HW_SCALE_DATA_YUV400:
    default:
        return GSP_DST_FMT_MAX_NUM;
        break;
    }
    return GSP_DST_FMT_MAX_NUM;
}


static GSP_ROT_ANGLE_E HAL2Kernel_ScaleAngleConvert(HW_ROTATION_MODE_E rotation)
{
    GSP_ROT_ANGLE_E result_degree = GSP_ROT_ANGLE_MAX_NUM;

    switch(rotation) {
    case HW_ROTATION_MIRROR:
        result_degree = GSP_ROT_ANGLE_0_M;
        break;
    case HW_ROTATION_90:
        result_degree = GSP_ROT_ANGLE_90;
        break;
    case HW_ROTATION_180:
        result_degree = GSP_ROT_ANGLE_180;
        break;
    case HW_ROTATION_270:
        result_degree = GSP_ROT_ANGLE_270;
        break;
    default:
        result_degree = GSP_ROT_ANGLE_0;
        break;
    }
    return result_degree;
}


int camera_rotation_copy_data(uint32_t width, uint32_t height, uint32_t in_addr, uint32_t out_addr)
{
    int ret = GSP_NO_ERR;
    GSP_CONFIG_INFO_T gsp_cfg_info;
    memset(&gsp_cfg_info,0,sizeof(GSP_CONFIG_INFO_T));

    ALOGI_IF(debugenable,"<<%s:%d\n", __func__, __LINE__);

    gsp_cfg_info.layer0_info.clip_rect.rect_w = width;
    gsp_cfg_info.layer0_info.clip_rect.rect_h = height;
    gsp_cfg_info.layer0_info.img_format = GSP_SRC_FMT_ARGB888;// == ROT_RGB888 ?
    gsp_cfg_info.layer0_info.src_addr.addr_y = in_addr;
    gsp_cfg_info.layer0_info.src_addr.addr_v = gsp_cfg_info.layer0_info.src_addr.addr_uv = in_addr + width*height;
    gsp_cfg_info.layer0_info.pitch = width;
    gsp_cfg_info.layer0_info.layer_en = 1;

    //gsp_cfg_info.layer0_info.rot_angle = ?
    if(gsp_cfg_info.layer0_info.rot_angle == GSP_ROT_ANGLE_90
            ||gsp_cfg_info.layer0_info.rot_angle == GSP_ROT_ANGLE_270
            ||gsp_cfg_info.layer0_info.rot_angle == GSP_ROT_ANGLE_90_M
            ||gsp_cfg_info.layer0_info.rot_angle == GSP_ROT_ANGLE_270_M) {
        gsp_cfg_info.layer_des_info.pitch = height;
        gsp_cfg_info.layer0_info.des_rect.rect_w = height;
        gsp_cfg_info.layer0_info.des_rect.rect_h = width;
    } else {
        gsp_cfg_info.layer_des_info.pitch = width;
        gsp_cfg_info.layer0_info.des_rect.rect_w = width;
        gsp_cfg_info.layer0_info.des_rect.rect_h = height;
    }
    gsp_cfg_info.layer_des_info.img_format = GSP_DST_FMT_ARGB888;//?
    gsp_cfg_info.layer_des_info.src_addr.addr_y = out_addr;
    gsp_cfg_info.layer_des_info.src_addr.addr_v = gsp_cfg_info.layer_des_info.src_addr.addr_uv = out_addr + width*height;

    ret = GSP_Proccess(&gsp_cfg_info);

    ALOGI_IF(ret,"%s:%d,%s >>\n", __func__, __LINE__,ret?"failed":"success");
    return ret;

}

int camera_rotation_copy_data_from_virtual(uint32_t width, uint32_t height, uint32_t in_virtual_addr, uint32_t out_addr)
{
    ALOGE("%s:%d,gsp not support virtual address !\n", __func__, __LINE__);
    return -1;
}


int32_t camera_rotation(HW_ROTATION_DATA_FORMAT_E rot_format,
                        int32_t degree,
                        uint32_t width,
                        uint32_t height,
                        uint32_t in_addr,
                        uint32_t out_addr)
{
    int32_t ret = GSP_NO_ERR;
    GSP_CONFIG_INFO_T gsp_cfg_info;
    memset(&gsp_cfg_info,0,sizeof(GSP_CONFIG_INFO_T));

    ALOGI_IF(debugenable,"<<%s:%d, \n", __func__, __LINE__);

    gsp_cfg_info.layer0_info.img_format = HAL2Kernel_RotSrcColorFormatConvert(rot_format);
    gsp_cfg_info.layer0_info.rot_angle = HAL2Kernel_RotMirrConvert(degree);
    gsp_cfg_info.layer0_info.clip_rect.rect_w = width;
    gsp_cfg_info.layer0_info.clip_rect.rect_h = height;
    gsp_cfg_info.layer0_info.pitch = width;
    if(gsp_cfg_info.layer0_info.rot_angle == GSP_ROT_ANGLE_0
            ||gsp_cfg_info.layer0_info.rot_angle == GSP_ROT_ANGLE_180
            ||gsp_cfg_info.layer0_info.rot_angle == GSP_ROT_ANGLE_0_M
            ||gsp_cfg_info.layer0_info.rot_angle == GSP_ROT_ANGLE_180_M) {
        gsp_cfg_info.layer0_info.des_rect.rect_w = width;
        gsp_cfg_info.layer0_info.des_rect.rect_h = height;
    } else {
        gsp_cfg_info.layer0_info.des_rect.rect_w = height;
        gsp_cfg_info.layer0_info.des_rect.rect_h = width;
    }
    gsp_cfg_info.layer0_info.layer_en = 1;
    gsp_cfg_info.layer0_info.src_addr.addr_y = in_addr;
    gsp_cfg_info.layer0_info.src_addr.addr_v = gsp_cfg_info.layer0_info.src_addr.addr_uv = in_addr + width * height;

    gsp_cfg_info.layer_des_info.img_format = HAL2Kernel_RotDesColorFormatConvert(rot_format);
    gsp_cfg_info.layer_des_info.src_addr.addr_y = out_addr;
    gsp_cfg_info.layer_des_info.src_addr.addr_v = gsp_cfg_info.layer_des_info.src_addr.addr_uv = out_addr + width * height;
    gsp_cfg_info.layer_des_info.pitch = gsp_cfg_info.layer0_info.des_rect.rect_w;

    ret = GSP_Proccess(&gsp_cfg_info);

    ALOGI_IF(ret,"%s:%d,%s >>\n", __func__, __LINE__,ret?"failed":"success");
    return ret;
}


#ifndef USE_GPU_PROCESS_VIDEO

int transform_layer(uint32_t srcPhy,//src  phy_addr_y
                    uint32_t srcVirt,//src  virt_addr_y
                    uint32_t srcFormat,//src color
                    uint32_t transform,//rotation and mirror
                    uint32_t srcWidth,//src pitch
                    uint32_t srcHeight ,//src slice
                    uint32_t dstPhy ,//des  phy_addr_y
                    uint32_t dstVirt,//des  virt_addr_y
                    uint32_t dstFormat ,//des color
                    uint32_t dstWidth,//des pitch
                    uint32_t dstHeight , //des slice
                    struct sprd_rect *trim_rect ,
                    uint32_t tmp_phy_addr,
                    uint32_t tmp_vir_addr)
{
    int ret = GSP_NO_ERR;
    GSP_CONFIG_INFO_T gsp_cfg_info;
    memset(&gsp_cfg_info,0,sizeof(GSP_CONFIG_INFO_T));

    ALOGI_IF(debugenable,"<<%s L%d",__func__,__LINE__);

    switch(srcFormat) {
    case HAL_PIXEL_FORMAT_YCbCr_420_SP:
        gsp_cfg_info.layer0_info.img_format = GSP_SRC_FMT_YUV420_2P;
        gsp_cfg_info.layer0_info.endian_mode.uv_word_endn = GSP_WORD_ENDN_0;//GSP_WORD_ENDN_1--not correct
        break;
    case HAL_PIXEL_FORMAT_YCrCb_420_SP:
        gsp_cfg_info.layer0_info.img_format = GSP_SRC_FMT_YUV420_2P;//?
        gsp_cfg_info.layer0_info.endian_mode.uv_word_endn = GSP_WORD_ENDN_1;//?
        break;
    case HAL_PIXEL_FORMAT_YV12:
        gsp_cfg_info.layer0_info.img_format = GSP_SRC_FMT_YUV420_3P;//?
        break;
    default:
        return GSP_HAL_PARAM_CHECK_ERR;
    }
    switch(dstFormat) {
    case HAL_PIXEL_FORMAT_YCbCr_420_SP:
        gsp_cfg_info.layer_des_info.img_format = GSP_DST_FMT_YUV420_2P;
        gsp_cfg_info.layer_des_info.endian_mode.uv_word_endn = GSP_WORD_ENDN_0;
        break;
    default:
        return GSP_HAL_PARAM_CHECK_ERR;
    }

    gsp_cfg_info.layer0_info.des_rect.rect_w = dstWidth;
    gsp_cfg_info.layer0_info.des_rect.rect_h = dstHeight;

    switch(transform) {
    case 0:
        gsp_cfg_info.layer0_info.rot_angle = GSP_ROT_ANGLE_0;
        break;
    case HAL_TRANSFORM_FLIP_H:// 1
        gsp_cfg_info.layer0_info.rot_angle = GSP_ROT_ANGLE_180_M;
        break;
    case HAL_TRANSFORM_FLIP_V:// 2
        gsp_cfg_info.layer0_info.rot_angle = GSP_ROT_ANGLE_0_M;
        break;
    case HAL_TRANSFORM_ROT_180:// 3
        gsp_cfg_info.layer0_info.rot_angle = GSP_ROT_ANGLE_180;
        break;
    case HAL_TRANSFORM_ROT_90:// 4
    default:
        gsp_cfg_info.layer0_info.rot_angle = GSP_ROT_ANGLE_270;
        break;
    case (HAL_TRANSFORM_ROT_90|HAL_TRANSFORM_FLIP_H)://5
        gsp_cfg_info.layer0_info.rot_angle = GSP_ROT_ANGLE_90_M;
        break;
    case (HAL_TRANSFORM_ROT_90|HAL_TRANSFORM_FLIP_V)://6
        gsp_cfg_info.layer0_info.rot_angle = GSP_ROT_ANGLE_270_M;
        break;
    case HAL_TRANSFORM_ROT_270:// 7
        gsp_cfg_info.layer0_info.rot_angle = GSP_ROT_ANGLE_90;
        break;
    }

    gsp_cfg_info.layer0_info.src_addr.addr_y = srcPhy;
    gsp_cfg_info.layer0_info.src_addr.addr_v = gsp_cfg_info.layer0_info.src_addr.addr_uv = srcPhy + srcWidth*srcHeight;
    gsp_cfg_info.layer0_info.clip_rect.st_x = trim_rect->x;
    gsp_cfg_info.layer0_info.clip_rect.st_y = trim_rect->y;
    gsp_cfg_info.layer0_info.clip_rect.rect_w = trim_rect->w;
    gsp_cfg_info.layer0_info.clip_rect.rect_h = trim_rect->h;
    gsp_cfg_info.layer0_info.pitch = srcWidth;
    gsp_cfg_info.layer0_info.layer_en = 1;
    //gsp_cfg_info.layer0_info.scaling_en leave it to driver to enable,if need scaling

    gsp_cfg_info.layer_des_info.src_addr.addr_y = dstPhy;
    gsp_cfg_info.layer_des_info.src_addr.addr_v = gsp_cfg_info.layer_des_info.src_addr.addr_uv = dstPhy + dstHeight*dstWidth;
    gsp_cfg_info.layer_des_info.pitch = gsp_cfg_info.layer0_info.des_rect.rect_w;

    ret = GSP_Proccess(&gsp_cfg_info);

    ALOGI_IF(ret,"%s:%d,%s >>\n", __func__, __LINE__,ret?"failed":"success");
    return ret;
}


#endif

#endif


/*
func:GSP_CFC
desc:implement color format convert
note:1 the source and destination image buffer should be physical-coherent memory space,
       the caller should ensure the in and out buffer size is large enough, or the GSP will access cross the buffer border,
       these two will rise unexpected exception.
     2 this function will be block until GSP process over.
*/
int32_t GSP_CFC(GSP_LAYER_SRC_DATA_FMT_E in_format,
                GSP_LAYER_DST_DATA_FMT_E out_format,
                uint32_t width,
                uint32_t height,
                uint32_t in_vaddr,
                uint32_t in_paddr,
                uint32_t out_vaddr,
                uint32_t out_paddr)
{
    int32_t ret = GSP_NO_ERR;
    int32_t gsp_fd = -1;
    uint32_t pixel_cnt = 0;
    GSP_CONFIG_INFO_T gsp_cfg_info;
    memset(&gsp_cfg_info,0,sizeof(GSP_CONFIG_INFO_T));

    ALOGI_IF(debugenable,"%s:%d,informat:%d outformat:%d w:%d h:%d invaddr:0x%08x inpaddr:0x%08x outvaddr:0x%08x outpaddr:0x%08x \n",
             __func__, __LINE__,
             in_format,
             out_format,
             width,
             height,
             in_vaddr,
             in_paddr,
             out_vaddr,
             out_paddr);

    gsp_cfg_info.layer1_info.img_format = in_format;
    gsp_cfg_info.layer1_info.clip_rect.rect_w = width;
    gsp_cfg_info.layer1_info.clip_rect.rect_h = height;
    gsp_cfg_info.layer1_info.pitch = width;
    gsp_cfg_info.layer1_info.layer_en = 1;

    pixel_cnt = width*height;
    switch(in_format) {
    case GSP_SRC_FMT_ARGB888:
    case GSP_SRC_FMT_RGB888:
    case GSP_SRC_FMT_RGB565:
        gsp_cfg_info.layer1_info.src_addr.addr_y = in_paddr;
        break;
    case GSP_SRC_FMT_YUV422_2P:
    case GSP_SRC_FMT_YUV420_2P:
        gsp_cfg_info.layer1_info.src_addr.addr_y = in_paddr;
        gsp_cfg_info.layer1_info.src_addr.addr_uv =
            gsp_cfg_info.layer1_info.src_addr.addr_v = in_paddr + pixel_cnt;
        break;
    case GSP_SRC_FMT_YUV420_3P:
        gsp_cfg_info.layer1_info.src_addr.addr_y = in_paddr;
        gsp_cfg_info.layer1_info.src_addr.addr_uv = in_paddr + pixel_cnt;
        gsp_cfg_info.layer1_info.src_addr.addr_v = gsp_cfg_info.layer1_info.src_addr.addr_uv + pixel_cnt/4;
        break;

    case GSP_SRC_FMT_ARGB565:
    default:
        return GSP_HAL_PARAM_CHECK_ERR;
    }

    gsp_cfg_info.layer_des_info.img_format = out_format;
    switch(out_format) {
    case GSP_DST_FMT_ARGB888:
    case GSP_DST_FMT_RGB888:
    case GSP_DST_FMT_RGB565:
        gsp_cfg_info.layer_des_info.src_addr.addr_y = out_paddr;
        break;
    case GSP_DST_FMT_YUV422_2P:
    case GSP_DST_FMT_YUV420_2P:
        gsp_cfg_info.layer_des_info.src_addr.addr_y = out_paddr;
        gsp_cfg_info.layer_des_info.src_addr.addr_uv =
            gsp_cfg_info.layer_des_info.src_addr.addr_v = out_paddr + pixel_cnt;
        break;
    case GSP_DST_FMT_YUV420_3P:
        gsp_cfg_info.layer_des_info.src_addr.addr_y = out_paddr;
        gsp_cfg_info.layer_des_info.src_addr.addr_uv = out_paddr + pixel_cnt;
        gsp_cfg_info.layer_des_info.src_addr.addr_v = gsp_cfg_info.layer_des_info.src_addr.addr_uv + pixel_cnt/4;
        break;
    case GSP_DST_FMT_ARGB565:
    default:
        return GSP_HAL_PARAM_CHECK_ERR;
    }
    gsp_cfg_info.layer_des_info.pitch = gsp_cfg_info.layer1_info.clip_rect.rect_w;


    gsp_fd = gsp_hal_open();
    if(-1 == gsp_fd) {
        ALOGE("%s:%d,opend gsp failed \n", __func__, __LINE__);
        return gsp_fd;
    }

    ret = gsp_hal_config(gsp_fd,&gsp_cfg_info);
    if(-1 == ret) {
        ALOGE("%s:%d,cfg gsp failed \n", __func__, __LINE__);
        goto exit;
    }
    ret = gsp_hal_trigger(gsp_fd);
    if(-1 == ret) {
        ALOGE("%s:%d,trigger gsp failed \n", __func__, __LINE__);
        goto exit;
    }
    ret = gsp_hal_waitdone(gsp_fd);
    if(-1 == ret) {
        ALOGE("%s:%d,wait done gsp failed \n", __func__, __LINE__);
        goto exit;
    }

exit:
    ret = gsp_hal_close(gsp_fd);
    return -1;

}

int32_t GSP_CFC_with_Scaling(GSP_LAYER_SRC_DATA_FMT_E in_format,
                             GSP_LAYER_DST_DATA_FMT_E out_format,
                             uint32_t in_width,
                             uint32_t in_height,
                             uint32_t out_width,
                             uint32_t out_height,
                             GSP_ENDIAN_INFO_PARAM_T *in_endian_mode,
                             GSP_ENDIAN_INFO_PARAM_T *out_endian_mode,
                             uint32_t in_vaddr,
                             uint32_t in_paddr,
                             uint32_t out_vaddr,
                             uint32_t out_paddr)
{
    int32_t ret = GSP_NO_ERR;
    int32_t gsp_fd = -1;
    uint32_t pixel_cnt_in = 0;
    uint32_t pixel_cnt_out = 0;
    GSP_CONFIG_INFO_T gsp_cfg_info;
    memset(&gsp_cfg_info,0,sizeof(GSP_CONFIG_INFO_T));

    ALOGI_IF(debugenable,"%s:%d,informat:%d outformat:%d iw:%d ih:%d ow:%d oh:%d invaddr:0x%08x inpaddr:0x%08x outvaddr:0x%08x outpaddr:0x%08x \n",
             __func__, __LINE__,
             in_format,
             out_format,
             in_width,
             in_height,
             out_width,
             out_height,
             in_vaddr,
             in_paddr,
             out_vaddr,
             out_paddr);

    gsp_cfg_info.layer0_info.img_format = in_format;
    gsp_cfg_info.layer0_info.clip_rect.rect_w = in_width;
    gsp_cfg_info.layer0_info.clip_rect.rect_h = in_height;
    gsp_cfg_info.layer0_info.pitch = in_width;
    gsp_cfg_info.layer0_info.layer_en = 1;

    gsp_cfg_info.layer0_info.endian_mode = *in_endian_mode;
    //gsp_cfg_info.layer0_info.des_rect.st_x = 0;
    //gsp_cfg_info.layer0_info.des_rect.st_y = 0;
    gsp_cfg_info.layer0_info.des_rect.rect_w = out_width;
    gsp_cfg_info.layer0_info.des_rect.rect_h = out_height;

    pixel_cnt_in = in_width*in_height;
    switch(in_format) {
    case GSP_SRC_FMT_ARGB888:
    case GSP_SRC_FMT_RGB888:
    case GSP_SRC_FMT_RGB565:
        gsp_cfg_info.layer0_info.src_addr.addr_y = in_paddr;
        break;
    case GSP_SRC_FMT_YUV422_2P:
    case GSP_SRC_FMT_YUV420_2P:
        gsp_cfg_info.layer0_info.src_addr.addr_y = in_paddr;
        gsp_cfg_info.layer0_info.src_addr.addr_uv =
            gsp_cfg_info.layer0_info.src_addr.addr_v = in_paddr + pixel_cnt_in;
        break;
    case GSP_SRC_FMT_YUV420_3P:
        gsp_cfg_info.layer0_info.src_addr.addr_y = in_paddr;
        gsp_cfg_info.layer0_info.src_addr.addr_uv = in_paddr + pixel_cnt_in;
        gsp_cfg_info.layer0_info.src_addr.addr_v = gsp_cfg_info.layer0_info.src_addr.addr_uv + pixel_cnt_in/4;
        break;

    case GSP_SRC_FMT_ARGB565:
    default:
        return GSP_HAL_PARAM_CHECK_ERR;
    }

    pixel_cnt_out = out_width*out_height;
    gsp_cfg_info.layer_des_info.img_format = out_format;
    switch(out_format) {
    case GSP_DST_FMT_ARGB888:
    case GSP_DST_FMT_RGB888:
    case GSP_DST_FMT_RGB565:
        gsp_cfg_info.layer_des_info.src_addr.addr_y = out_paddr;
        break;
    case GSP_DST_FMT_YUV422_2P:
    case GSP_DST_FMT_YUV420_2P:
        gsp_cfg_info.layer_des_info.src_addr.addr_y = out_paddr;
        gsp_cfg_info.layer_des_info.src_addr.addr_uv =
            gsp_cfg_info.layer_des_info.src_addr.addr_v = out_paddr + pixel_cnt_out;
        break;
    case GSP_DST_FMT_YUV420_3P:
        gsp_cfg_info.layer_des_info.src_addr.addr_y = out_paddr;
        gsp_cfg_info.layer_des_info.src_addr.addr_uv = out_paddr + pixel_cnt_out;
        gsp_cfg_info.layer_des_info.src_addr.addr_v = gsp_cfg_info.layer_des_info.src_addr.addr_uv + pixel_cnt_out/4;
        break;
    case GSP_DST_FMT_ARGB565:
    default:
        return GSP_HAL_PARAM_CHECK_ERR;
    }
    gsp_cfg_info.layer_des_info.pitch = gsp_cfg_info.layer0_info.des_rect.rect_w;
    gsp_cfg_info.layer_des_info.endian_mode = *out_endian_mode;


    gsp_fd = gsp_hal_open();
    if(-1 == gsp_fd) {
        ALOGE("%s:%d,opend gsp failed \n", __func__, __LINE__);
        return -1;
    }

    ret = gsp_hal_config(gsp_fd,&gsp_cfg_info);
    if(-1 == ret) {
        ALOGE("%s:%d,cfg gsp failed \n", __func__, __LINE__);
        goto exit;
    }
    ret = gsp_hal_trigger(gsp_fd);
    if(-1 == ret) {
        ALOGE("%s:%d,trigger gsp failed \n", __func__, __LINE__);
        goto exit;
    }
    ret = gsp_hal_waitdone(gsp_fd);
    if(-1 == ret) {
        ALOGE("%s:%d,wait done gsp failed \n", __func__, __LINE__);
        goto exit;
    }

exit:
    ret = gsp_hal_close(gsp_fd);
    return -1;

}



/** State information for each device instance */
typedef struct _gsp_context_t {
    gsp_device_t device;
    GSP_CONFIG_INFO_T gsp_cfg_info;
    int     mFD;
    uint8_t mAlpha;
    uint8_t mFlags;
} gsp_context_t;

/** Close the gsp device */
static int close_gsp(hw_device_t *dev)
{
    gsp_context_t* ctx = (gsp_context_t*)dev;
    if (ctx) {
        //gsp_hal_close(ctx->mFD);
        free(ctx);
    }
    return GSP_NO_ERR;
}

static GSP_ROT_ANGLE_E copybit_angle_convert(uint32_t src)
{
    GSP_ROT_ANGLE_E dst = GSP_ROT_ANGLE_0;

    switch(src) {
        /* flip source image horizontally */
    case COPYBIT_TRANSFORM_FLIP_H:
        dst = GSP_ROT_ANGLE_180_M;
        break;
        /* flip source image vertically */
    case COPYBIT_TRANSFORM_FLIP_V:
        dst = GSP_ROT_ANGLE_0_M;
        break;
        /* rotate source image 90 degres */
    case COPYBIT_TRANSFORM_ROT_90:
        dst = GSP_ROT_ANGLE_90;
        break;
        /* rotate source image 180 degres */
    case COPYBIT_TRANSFORM_ROT_180:
        dst = GSP_ROT_ANGLE_180;
        break;
        /* rotate source image 270 degres */
    case COPYBIT_TRANSFORM_ROT_270:
        dst = GSP_ROT_ANGLE_270;
        break;
    default:
        dst = GSP_ROT_ANGLE_0;
        break;
    }
    return dst;
}



/**
 * Set a copybit parameter.
 *
 * @param dev from open
 * @param name one for the COPYBIT_NAME_xxx
 * @param value one of the COPYBIT_VALUE_xxx
 *
 * @return 0 if successful
 */
static int set_parameter_gsp(struct gsp_device_t *dev, int name, int value)
{
    int status = GSP_NO_ERR;
    gsp_context_t* ctx = ( gsp_context_t*)dev;
    switch(name) {
    case COPYBIT_TRANSFORM: {
        ctx->gsp_cfg_info.layer0_info.rot_angle = copybit_angle_convert(value);
    }
    break;
    case COPYBIT_PLANE_ALPHA: {
        ctx->gsp_cfg_info.layer0_info.alpha = (value&0xff);
    }
    break;
    case COPYBIT_DITHER: {
        ctx->gsp_cfg_info.misc_info.dithering_en = (value&0x1);
    }
    break;

    case COPYBIT_PALLET_CLEAN: {
        gsp_image_t const *dst=(gsp_image_t const *)value;

        ctx->gsp_cfg_info.layer1_info.grey.r_val = 0;
        ctx->gsp_cfg_info.layer1_info.grey.g_val = 0;
        ctx->gsp_cfg_info.layer1_info.grey.b_val = 0;
        ctx->gsp_cfg_info.layer1_info.clip_rect.st_x = 0;
        ctx->gsp_cfg_info.layer1_info.clip_rect.st_y = 0;
        ctx->gsp_cfg_info.layer1_info.clip_rect.rect_w = dst->w;
        ctx->gsp_cfg_info.layer1_info.clip_rect.rect_h = dst->h;
        ctx->gsp_cfg_info.layer1_info.pitch = dst->w;

        //the 3-plane addr should not be used by GSP
        ctx->gsp_cfg_info.layer1_info.src_addr.addr_y = (uint32_t)dst->base;
        ctx->gsp_cfg_info.layer1_info.src_addr.addr_uv = (uint32_t)dst->base;
        ctx->gsp_cfg_info.layer1_info.src_addr.addr_v = (uint32_t)dst->base;

        ctx->gsp_cfg_info.layer1_info.pallet_en = 1;
        ctx->gsp_cfg_info.layer1_info.alpha = 0x1;
		ctx->gsp_cfg_info.layer0_info.alpha = 0xff;
        ctx->gsp_cfg_info.layer1_info.rot_angle = GSP_ROT_ANGLE_0;
        ctx->gsp_cfg_info.layer1_info.des_pos.pos_pt_x = 0;
        ctx->gsp_cfg_info.layer1_info.des_pos.pos_pt_y = 0;
        ctx->gsp_cfg_info.layer1_info.layer_en = 1;

        ALOGE("set_parameter_gsp%d: COPYBIT_PALLET_CLEAN\n", __LINE__);
    }
    break;

    default: {
        status = GSP_HAL_PARAM_ERR;
        ALOGE("COPYBIT_BLUR and COPYBIT_ROTATION_DEG not support by SPRD-GSP: Line:%d\n", __LINE__);
    }
    break;
    }
    return status;
}


/** do a stretch blit type operation */
static int stretch_gsp(
    gsp_device_t *dev,
    gsp_image_t const *src,
    gsp_image_t const *dst,
    gsp_rect_t const *src_rect,
    gsp_rect_t const *dst_rect,
    gsp_region_t const *region)
{
    int status = GSP_NO_ERR;
    unsigned int     src_phyaddr = 0;
    unsigned int     dst_phyaddr = 0;
    gsp_context_t* ctx = NULL;
    struct private_handle_t *private_h_src = NULL;
    struct private_handle_t *private_h_dst = NULL;

    ALOGE("stretch_gsp%d: enter\n", __LINE__);

    ctx = (gsp_context_t*)dev;

    if(src->handle != NULL) {
        private_h_src = (struct private_handle_t *)src->handle;
        private_h_dst = (struct private_handle_t *)dst->handle;


        if(ctx == NULL
                ||private_h_src == NULL
                ||private_h_dst == NULL) {
            ALOGE("parameters err! Line:%d \n", __LINE__);
            return GSP_HAL_PARAM_ERR;
        }

        if(!(private_h_src->flags & private_handle_t::PRIV_FLAGS_USES_PHY)
                ||!(private_h_dst->flags & private_handle_t::PRIV_FLAGS_USES_PHY)) {
            ALOGE("gsp only support physical address now! Line:%d \n", __LINE__);
            return GSP_HAL_VITUAL_ADDR_NOT_SUPPORT;
        }
        ALOGE("handle phy addr! Line:%d \n", __LINE__);
        src_phyaddr = private_h_src->phyaddr;
        dst_phyaddr = private_h_dst->phyaddr;
    } else if(src->base) {
        ALOGE("base phy addr! Line:%d \n", __LINE__);
        src_phyaddr = (unsigned int)src->base;
        dst_phyaddr = (unsigned int)dst->base;
    } else {
        ALOGE("parameters err! Line:%d \n", __LINE__);
        return GSP_HAL_PARAM_ERR;
    }

    uint32_t pixel_cnt_in = 0;
    uint32_t pixel_cnt_out = 0;

    ctx->gsp_cfg_info.layer0_info.img_format = src->src_format;
    ctx->gsp_cfg_info.layer0_info.pitch = src->w;

    ctx->gsp_cfg_info.layer0_info.clip_rect.st_x = src_rect->x;
    ctx->gsp_cfg_info.layer0_info.clip_rect.st_y = src_rect->y;
    ctx->gsp_cfg_info.layer0_info.clip_rect.rect_w = src_rect->w;
    ctx->gsp_cfg_info.layer0_info.clip_rect.rect_h = src_rect->h;

    ctx->gsp_cfg_info.layer0_info.layer_en = 1;

    ctx->gsp_cfg_info.layer0_info.endian_mode = src->endian_mode;
    ctx->gsp_cfg_info.layer0_info.des_rect.st_x = dst_rect->x;
    ctx->gsp_cfg_info.layer0_info.des_rect.st_y = dst_rect->y;
    ctx->gsp_cfg_info.layer0_info.des_rect.rect_w = dst_rect->w;
    ctx->gsp_cfg_info.layer0_info.des_rect.rect_h = dst_rect->h;

    pixel_cnt_in = src->w*src->h;
    switch(ctx->gsp_cfg_info.layer0_info.img_format) {
    case GSP_SRC_FMT_ARGB888:
    case GSP_SRC_FMT_RGB888:
    case GSP_SRC_FMT_RGB565:
        ctx->gsp_cfg_info.layer0_info.src_addr.addr_y = src_phyaddr;
        break;
    case GSP_SRC_FMT_YUV422_2P:
    case GSP_SRC_FMT_YUV420_2P:
        ctx->gsp_cfg_info.layer0_info.src_addr.addr_y = src_phyaddr;
        ctx->gsp_cfg_info.layer0_info.src_addr.addr_uv =
            ctx->gsp_cfg_info.layer0_info.src_addr.addr_v = src_phyaddr + pixel_cnt_in;
        break;
    case GSP_SRC_FMT_YUV420_3P:
        ctx->gsp_cfg_info.layer0_info.src_addr.addr_y = src_phyaddr;
        ctx->gsp_cfg_info.layer0_info.src_addr.addr_uv = src_phyaddr + pixel_cnt_in;
        ctx->gsp_cfg_info.layer0_info.src_addr.addr_v = ctx->gsp_cfg_info.layer0_info.src_addr.addr_uv + pixel_cnt_in/4;
        break;

    case GSP_SRC_FMT_ARGB565:
    default:
        status = GSP_HAL_PARAM_CHECK_ERR;
        ALOGE("gsp not support this source color format! Line:%d \n", __LINE__);
        goto exit;
        break;
    }

    pixel_cnt_out = dst->w*dst->h;
    ctx->gsp_cfg_info.layer_des_info.img_format = dst->des_format;
    switch(ctx->gsp_cfg_info.layer_des_info.img_format) {
    case GSP_DST_FMT_ARGB888:
    case GSP_DST_FMT_RGB888:
    case GSP_DST_FMT_RGB565:
        ctx->gsp_cfg_info.layer_des_info.src_addr.addr_y = dst_phyaddr;
        break;
    case GSP_DST_FMT_YUV422_2P:
    case GSP_DST_FMT_YUV420_2P:
        ctx->gsp_cfg_info.layer_des_info.src_addr.addr_y = dst_phyaddr;
        ctx->gsp_cfg_info.layer_des_info.src_addr.addr_uv =
            ctx->gsp_cfg_info.layer_des_info.src_addr.addr_v = dst_phyaddr + pixel_cnt_out;
        break;
    case GSP_DST_FMT_YUV420_3P:
        ctx->gsp_cfg_info.layer_des_info.src_addr.addr_y = dst_phyaddr;
        ctx->gsp_cfg_info.layer_des_info.src_addr.addr_uv = dst_phyaddr + pixel_cnt_out;
        ctx->gsp_cfg_info.layer_des_info.src_addr.addr_v = ctx->gsp_cfg_info.layer_des_info.src_addr.addr_uv + pixel_cnt_out/4;
        break;
    case GSP_DST_FMT_ARGB565:
    default:
        status = GSP_HAL_PARAM_CHECK_ERR;
        ALOGE("gsp not support this destination color format! Line:%d \n", __LINE__);
        goto exit;
        break;
    }
    //ctx->gsp_cfg_info.layer_des_info.pitch = ctx->gsp_cfg_info.layer0_info.des_rect.rect_w;
    ctx->gsp_cfg_info.layer_des_info.pitch = dst->w;
    ctx->gsp_cfg_info.layer_des_info.endian_mode = dst->endian_mode;
/*
    ALOGE("{%dx%d %d}(%d,%d)[%dx%d]=>{%dx%d %d}(%d,%d)[%dx%d]\n",
          ctx->gsp_cfg_info.layer0_info.pitch,
          src->h,
          ctx->gsp_cfg_info.layer0_info.img_format,
          ctx->gsp_cfg_info.layer0_info.clip_rect.st_x,
          ctx->gsp_cfg_info.layer0_info.clip_rect.st_y,
          ctx->gsp_cfg_info.layer0_info.clip_rect.rect_w,
          ctx->gsp_cfg_info.layer0_info.clip_rect.rect_h,
          ctx->gsp_cfg_info.layer_des_info.pitch,
          dst->h,
          ctx->gsp_cfg_info.layer_des_info.img_format,
          ctx->gsp_cfg_info.layer0_info.des_rect.st_x,
          ctx->gsp_cfg_info.layer0_info.des_rect.st_y,
          ctx->gsp_cfg_info.layer0_info.des_rect.rect_w,
          ctx->gsp_cfg_info.layer0_info.des_rect.rect_h);
*/
    status = GSP_Proccess(&ctx->gsp_cfg_info);
    ALOGI_IF(status,"%s:%d,%s\n", __func__, __LINE__,status?"failed":"success");

exit:
    return status;
}


/** Perform a blit type operation , of course the color convert also can be implement by this procedure */
static int blit_gsp(
    gsp_device_t *dev,
    gsp_image_t const *src,
    gsp_image_t const *dst,
    gsp_region_t const *region)
{
    gsp_rect_t dr = { 0, 0, dst->w, dst->h };
    gsp_rect_t sr = { 0, 0, src->w, src->h };
    return stretch_gsp(dev, src, dst, &sr, &dr, region);
}


/** Open a new instance of a copybit device using name */
static int open_gsp(const struct hw_module_t* module,
                    const char* name,
                    struct hw_device_t** device)
{
    int status = GSP_NO_ERR;
    gsp_context_t *ctx;
    ctx = ( gsp_context_t *)malloc(sizeof( gsp_context_t));
    if(ctx) {
        memset(ctx, 0, sizeof(*ctx));
    } else {
        ALOGE("gsp hal open, alloc context failed. Line:%d \n", __LINE__);
        status = GSP_HAL_ALLOC_ERR;
        goto exit;
    }
    ctx->device.common.tag = HARDWARE_DEVICE_TAG;
    ctx->device.common.version = 1;
    ctx->device.common.module = const_cast<hw_module_t*>(module);
    ctx->device.common.close = close_gsp;
    ctx->device.blit = blit_gsp;
    ctx->device.stretch = stretch_gsp;
    ctx->device.set_parameter= set_parameter_gsp;
    ctx->device.color_convert = GSP_CFC;
    ctx->device.scaling = GSP_CFC_with_Scaling;
#if 1// ndef _HWCOMPOSER_USE_GSP_BLEND
    ctx->device.rotation = camera_rotation;
    ctx->device.copy_data = camera_rotation_copy_data;
    ctx->device.copy_data_from_virtual = camera_rotation_copy_data_from_virtual;
    ctx->device.transform_layer = transform_layer;
#endif
    ctx->device.GSP_Proccess = GSP_Proccess;
    ctx->mAlpha = 0;
    ctx->mFlags = 0;
    ctx->mFD = 0;//gsp_hal_open();

    *device = &ctx->device.common;

    status = GSP_NO_ERR;
    ALOGI_IF(debugenable,"gsp hal lib open success. Line:%d \n", __LINE__);
exit:
    return status;
}




static struct hw_module_methods_t gsp_module_methods = {
open:
    open_gsp
};


/*
 * The COPYBIT Module
 */
gsp_module_t HAL_MODULE_INFO_SYM = {
common:
    {
tag:
        HARDWARE_MODULE_TAG,
        version_major: 1,
        version_minor: 0,
id:
        GSP_HARDWARE_MODULE_ID,
name: "SPRD 2D Accelerate Module"
        ,
author: "Google, Inc."
        ,
methods:
        &gsp_module_methods
    }
};


