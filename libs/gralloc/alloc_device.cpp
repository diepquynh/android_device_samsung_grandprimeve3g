/*
 * Copyright (C) 2010 ARM Limited. All rights reserved.
 *
 * Copyright (C) 2008 The Android Open Source Project
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

#include <string.h>
#include <errno.h>
#include <pthread.h>

#include <cutils/log.h>
#include <cutils/atomic.h>
#include <hardware/hardware.h>
#include <hardware/gralloc.h>

#include <sys/ioctl.h>

#include "alloc_device.h"
#include "gralloc_priv.h"
#include "gralloc_helper.h"
#include "framebuffer_device.h"

#if GRALLOC_ARM_UMP_MODULE
#include <ump/ump.h>
#include <ump/ump_ref_drv.h>
#endif

#if GRALLOC_ARM_DMA_BUF_MODULE
#include <linux/ion.h>
#include <ion/ion.h>
#endif

#include <sys/mman.h>
//#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <fcntl.h>
//#include <linux/ion.h>
#include "usr/include/linux/ion.h"
#include "ion_sprd.h"
#define ION_DEVICE "/dev/ion"


#define GRALLOC_ALIGN( value, base ) (((value) + ((base) - 1)) & ~((base) - 1))


#if GRALLOC_SIMULATE_FAILURES
#include <cutils/properties.h>

/* system property keys for controlling simulated UMP allocation failures */
#define PROP_MALI_TEST_GRALLOC_FAIL_FIRST     "mali.test.gralloc.fail_first"
#define PROP_MALI_TEST_GRALLOC_FAIL_INTERVAL  "mali.test.gralloc.fail_interval"

static int __ump_alloc_should_fail()
{

	static unsigned int call_count	= 0;
	unsigned int        first_fail	= 0;
	int                 fail_period = 0;
	int                 fail        = 0;

	++call_count;

	/* read the system properties that control failure simulation */
	{
		char prop_value[PROPERTY_VALUE_MAX];

		if (property_get(PROP_MALI_TEST_GRALLOC_FAIL_FIRST, prop_value, "0") > 0)
		{
			sscanf(prop_value, "%u", &first_fail);
		}

		if (property_get(PROP_MALI_TEST_GRALLOC_FAIL_INTERVAL, prop_value, "0") > 0)
		{
			sscanf(prop_value, "%u", &fail_period);
		}
	}

	/* failure simulation is enabled by setting the first_fail property to non-zero */
	if (first_fail > 0)
	{
		LOGI("iteration %u (fail=%u, period=%u)\n", call_count, first_fail, fail_period);

		fail =	(call_count == first_fail) ||
				(call_count > first_fail && fail_period > 0 && 0 == (call_count - first_fail) % fail_period);

		if (fail)
		{
			AERR("failed ump_ref_drv_allocate on iteration #%d\n", call_count);
		}
	}
	return fail;
}
#endif

#if SPRD_ION
int open_ion_device(private_module_t* m)
{
	int res=-1;
	pthread_mutex_lock(&m->fd_lock);
	if(m->mIonFd < 0)
	{
		m->mIonFd = open(ION_DEVICE, O_RDONLY|O_SYNC);
	}
	if(m->mIonFd >= 0)
	{
		res=0;
	}
	pthread_mutex_unlock(&m->fd_lock);
	return res;
}

void close_ion_device(private_module_t* m)
{
	pthread_mutex_lock(&m->fd_lock);
	if(m->mIonFd >= 0)
	{
		close(m->mIonFd);
	}
	m->mIonFd = -1;
	pthread_mutex_unlock(&m->fd_lock);
}

static int gralloc_alloc_ionbuffer_locked(alloc_device_t* dev, size_t size, int usage, buffer_handle_t* pHandle,  int is_overlay)
{
	private_module_t* m = reinterpret_cast<private_module_t*>(dev->common.module);

	if(open_ion_device(m))
	{
       	ALOGE("%s: Failed to open ion device - %s",  __FUNCTION__, strerror(errno));
       	return -ENOMEM;
       }

	int is_cached = 0;
	int ion_fd = m->mIonFd;
	uint32_t uread = usage & GRALLOC_USAGE_SW_READ_MASK;
	uint32_t uwrite = usage & GRALLOC_USAGE_SW_WRITE_MASK;
	if (uread == GRALLOC_USAGE_SW_READ_OFTEN || uwrite == GRALLOC_USAGE_SW_WRITE_OFTEN) {
		is_cached = 1;
		ion_fd = open(ION_DEVICE, O_RDONLY);
		if(ion_fd < 0) {
			ALOGE("open cacheable ion device fail");
			return  -ENOMEM;
		}
	}

	int err = 0;
	void *base = 0;
       struct ion_fd_data fd_data;
       struct ion_handle_data handle_data;
       struct ion_allocation_data ionAllocData;
       ionAllocData.len = round_up_to_page_size(size);
       ionAllocData.align = PAGE_SIZE;
#if (ION_DRIVER_VERSION == 1)
       if (is_overlay) {
	   	ionAllocData.heap_mask = (1 <<( ION_HEAP_TYPE_CARVEOUT+1));
       } else {
       		ionAllocData.heap_mask = ION_HEAP_CARVEOUT_MASK;
       }
	ionAllocData.flags = 0;
#else
       if (is_overlay) {
	   	ionAllocData.flags = (1 <<( ION_HEAP_TYPE_CARVEOUT+1));
       } else {
       		ionAllocData.flags = ION_HEAP_CARVEOUT_MASK;
       }	
#endif
    	err = ioctl(ion_fd, ION_IOC_ALLOC, &ionAllocData);
    	if(err)
	{
		if(is_overlay)
			ALOGI("overlay is not opened or not enough overlay ion memory, result is:%d" , err);
		else
			ALOGE("ION_IOC_ALLOC fail");
		if(is_cached) close(ion_fd);
		return -ENOMEM;
    	}

    	fd_data.handle = ionAllocData.handle;
       handle_data.handle = ionAllocData.handle;

       err = ioctl(ion_fd, ION_IOC_SHARE, &fd_data);
       if(err)
	{
		ALOGE("ION_IOC_SHARE fail");
		if(is_cached) close(ion_fd);
		return -ENOMEM;
       }

       base = mmap(0, ionAllocData.len, PROT_READ|PROT_WRITE,
                                MAP_SHARED, fd_data.fd, 0);

	if(base == MAP_FAILED)
	{
		ALOGE("%s: Failed to map the allocated memory: %s",  __FUNCTION__, strerror(errno));
		ioctl(ion_fd, ION_IOC_FREE, &handle_data);
		if(is_cached) close(ion_fd);
		return -ENOMEM;
       }

	gralloc_module_t *module = &(m->base);

	err = module->perform(module,
				GRALLOC_MODULE_PERFORM_CREATE_HANDLE_FROM_BUFFER,
				fd_data.fd, ionAllocData.len,
				0,base,
				pHandle);
	if(err)
	{
		ALOGE("perform fail");
		munmap(base, ionAllocData.len);
		if(is_cached) close(ion_fd);
		return -ENOMEM;
	}

	ioctl(ion_fd, ION_IOC_FREE, &handle_data);

	if(is_cached) close(ion_fd);

	return 0;
}

static int gralloc_alloc_ionbuffer(alloc_device_t* dev, size_t size, int usage, buffer_handle_t* pHandle, int is_overlay)
{
	private_module_t* m = reinterpret_cast<private_module_t*>(dev->common.module);
	pthread_mutex_lock(&m->lock);
	int err = gralloc_alloc_ionbuffer_locked(dev, size, usage, pHandle, is_overlay);
	pthread_mutex_unlock(&m->lock);
	return err;
}
#endif

static int gralloc_alloc_buffer(alloc_device_t* dev, size_t size, int usage, buffer_handle_t* pHandle)
{
    #if GRALLOC_ARM_DMA_BUF_MODULE
    {
        private_module_t* m = reinterpret_cast<private_module_t*>(dev->common.module);
        struct ion_handle *ion_hnd;
        unsigned char *cpu_ptr;
        int shared_fd;
        int ret;
        int ion_heap_mask = 0;
        int ion_flag = 0;
        private_handle_t *hnd = NULL;

        if (usage & (GRALLOC_USAGE_VIDEO_BUFFER|GRALLOC_USAGE_CAMERA_BUFFER))
        { 
            ion_heap_mask = ION_HEAP_ID_MASK_MM;
        }
        else if(usage & GRALLOC_USAGE_OVERLAY_BUFFER)
        {
            ion_heap_mask = ION_HEAP_ID_MASK_OVERLAY;
        }
        else
        {
            ion_heap_mask = ION_HEAP_ID_MASK_SYSTEM;
        }

        if (usage & (GRALLOC_USAGE_SW_READ_MASK | GRALLOC_USAGE_SW_WRITE_MASK))
        {
            ion_flag = ION_FLAG_CACHED | ION_FLAG_CACHED_NEEDS_SYNC;
        }

        ret = ion_alloc(m->ion_client, size, 0, ion_heap_mask, ion_flag, &ion_hnd);
        if ( ret != 0)
        {
            AERR("Failed to ion_alloc from ion_client:%d", m->ion_client);
            return -1;
        }

        ret = ion_share( m->ion_client, ion_hnd, &shared_fd );
        if ( ret != 0 )
        {
            AERR( "ion_share( %d ) failed", m->ion_client );
            if ( 0 != ion_free( m->ion_client, ion_hnd ) ) AERR( "ion_free( %d ) failed", m->ion_client );
            return -1;
        }
        cpu_ptr = (unsigned char*)mmap( NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, shared_fd, 0 );

        if ( MAP_FAILED == cpu_ptr )
        {
            AERR( "ion_map( %d ) failed", m->ion_client );
            if ( 0 != ion_free( m->ion_client, ion_hnd ) ) AERR( "ion_free( %d ) failed", m->ion_client );
            close( shared_fd );
            return -1;
        }


        hnd = new private_handle_t( private_handle_t::PRIV_FLAGS_USES_ION, usage, size, (int)cpu_ptr, private_handle_t::LOCK_STATE_MAPPED );
        if ( NULL != hnd )
        {
            if(ion_heap_mask == ION_HEAP_CARVEOUT_MASK)
            {
                hnd->flags=(private_handle_t::PRIV_FLAGS_USES_ION)|(private_handle_t::PRIV_FLAGS_USES_PHY);
            }
            ALOGD("the flag 0x%x and the vadress is 0x%x and the size is 0x%x",hnd->flags,(int)cpu_ptr,size);
            hnd->share_fd = shared_fd;
            hnd->ion_hnd = ion_hnd;
            hnd->ion_client = m->ion_client;
            *pHandle = hnd;
            ion_invalidate_fd(hnd->ion_client,hnd->share_fd);
            return 0;
        }
        else
        {
            AERR( "Gralloc out of mem for ion_client:%d", m->ion_client );
            close( shared_fd );
            ret = munmap( cpu_ptr, size );
            if ( 0 != ret ) AERR( "munmap failed for base:%p size: %d", cpu_ptr, size );
            ret = ion_free( m->ion_client, ion_hnd );
            if ( 0 != ret ) AERR( "ion_free( %d ) failed", m->ion_client );
            return -1;
        }
    }
    #endif

#if GRALLOC_ARM_UMP_MODULE
	{
		ump_handle ump_mem_handle;
		void *cpu_ptr;
		ump_secure_id ump_id;
		ump_alloc_constraints constraints;

		size = round_up_to_page_size(size);

		if( (usage&GRALLOC_USAGE_SW_READ_MASK) == GRALLOC_USAGE_SW_READ_OFTEN )
		{
			constraints =  UMP_REF_DRV_CONSTRAINT_USE_CACHE;
		}
		else
		{
			constraints = UMP_REF_DRV_CONSTRAINT_NONE;
		}

#ifdef GRALLOC_SIMULATE_FAILURES
		/* if the failure condition matches, fail this iteration */
		if (__ump_alloc_should_fail())
		{
			ump_mem_handle = UMP_INVALID_MEMORY_HANDLE;
		}
		else
#endif
		{
			ump_mem_handle = ump_ref_drv_allocate(size, constraints);

			if (UMP_INVALID_MEMORY_HANDLE != ump_mem_handle)
			{
				cpu_ptr = ump_mapped_pointer_get(ump_mem_handle);
				if (NULL != cpu_ptr)
				{
					ump_id = ump_secure_id_get(ump_mem_handle);
					if (UMP_INVALID_SECURE_ID != ump_id)
					{
						private_handle_t* hnd = new private_handle_t(private_handle_t::PRIV_FLAGS_USES_UMP, usage, size, (int)cpu_ptr,
																	 private_handle_t::LOCK_STATE_MAPPED, ump_id, ump_mem_handle);
						if (NULL != hnd)
						{
							*pHandle = hnd;
							return 0;
						}
						else
						{
							AERR( "gralloc_alloc_buffer() failed to allocate handle. ump_handle = %p, ump_id = %d", ump_mem_handle, ump_id );
						}
					}
					else
					{
						AERR( "gralloc_alloc_buffer() failed to retrieve valid secure id. ump_handle = %p", ump_mem_handle );
					}
			
					ump_mapped_pointer_release(ump_mem_handle);
				}
				else
				{
					AERR( "gralloc_alloc_buffer() failed to map UMP memory. ump_handle = %p", ump_mem_handle );
				}

				ump_reference_release(ump_mem_handle);
			}
			else
			{
				AERR( "gralloc_alloc_buffer() failed to allocate UMP memory. size:%d constraints: %d", size, constraints );
			}
		}
		return -1;
	}
#endif

}

static int gralloc_alloc_framebuffer_locked(alloc_device_t* dev, size_t size, int usage, buffer_handle_t* pHandle)
{
	private_module_t* m = reinterpret_cast<private_module_t*>(dev->common.module);

	// allocate the framebuffer
	if (m->framebuffer == NULL)
	{
		// initialize the framebuffer, the framebuffer is mapped once and forever.
		int err = init_frame_buffer_locked(m);
		if (err < 0)
		{
			return err;
		}
	}

	const uint32_t bufferMask = m->bufferMask;
	const uint32_t numBuffers = m->numBuffers;
	const size_t bufferSize = m->finfo.line_length * m->info.yres;
	if (numBuffers == 1)
	{
		// If we have only one buffer, we never use page-flipping. Instead,
		// we return a regular buffer which will be memcpy'ed to the main
		// screen when post is called.
		int newUsage = (usage & ~GRALLOC_USAGE_HW_FB) | GRALLOC_USAGE_HW_2D;
		AERR( "fallback to single buffering. Virtual Y-res too small %d", m->info.yres );
		return gralloc_alloc_buffer(dev, bufferSize, newUsage, pHandle);
	}

	if (bufferMask >= ((1LU<<numBuffers)-1))
	{
		// We ran out of buffers.
		return -ENOMEM;
	}

	int vaddr = m->framebuffer->base;
	// find a free slot
	for (uint32_t i=0 ; i<numBuffers ; i++)
	{
		if ((bufferMask & (1LU<<i)) == 0)
		{
			m->bufferMask |= (1LU<<i);
			break;
		}
		vaddr += bufferSize;
	}

	// The entire framebuffer memory is already mapped, now create a buffer object for parts of this memory
	private_handle_t* hnd = new private_handle_t(private_handle_t::PRIV_FLAGS_FRAMEBUFFER, usage, size, vaddr,
												 0, dup(m->framebuffer->fd), vaddr - m->framebuffer->base);
#if GRALLOC_ARM_UMP_MODULE
	hnd->ump_id = m->framebuffer->ump_id;
	/* create a backing ump memory handle if the framebuffer is exposed as a secure ID */
	if ( (int)UMP_INVALID_SECURE_ID != hnd->ump_id )
	{
		hnd->ump_mem_handle = (int)ump_handle_create_from_secure_id( hnd->ump_id );
		if ( (int)UMP_INVALID_MEMORY_HANDLE == hnd->ump_mem_handle )
		{
			AINF("warning: unable to create UMP handle from secure ID %i\n", hnd->ump_id);
		}
	}
#endif

#if GRALLOC_ARM_DMA_BUF_MODULE
	{
	#ifdef FBIOGET_DMABUF
		struct fb_dmabuf_export fb_dma_buf;

		if ( ioctl( m->framebuffer->fd, FBIOGET_DMABUF, &fb_dma_buf ) == 0 )
		{
			AINF("framebuffer accessed with dma buf (fd 0x%x)\n", (int)fb_dma_buf.fd);
			hnd->share_fd = fb_dma_buf.fd;
		}
	#endif
	}
#endif

	*pHandle = hnd;

	return 0;
}

static int gralloc_alloc_framebuffer(alloc_device_t* dev, size_t size, int usage, buffer_handle_t* pHandle)
{
	private_module_t* m = reinterpret_cast<private_module_t*>(dev->common.module);
	pthread_mutex_lock(&m->lock);
	int err = gralloc_alloc_framebuffer_locked(dev, size, usage, pHandle);
	pthread_mutex_unlock(&m->lock);
	return err;
}

static int alloc_device_alloc(alloc_device_t* dev, int w, int h, int format, int usage, buffer_handle_t* pHandle, int* pStride)
{
	ALOGD("%s w:%d, h:%d, format:%d usage:0x%x start",__FUNCTION__,w,h,format,usage);
	if (!pHandle || !pStride)
	{
		return -EINVAL;
	}
    
	if(w < 1 || h < 1)
	{
		return -EINVAL;
	}

	size_t size;
	size_t stride;
	if (format == HAL_PIXEL_FORMAT_YCbCr_420_SP || format == HAL_PIXEL_FORMAT_YCrCb_420_SP || format == HAL_PIXEL_FORMAT_YV12 )
	{
		switch (format)
		{
		case HAL_PIXEL_FORMAT_YCbCr_420_SP:
		case HAL_PIXEL_FORMAT_YCrCb_420_SP:
		case HAL_PIXEL_FORMAT_YV12:
			stride = GRALLOC_ALIGN(w, 16);
			size = h * (stride + GRALLOC_ALIGN(stride/2,16));

			break;
		default:
			return -EINVAL;
		}
	}
	else if(format == HAL_PIXEL_FORMAT_BLOB)
	{
		stride = GRALLOC_ALIGN(w, 16);
		size = w*h;
	}
	else
	{
		int align = 8;
		int bpp = 0;
		switch (format)
		{
		case HAL_PIXEL_FORMAT_RGBA_8888:
		case HAL_PIXEL_FORMAT_RGBX_8888:
		case HAL_PIXEL_FORMAT_BGRA_8888:
			bpp = 4;
			break;
		case HAL_PIXEL_FORMAT_RGB_888:
			bpp = 3;
			break;
		case HAL_PIXEL_FORMAT_RGB_565:
		case HAL_PIXEL_FORMAT_RGBA_5551:
		case HAL_PIXEL_FORMAT_RGBA_4444:
			bpp = 2;
			break;
		default:
			return -EINVAL;
		}
		size_t bpr = GRALLOC_ALIGN(w * bpp, 8);
		size = bpr * h;
		stride = bpr / bpp;
	}
#if SPRD_ION
	int preferIon = 0;
	private_module_t* m = reinterpret_cast<private_module_t*>(dev->common.module);
	if ((format == HAL_PIXEL_FORMAT_RGBA_8888)
		&& (((stride == m->info.xres) && (h == m->info.yres)) ||((h == m->info.xres) && (stride == m->info.yres)) )
		&& !(usage & GRALLOC_USAGE_HW_FB)) {
			//usage |= GRALLOC_USAGE_PRIVATE_0;
			//preferIon = 1;
			usage |= GRALLOC_USAGE_OVERLAY_BUFFER;
	}
#endif
	int err;
	#ifndef MALI_600
#if SPRD_ION
	if(usage & (GRALLOC_USAGE_PRIVATE_0 | GRALLOC_USAGE_PRIVATE_1))
	{
		 err = gralloc_alloc_ionbuffer(dev, size, usage, pHandle, preferIon);
		 if(err>=0){
			const native_handle_t *p_nativeh  = *pHandle;
			private_handle_t *hnd = (private_handle_t*)p_nativeh;
			hnd->format = format;
			hnd->width = stride;
			hnd->height = h;
			if (usage &  GRALLOC_USAGE_PRIVATE_1) {
				hnd->flags |= private_handle_t::PRIV_FLAGS_NOT_OVERLAY;
			}
#ifdef DRM_SPECIAL_PROCESS
                        if (!(usage & GRALLOC_USAGE_PROTECTED)) {
                                hnd->flags |= private_handle_t::PRIV_FLAGS_NOT_OVERLAY;
                        }
#endif
			if(preferIon)
			{
				m->mIonBufNum++;
				ALOGI("================allocat  ion memory for rgba xres*yres = %d*%d fd = %d:%d", m->info.xres,  m->info.yres, hnd->fd, m->mIonBufNum);
			}
		} else {
			if(preferIon) {
				ALOGI("================allocat  ion memory for rgba reserved buffer size too small,need to alloc normal buffer");
				goto AllocNormalBuffer;
			}
		}
	}
	else
#endif
	if (usage & GRALLOC_USAGE_HW_FB)
	{
		err = gralloc_alloc_framebuffer(dev, size, usage, pHandle);
	}
	else
	#endif
	{
AllocNormalBuffer:
		err = gralloc_alloc_buffer(dev, size, usage, pHandle);
		 if(err>=0){
			const native_handle_t *p_nativeh  = *pHandle;
			private_handle_t *hnd = (private_handle_t*)p_nativeh;
			hnd->format = format;
			hnd->width = stride;
			hnd->height = h;
		}
	}

	ALOGD("%s handle:0x%x end err is %d",__FUNCTION__,(unsigned int)*pHandle,err);
	if (err < 0)
	{
		return err;
	}

	/* match the framebuffer format */
	if (usage & GRALLOC_USAGE_HW_FB)
	{
		format = HAL_PIXEL_FORMAT_RGBA_8888;
	}

	private_handle_t *hnd = (private_handle_t *)*pHandle;
	int               private_usage = usage & (GRALLOC_USAGE_PRIVATE_0 |
	                                  GRALLOC_USAGE_PRIVATE_1);

	switch (private_usage)
	{
		case 0:
			hnd->yuv_info = MALI_YUV_BT601_NARROW;
			break;

		case GRALLOC_USAGE_PRIVATE_1:
			hnd->yuv_info = MALI_YUV_BT601_WIDE;
			break;

		case GRALLOC_USAGE_PRIVATE_0:
			hnd->yuv_info = MALI_YUV_BT709_NARROW;
			break;

		case (GRALLOC_USAGE_PRIVATE_0 | GRALLOC_USAGE_PRIVATE_1):
			hnd->yuv_info = MALI_YUV_BT709_WIDE;
			break;
	}

	hnd->width = w;
	hnd->height = h;
	hnd->format = format;
	hnd->stride = stride;

	*pStride = stride;
	return 0;
}

static int alloc_device_free(alloc_device_t* dev, buffer_handle_t handle)
{
	ALOGD("%s buffer_handle_t:0x%x start",__FUNCTION__,(unsigned int)handle);
	if (private_handle_t::validate(handle) < 0)
	{
		return -EINVAL;
	}

	private_handle_t const* hnd = reinterpret_cast<private_handle_t const*>(handle);
	ALOGD("%s buffer_handle_t:0x%x flags:0x%x  start",__FUNCTION__,(unsigned int)handle,hnd->flags);
		//LOGD("unmapping from %p, size=%d", base, size);

				// we can't deallocate the memory in case of UNMAP failure
				// because it would give that process access to someone else's
				// surfaces, which would be a security breach.
#if SPRD_ION
	if (hnd->flags & private_handle_t::PRIV_FLAGS_USES_PHY)
	{
		private_module_t* m = reinterpret_cast<private_module_t*>(dev->common.module);
		gralloc_module_t *module = &(m->base);
		module->perform(module,
					GRALLOC_MODULE_PERFORM_FREE_HANDLE,
					&handle);
		munmap((void *)hnd->base, hnd->size);
		ALOGI("FREE hnd=%p,fd=%d,offset=0x%x,size=%d,base=%x,phys_addr=0x%x",hnd,hnd->fd,hnd->offset,hnd->size,hnd->base,hnd->phyaddr);
		close(hnd->fd);
	       close_ion_device(m);
		if(hnd->format == HAL_PIXEL_FORMAT_RGBA_8888)
		{
			m->mIonBufNum--;
			ALOGI("================ free ion memory fd=%d:%d", hnd->fd, m->mIonBufNum);
		}
		/*
    		struct ion_fd_data fd_data;
    		fd_data.fd = hnd->fd;
    		int err = ioctl(m->mIonFd, ION_IOC_IMPORT, &fd_data);
		if(err) {
			ALOGE("ION_IOC_IMPORT fail");
		}

       	struct ion_handle_data handle_data;
		handle_data.handle = fd_data.handle;
		ioctl(m->mIonFd, ION_IOC_FREE, &handle_data);
		*/
	}
	else
#endif
	if (hnd->flags & private_handle_t::PRIV_FLAGS_FRAMEBUFFER)
	{
		// free this buffer
		private_module_t* m = reinterpret_cast<private_module_t*>(dev->common.module);
		const size_t bufferSize = m->finfo.line_length * m->info.yres;
		int index = (hnd->base - m->framebuffer->base) / bufferSize;
		m->bufferMask &= ~(1<<index);
		close(hnd->fd);

#if GRALLOC_ARM_UMP_MODULE
		if ( (int)UMP_INVALID_MEMORY_HANDLE != hnd->ump_mem_handle )
		{
			ump_reference_release((ump_handle)hnd->ump_mem_handle);
		}
#endif
	}
	else if (hnd->flags & private_handle_t::PRIV_FLAGS_USES_UMP)
	{
#if GRALLOC_ARM_UMP_MODULE
		/* Buffer might be unregistered so we need to check for invalid ump handle*/
		if ( (int)UMP_INVALID_MEMORY_HANDLE != hnd->ump_mem_handle )
		{
			ump_mapped_pointer_release((ump_handle)hnd->ump_mem_handle);
			ump_reference_release((ump_handle)hnd->ump_mem_handle);
		}
#else
		AERR( "Can't free ump memory for handle:0x%x. Not supported.", (unsigned int)hnd );
#endif
	}
	else if ( hnd->flags & private_handle_t::PRIV_FLAGS_USES_ION )
	{
#if GRALLOC_ARM_DMA_BUF_MODULE
		private_module_t* m = reinterpret_cast<private_module_t*>(dev->common.module);
		/* Buffer might be unregistered so we need to check for invalid ump handle*/
		if ( 0 != hnd->base )
		{
			ALOGD("%s the vadress 0x%x size of 0x%x will be free",__FUNCTION__,hnd->base,hnd->size);
			if ( 0 != munmap( (void*)hnd->base, hnd->size ) ) AERR( "Failed to munmap handle 0x%x", (unsigned int)hnd );
		}
		close( hnd->share_fd );
		if ( 0 != ion_free( m->ion_client, hnd->ion_hnd ) ) AERR( "Failed to ion_free( ion_client: %d ion_hnd: %p )", m->ion_client, hnd->ion_hnd );
		memset( (void*)hnd, 0, sizeof( *hnd ) );
#else 
		AERR( "Can't free dma_buf memory for handle:0x%x. Not supported.", (unsigned int)hnd );
#endif
		
	}

	delete hnd;

	ALOGD("%s end",__FUNCTION__);
	return 0;
}

static int alloc_device_close(struct hw_device_t *device)
{
	alloc_device_t* dev = reinterpret_cast<alloc_device_t*>(device);
	if (dev)
	{
#if GRALLOC_ARM_DMA_BUF_MODULE
		private_module_t *m = reinterpret_cast<private_module_t*>(device);
		if ( 0 != ion_close(m->ion_client) ) AERR( "Failed to close ion_client: %d", m->ion_client );
		close(m->ion_client);
#endif
		delete dev;
#if GRALLOC_ARM_UMP_MODULE
		ump_close(); // Our UMP memory refs will be released automatically here...
#endif
	}
	return 0;
}

int alloc_device_open(hw_module_t const* module, const char* name, hw_device_t** device)
{
	alloc_device_t *dev;

	dev = new alloc_device_t;
	if (NULL == dev)
	{
		return -1;
	}

#if GRALLOC_ARM_UMP_MODULE
	ump_result ump_res = ump_open();
	if (UMP_OK != ump_res)
	{
		AERR( "UMP open failed with %d", ump_res );
		delete dev;
		return -1;
	}
#endif

	/* initialize our state here */
	memset(dev, 0, sizeof(*dev));

	/* initialize the procs */
	dev->common.tag = HARDWARE_DEVICE_TAG;
	dev->common.version = 0;
	dev->common.module = const_cast<hw_module_t*>(module);
	dev->common.close = alloc_device_close;
	dev->alloc = alloc_device_alloc;
	dev->free = alloc_device_free;

#if GRALLOC_ARM_DMA_BUF_MODULE
	private_module_t *m = reinterpret_cast<private_module_t *>(dev->common.module);
	m->ion_client = ion_open();
	if ( m->ion_client < 0 )
	{
		AERR( "ion_open failed with %s", strerror(errno) );
		delete dev;
		return -1;
	}
#endif
	
	*device = &dev->common;

	return 0;
}
