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
#ifndef _GSP_HAL_H_
#define _GSP_HAL_H_

#ifdef   __cplusplus
//extern   "C"
//{
#endif
#include <fcntl.h>
#include <sys/ioctl.h>
#include <cutils/log.h>
#include <stdlib.h>
#include <hardware/hardware.h>

#include <gsp_types_shark.h>
#include "../sc8825/dcam_hal.h"






/*
func:gsp_hal_open
desc:open GSP device
return: -1 means failed,other success
notes: a thread can't open the device again unless it close first
*/
extern int32_t gsp_hal_open(void);



/*
func:gsp_hal_close
desc:close GSP device
return: -1 means failed,0 success
notes:
*/
extern int32_t gsp_hal_close(int32_t gsp_fd);


/*
func:gsp_hal_config
desc:set GSP device config parameters
return: -1 means failed,0 success
notes:
*/
extern int32_t gsp_hal_config(int32_t gsp_fd,GSP_CONFIG_INFO_T *gsp_cfg_info);


/*
func:gsp_hal_trigger
desc:trigger GSP to run
return: -1 means failed,0 success
notes:
*/
extern int32_t gsp_hal_trigger(int32_t gsp_fd);


/*
func:gsp_hal_waitdone
desc:wait GSP finish
return: -1 means thread interrupt by signal,0 means GSP done successfully
notes:
*/
extern int32_t gsp_hal_waitdone(int32_t gsp_fd);



/*
func:GSP_CFC
desc:implement color format convert
note:1 the source and destination image buffer should be physical-coherent memory space,
       the caller should ensure the in and out buffer size is large enough, or the GSP will access cross the buffer border,
       these two will rise unexpected exception.
     2 this function will be block until GSP process over.
*/
extern int32_t GSP_CFC(GSP_LAYER_SRC_DATA_FMT_E in_format,
                       GSP_LAYER_DST_DATA_FMT_E out_format,
                       uint32_t width,
                       uint32_t height,
                       uint32_t in_vaddr,
                       uint32_t in_paddr,
                       uint32_t out_vaddr,
                       uint32_t out_paddr);

extern int32_t GSP_CFC_with_Scaling(GSP_LAYER_SRC_DATA_FMT_E in_format,
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
                                    uint32_t out_paddr);


/*
func:GSP_Proccess
desc:all the GSP function can be complete in this function, such as CFC,scaling,blend,rotation and mirror,clipping.
note:1 the source and destination image buffer should be physical-coherent memory space,
       the caller should ensure the in and out buffer size is large enough, or the GSP will access cross the buffer border,
       these two will rise unexpected exception.
     2 this function will be block until GSP process over.
*/
extern int32_t GSP_Proccess(GSP_CONFIG_INFO_T *pgsp_cfg_info);





/**
 * The id of this module
 */
#define GSP_HARDWARE_MODULE_ID "sprd_gsp"

/**
 * Every hardware module must have a data structure named HAL_MODULE_INFO_SYM
 * and the fields of this data structure must begin with hw_module_t
 * followed by module specific information.
 */
typedef struct gsp_module_t
{
    struct hw_module_t common;
} gsp_module_t;

/* Rectangle */
typedef struct gsp_rect_t
{
    /* left x */
    uint32_t x;
    /* top y*/
    uint32_t y;
    /* width */
    uint32_t w;
    /* height */
    uint32_t h;
} gsp_rect_t;

/* Region */
typedef struct gsp_region_t
{
    int (*next)( struct gsp_region_t const *region,  gsp_rect_t *rect);
} gsp_region_t;


/* Image structure */
typedef struct gsp_image_t
{
    /* width */
    uint32_t    w;
    /* height */
    uint32_t    h;
    GSP_LAYER_SRC_DATA_FMT_E    src_format;
    GSP_LAYER_DST_DATA_FMT_E    des_format;
    GSP_ENDIAN_INFO_PARAM_T     endian_mode;
    /* base of buffer with image */
    void        *base;
    /* handle to the image */
    native_handle_t* handle;
    /* number of pixels added for the stride */
    uint32_t    padding;
} gsp_image_t;

/* name for copybit_set_parameter */
enum
{
    /* rotation of the source image in degrees (0 to 359) */
    COPYBIT_ROTATION_DEG    = 1,
    /* plane alpha value */
    COPYBIT_PLANE_ALPHA     = 2,
    /* enable or disable dithering */
    COPYBIT_DITHER          = 3,
    /* transformation applied (this is a superset of COPYBIT_ROTATION_DEG) */
    COPYBIT_TRANSFORM       = 4,
    /* blurs the copied bitmap. The amount of blurring cannot be changed
     * at this time. */
    COPYBIT_BLUR            = 5,
    /* use Layer1's pallet function to clean the area Layer0 not covered */
    COPYBIT_PALLET_CLEAN    = 6
};

/* values for copybit_set_parameter(COPYBIT_TRANSFORM) */
enum
{
    /* flip source image horizontally */
    COPYBIT_TRANSFORM_FLIP_H    = HAL_TRANSFORM_FLIP_H,
    /* flip source image vertically */
    COPYBIT_TRANSFORM_FLIP_V    = HAL_TRANSFORM_FLIP_V,
    /* rotate source image 90 degres */
    COPYBIT_TRANSFORM_ROT_90    = HAL_TRANSFORM_ROT_90,
    /* rotate source image 180 degres */
    COPYBIT_TRANSFORM_ROT_180   = HAL_TRANSFORM_ROT_180,
    /* rotate source image 270 degres */
    COPYBIT_TRANSFORM_ROT_270   = HAL_TRANSFORM_ROT_270,
};

/* enable/disable value copybit_set_parameter */
enum
{
    COPYBIT_DISABLE = 0,
    COPYBIT_ENABLE  = 1
};



/**
 * Every device data structure must begin with hw_device_t
 * followed by module specific public methods and attributes.
 */
typedef struct gsp_device_t
{
    struct hw_device_t common;
#if 1 //these interfaces are for copybit function

    /**
     * Set a copybit parameter.
     *
     * @param dev from open
     * @param name one for the COPYBIT_NAME_xxx
     * @param value one of the COPYBIT_VALUE_xxx
     *
     * @return 0 if successful
     */
    int (*set_parameter)(struct gsp_device_t *dev, int name, int value);


    /**
     * Execute the bit blit copy operation
     *
     * @param dev from open
     * @param dst is the destination image
     * @param src is the source image
     * @param region the clip region
     *
     * @return 0 if successful
     */
    int (*blit)(struct gsp_device_t *dev,
                gsp_image_t const *src,
                gsp_image_t const *dst,
                gsp_region_t const *region);

    /**
    * Execute the stretch bit blit copy operation
    *
    * @param dev from open
    * @param dst is the destination image
    * @param src is the source image
    * @param dst_rect is the destination rectangle
    * @param src_rect is the source rectangle
    * @param region the clip region
    *
    * @return 0 if successful
    */
    int (*stretch)(struct gsp_device_t *dev,
                   gsp_image_t const *src,
                   gsp_image_t const *dst,
                   gsp_rect_t const *src_rect,
                   gsp_rect_t const *dst_rect,
                   gsp_region_t const *region);


#if 1
//these two function is for HTC to their early demand

    /*
    func:GSP_CFC
    desc:implement color format convert
    note:1 the source and destination image buffer should be physical-coherent memory space,
           the caller should ensure the in and out buffer size is large enough, or the GSP will access cross the buffer border,
           these two will rise unexpected exception.
         2 this function will be block until GSP process over.
    */
    int32_t (*color_convert)(GSP_LAYER_SRC_DATA_FMT_E in_format,
                             GSP_LAYER_DST_DATA_FMT_E out_format,
                             uint32_t width,
                             uint32_t height,
                             uint32_t in_vaddr,
                             uint32_t in_paddr,
                             uint32_t out_vaddr,
                             uint32_t out_paddr);



    int32_t (*scaling)(GSP_LAYER_SRC_DATA_FMT_E in_format,
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
                       uint32_t out_paddr);
#endif

#endif

#if 1 //ndef _HWCOMPOSER_USE_GSP_BLEND
    /*
    GSP is used to copy osd and scaling video, DDR data band need much more than sc8830 can provide,
    result in dispC underflow!!
    */

    int (*copy_data)(uint32_t width, uint32_t height, uint32_t in_addr, uint32_t out_addr);
    int (*copy_data_from_virtual)(uint32_t width, uint32_t height, uint32_t in_virtual_addr, uint32_t out_addr);
    int32_t (*rotation)(HW_ROTATION_DATA_FORMAT_E rot_format,
                        int32_t degree,
                        uint32_t width,
                        uint32_t height,
                        uint32_t in_addr,
                        uint32_t out_addr);


    int (*transform_layer)(uint32_t srcPhy,//src  phy_addr_y
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
                           uint32_t tmp_vir_addr);
#endif


    /*
    func:GSP_Proccess
    desc:all the GSP function can be complete in this function, such as CFC,scaling,blend,rotation and mirror,clipping.
    note:1 the source and destination image buffer should be physical-coherent memory space,
           the caller should ensure the in and out buffer size is large enough, or the GSP will access cross the buffer border,
           these two will rise unexpected exception.
         2 this function will be block until GSP process over.
    */
    int32_t (*GSP_Proccess)(GSP_CONFIG_INFO_T *pgsp_cfg_info);//GSP_Proccess


} gsp_device_t;

#ifdef   __cplusplus
//}
#endif
#endif
