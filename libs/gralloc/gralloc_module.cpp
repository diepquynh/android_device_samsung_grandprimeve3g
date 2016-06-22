/*
 * Copyright (C) 2010 ARM Limited. All rights reserved.
 *
 * Copyright (C) 2008 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * You may not use this file except in compliance with the License.
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

#include <errno.h>
#include <pthread.h>
#include <sys/ioctl.h>

#include <cutils/log.h>
#include <cutils/atomic.h>
#include <hardware/hardware.h>
#include <hardware/gralloc.h>

//#include <linux/ion.h>
#include "usr/include/linux/ion.h"
#include "ion_sprd.h"

#include "gralloc_priv.h"
#include "alloc_device.h"
#include "framebuffer_device.h"

#if GRALLOC_ARM_UMP_MODULE
#include <ump/ump_ref_drv.h>
static int s_ump_is_open = 0;
#endif

#if GRALLOC_ARM_DMA_BUF_MODULE
#include <linux/ion.h>
#include <ion/ion.h>
#include <sys/mman.h>
#endif

static pthread_mutex_t s_map_lock = PTHREAD_MUTEX_INITIALIZER;

extern int open_ion_device(private_module_t* m);
extern void close_ion_device(private_module_t* m);

static int gralloc_device_open(const hw_module_t* module, const char* name, hw_device_t** device)
{
	int status = -EINVAL;

	if (!strcmp(name, GRALLOC_HARDWARE_GPU0))
	{
		status = alloc_device_open(module, name, device);
	}
	else if (!strcmp(name, GRALLOC_HARDWARE_FB0))
	{
		status = framebuffer_device_open(module, name, device);
	}

	return status;
}

static int gralloc_register_buffer(gralloc_module_t const *module, buffer_handle_t handle)
{
	if (private_handle_t::validate(handle) < 0)
	{
		AERR("Registering invalid buffer 0x%x, returning error", (int)handle);
		return -EINVAL;
	}

	// if this handle was created in this process, then we keep it as is.
	private_handle_t *hnd = (private_handle_t *)handle;

	int retval = -EINVAL;

	pthread_mutex_lock(&s_map_lock);

#if GRALLOC_ARM_UMP_MODULE

	if (!s_ump_is_open)
	{
		ump_result res = ump_open(); // MJOLL-4012: UMP implementation needs a ump_close() for each ump_open

		if (res != UMP_OK)
		{
			pthread_mutex_unlock(&s_map_lock);
			AERR("Failed to open UMP library with res=%d", res);
			return retval;
		}

		s_ump_is_open = 1;
	}

#endif

	hnd->pid = getpid();

	if (hnd->flags & private_handle_t::PRIV_FLAGS_FRAMEBUFFER)
	{
		AERR("Can't register buffer 0x%x as it is a framebuffer", (unsigned int)handle);
	}
	else if (hnd->flags & private_handle_t::PRIV_FLAGS_USES_UMP)
	{
#if GRALLOC_ARM_UMP_MODULE
		hnd->ump_mem_handle = (int)ump_handle_create_from_secure_id(hnd->ump_id);

		if (UMP_INVALID_MEMORY_HANDLE != (ump_handle)hnd->ump_mem_handle)
		{
			hnd->base = (int)ump_mapped_pointer_get((ump_handle)hnd->ump_mem_handle);

			if (0 != hnd->base)
			{
				hnd->lockState = private_handle_t::LOCK_STATE_MAPPED;
				hnd->writeOwner = 0;
				hnd->lockState = 0;

				if((hnd->flags & private_handle_t::PRIV_FLAGS_USES_PHY)&&(0==hnd->resv0))
				{
					private_module_t* m = (private_module_t*)(module);
					if(open_ion_device(m))
					{
						ALOGE("open ion fail %s", __FUNCTION__);
					}else
					{
						struct ion_fd_data fd_data;
    						fd_data.fd = hnd->fd;
    						int err = ioctl(m->mIonFd, ION_IOC_IMPORT, &fd_data);
						if(err)
						{
							ALOGE("ION_IOC_IMPORT fail %x,phy addr = %x",hnd->resv0,hnd->phyaddr);
						}else
						{
							hnd->resv0 = (int)fd_data.handle;
							ALOGI("ION_IOC_IMPORT success %x,phy addr = %x",hnd->resv0,hnd->phyaddr);
						}
					}
				}

				pthread_mutex_unlock(&s_map_lock);
				return 0;
			}
			else
			{
				AERR("Failed to map UMP handle 0x%x", hnd->ump_mem_handle);
			}

			ump_reference_release((ump_handle)hnd->ump_mem_handle);
		}
		else
		{
			AERR("Failed to create UMP handle 0x%x", hnd->ump_mem_handle);
		}

#else
		AERR("Gralloc does not support UMP. Unable to register UMP memory for handle 0x%x", (unsigned int)hnd);
#endif
	}
	else if (hnd->flags & private_handle_t::PRIV_FLAGS_USES_ION)
	{
#if GRALLOC_ARM_DMA_BUF_MODULE
		int ret;
		unsigned char *mappedAddress;
		size_t size = hnd->size;
		hw_module_t *pmodule = NULL;
		private_module_t *m = NULL;

		if (hw_get_module(GRALLOC_HARDWARE_MODULE_ID, (const hw_module_t **)&pmodule) == 0)
		{
			m = reinterpret_cast<private_module_t *>(pmodule);
		}
		else
		{
			AERR("Could not get gralloc module for handle: 0x%x", (unsigned int)hnd);
			retval = -errno;
			goto cleanup;
		}

		/* the test condition is set to m->ion_client <= 0 here, because:
		 * 1) module structure are initialized to 0 if no initial value is applied
		 * 2) a second user process should get a ion fd greater than 0.
		 */
		if (m->ion_client <= 0)
		{
			/* a second user process must obtain a client handle first via ion_open before it can obtain the shared ion buffer*/
			m->ion_client = ion_open();

			if (m->ion_client < 0)
			{
				AERR("Could not open ion device for handle: 0x%x", (unsigned int)hnd);
				retval = -errno;
				goto cleanup;
			}
		}

		mappedAddress = (unsigned char *)mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, hnd->share_fd, 0);

		if (MAP_FAILED == mappedAddress)
		{
			AERR("mmap( share_fd:%d ) failed with %s",  hnd->share_fd, strerror(errno));
			retval = -errno;
			goto cleanup;
		}

		hnd->base = intptr_t(mappedAddress) + hnd->offset;
		pthread_mutex_unlock(&s_map_lock);
		return 0;
#endif
	}
	else
	{
		AERR("registering non-UMP buffer not supported. flags = %d", hnd->flags);
	}

cleanup:
	pthread_mutex_unlock(&s_map_lock);
	return retval;
}

static int gralloc_unregister_buffer(gralloc_module_t const *module, buffer_handle_t handle)
{
	if (private_handle_t::validate(handle) < 0)
	{
		AERR("unregistering invalid buffer 0x%x, returning error", (int)handle);
		return -EINVAL;
	}

	private_handle_t *hnd = (private_handle_t *)handle;

	AERR_IF(hnd->lockState & private_handle_t::LOCK_STATE_READ_MASK, "[unregister] handle %p still locked (state=%08x)", hnd, hnd->lockState);

	if (hnd->flags & private_handle_t::PRIV_FLAGS_FRAMEBUFFER)
	{
		AERR("Can't unregister buffer 0x%x as it is a framebuffer", (unsigned int)handle);
	}
	else if (hnd->pid == getpid()) // never unmap buffers that were not registered in this process
	{
		pthread_mutex_lock(&s_map_lock);

		if (hnd->flags & private_handle_t::PRIV_FLAGS_USES_UMP)
		{
#if GRALLOC_ARM_UMP_MODULE
			ump_mapped_pointer_release((ump_handle)hnd->ump_mem_handle);
			hnd->base = 0;
			ump_reference_release((ump_handle)hnd->ump_mem_handle);
			hnd->ump_mem_handle = (int)UMP_INVALID_MEMORY_HANDLE;
#else
			AERR("Can't unregister UMP buffer for handle 0x%x. Not supported", (unsigned int)handle);
#endif
		}
		else if (hnd->flags & private_handle_t::PRIV_FLAGS_USES_ION)
		{
#if GRALLOC_ARM_DMA_BUF_MODULE
			void *base = (void *)hnd->base;
			size_t size = hnd->size;

			if (munmap(base, size) < 0)
			{
				AERR("Could not munmap base:0x%x size:%d '%s'", (unsigned int)base, size, strerror(errno));
			}

#else
			AERR("Can't unregister DMA_BUF buffer for hnd %p. Not supported", hnd);
#endif

		}
		else
		{
			AERR("Unregistering unknown buffer is not supported. Flags = %d", hnd->flags);
		}

		hnd->base = 0;
		hnd->lockState	= 0;
		hnd->writeOwner = 0;
#if SPRD_ION
		if((hnd->flags & private_handle_t::PRIV_FLAGS_USES_PHY)&&(0!=hnd->resv0))
		{
			private_module_t* m = (private_module_t*)(module);
			if(open_ion_device(m))
			{
				ALOGE("open ion fail %s", __FUNCTION__);
			}else
			{
       			struct ion_handle_data handle_data;
				handle_data.handle = (struct ion_handle *)hnd->resv0;
				ioctl(m->mIonFd, ION_IOC_FREE, &handle_data);
				close_ion_device(m);
				ALOGI("ION_IOC_FREE success %x,phy addr = %x",hnd->resv0,hnd->phyaddr);
			}
		}
#endif
		pthread_mutex_unlock(&s_map_lock);
	}
	else
	{
		AERR("Trying to unregister buffer 0x%x from process %d that was not created in current process: %d", (unsigned int)hnd, hnd->pid, getpid());
	}

	return 0;
}

static int gralloc_lock(gralloc_module_t const *module, buffer_handle_t handle, int usage, int l, int t, int w, int h, void **vaddr)
{
	if (private_handle_t::validate(handle) < 0)
	{
		AERR("Locking invalid buffer 0x%x, returning error", (int)handle);
		return -EINVAL;
	}

	private_handle_t *hnd = (private_handle_t *)handle;

	if (hnd->flags & private_handle_t::PRIV_FLAGS_USES_UMP || hnd->flags & private_handle_t::PRIV_FLAGS_USES_ION)
	{
		hnd->writeOwner = usage & GRALLOC_USAGE_SW_WRITE_MASK;
	}

	if (usage & (GRALLOC_USAGE_SW_READ_MASK | GRALLOC_USAGE_SW_WRITE_MASK))
	{
		*vaddr = (void *)hnd->base;
#if GRALLOC_ARM_DMA_BUF_MODULE
		hw_module_t *pmodule = NULL;
		private_module_t *m = NULL;

		if (hw_get_module(GRALLOC_HARDWARE_MODULE_ID, (const hw_module_t **)&pmodule) == 0)
		{
			m = reinterpret_cast<private_module_t *>(pmodule);
			ion_invalidate_fd(m->ion_client, hnd->share_fd);
		}
		else
		{
			AERR("lock couldnot get gralloc module for handle 0x%x\n", (unsigned int)handle);
		}
#endif
	}

	return 0;
}



static int gralloc_lock_ycbcr(struct gralloc_module_t const* module,
            buffer_handle_t handle, int usage,
            int l, int t, int w, int h,
            struct android_ycbcr *ycbcr)
{
    if (private_handle_t::validate(handle) < 0)
    {
        AERR("Locking invalid buffer 0x%x, returning error", (int)handle );
        return -EINVAL;
    }
    private_handle_t* hnd = (private_handle_t*)handle;
    int ystride;
    int err=0;

    switch (hnd->format) {
        case HAL_PIXEL_FORMAT_YCrCb_420_SP:
            ystride = GRALLOC_ALIGN(hnd->width, 16);
            ycbcr->y  = (void*)hnd->base;
            ycbcr->cr = (void*)(hnd->base + ystride * hnd->height);
            ycbcr->cb = (void*)(hnd->base + ystride * hnd->height + 1);
            ycbcr->ystride = ystride;
            ycbcr->cstride = ystride;
            ycbcr->chroma_step = 2;
            memset(ycbcr->reserved, 0, sizeof(ycbcr->reserved));
            break;
        case HAL_PIXEL_FORMAT_YCbCr_420_SP:
            ystride = GRALLOC_ALIGN(hnd->width, 16);
            ycbcr->y  = (void*)hnd->base;
            ycbcr->cb = (void*)(hnd->base + ystride * hnd->height);
            ycbcr->cr = (void*)(hnd->base + ystride * hnd->height + 1);
            ycbcr->ystride = ystride;
            ycbcr->cstride = ystride;
            ycbcr->chroma_step = 2;
            memset(ycbcr->reserved, 0, sizeof(ycbcr->reserved));
            break;
        default:
            ALOGD("%s: Invalid format passed: 0x%x", __FUNCTION__,
                  hnd->format);
            err = -EINVAL;
    }

    return err;
}



static int gralloc_unlock(gralloc_module_t const* module, buffer_handle_t handle)
{
	if (private_handle_t::validate(handle) < 0)
	{
		AERR("Unlocking invalid buffer 0x%x, returning error", (int)handle);
		return -EINVAL;
	}

	private_handle_t *hnd = (private_handle_t *)handle;
	int32_t current_value;
	int32_t new_value;
	int retry;

	if (hnd->flags & private_handle_t::PRIV_FLAGS_USES_UMP && hnd->writeOwner)
	{
#if GRALLOC_ARM_UMP_MODULE
		ump_cpu_msync_now((ump_handle)hnd->ump_mem_handle, UMP_MSYNC_CLEAN_AND_INVALIDATE, (void *)hnd->base, hnd->size);
#else
		AERR("Buffer 0x%x is UMP type but it is not supported", (unsigned int)hnd);
#endif
	} else if ( hnd->flags & private_handle_t::PRIV_FLAGS_USES_ION && hnd->writeOwner)
	{
#if GRALLOC_ARM_DMA_BUF_MODULE
		hw_module_t *pmodule = NULL;
		private_module_t *m = NULL;

		if (hw_get_module(GRALLOC_HARDWARE_MODULE_ID, (const hw_module_t **)&pmodule) == 0)
		{
			m = reinterpret_cast<private_module_t *>(pmodule);
			ion_sync_fd(m->ion_client, hnd->share_fd);
		}
		else
		{
			AERR("Unlock couldnot get gralloc module for handle 0x%x\n", (unsigned int)handle);
		}

#endif
	}

	return 0;
}

int gralloc_perform(struct gralloc_module_t const* module,
		int operation, ... )
{
	int res = -EINVAL;
	va_list args;
	va_start(args, operation);
#if GRALLOC_ARM_UMP_MODULE
	switch (operation) {
		case GRALLOC_MODULE_PERFORM_CREATE_HANDLE_FROM_BUFFER:
		{
			int fd = va_arg(args, int);
			size_t size = va_arg(args, size_t);
			size_t offset = va_arg(args, size_t);
			void* base = va_arg(args, void*);
			native_handle_t** handle = va_arg(args, native_handle_t**);
			unsigned long phys_addr = 0;
			ump_handle ump_h;

			// it's a HACK
			// we always return OK even the mapping is invalidate because
			// most operation still can be done by glTexImage2D rather than
			// purely software, the *handle will reflect the success/fail
			// situation

			private_module_t* m = (private_module_t*)(module);

			if(open_ion_device(m))
			{
				ALOGE("open ion fail %s", __FUNCTION__);
				break;
			}
	 		struct ion_phys_data phys_data;
	 		struct ion_custom_data  custom_data;
			phys_data.fd_buffer = fd;
			custom_data.cmd = ION_SPRD_CUSTOM_PHYS;
			custom_data.arg = (unsigned long)&phys_data;
			int err = ioctl(m->mIonFd,ION_IOC_CUSTOM,&custom_data);
			if(err){
				break;
			}
	 		phys_addr = phys_data.phys + offset;

			// align offset to page
			size_t start = phys_addr & ~4095;
			size_t bias = phys_addr - start;
			// align size to page
			size = ((size + bias) + 4095) & ~4095;

			ump_h = ump_handle_create_from_phys_block(start, size);

			if (UMP_INVALID_MEMORY_HANDLE == ump_h) {
				ALOGE("UMP Memory handle invalid\n");
				break;
			}
			private_handle_t* hnd =
				new private_handle_t(private_handle_t::PRIV_FLAGS_USES_UMP | private_handle_t::PRIV_FLAGS_USES_PHY, 0,
									size,
									intptr_t(base) + (offset - bias),
									private_handle_t::LOCK_STATE_MAPPED,
									ump_secure_id_get(ump_h),
									ump_h,
									bias,
									fd);
			hnd->phyaddr = phys_addr;
			hnd->resv0 = 0;
			AINF("PERFORM_CREATE hnd=%p,fd=%d,offset=0x%x,size=%d,base=%p,phys_addr=0x%lx",hnd,fd,offset,size,base,phys_addr);
			*handle = (native_handle_t *)hnd;
			res = 0;
			break;
		}

		case GRALLOC_MODULE_PERFORM_FREE_HANDLE:
		{
			native_handle_t** handle = va_arg(args, native_handle_t**);
			private_handle_t* hnd = (private_handle_t *)*handle;
			ump_free_handle_from_mapped_phys_block((ump_handle)hnd->ump_mem_handle);
			res = 0;
			break;
		}

		case GRALLOC_MODULE_PERFORM_GET_MALI_DATA:
		{
			ump_handle *ump_h = va_arg(args, ump_handle*);
			native_handle_t* handle = va_arg(args, native_handle_t*);
			private_handle_t* hnd = (private_handle_t *)handle;

			if (hnd->flags & private_handle_t::PRIV_FLAGS_FRAMEBUFFER) {
				*ump_h = UMP_INVALID_MEMORY_HANDLE;
				res = 0;
			}
			else if (hnd->flags & private_handle_t::PRIV_FLAGS_USES_UMP) {
				*ump_h = (ump_handle)hnd->ump_mem_handle;
				res = 0;
			}
			else {
				ALOGE("GRALLOC_MODULE_PERFORM_GET_MALI_DATA:"
					 " gralloc_priv handle invalid\n");
			}
			break;
		}

		case GRALLOC_MODULE_GET_MALI_INTERNAL_BUF_OFF:
		{
			uint32_t *offset = va_arg(args, uint32_t*);
			native_handle_t* handle = va_arg(args, native_handle_t*);
			private_handle_t* hnd = (private_handle_t *)handle;
			if (hnd->flags & private_handle_t::PRIV_FLAGS_USES_UMP) {
				*offset = hnd->offset;
				res = 0;
			}
			else {
				*offset = 0;
				ALOGE("GRALLOC_MODULE_GET_MALI_INTERNAL_BUF_OFF"
					 " gralloc_priv handle invalid\n");
			}
			break;
		}
	}
#endif
	va_end(args);
	return res;
}


// There is one global instance of the module

static struct hw_module_methods_t gralloc_module_methods =
{
open:
	gralloc_device_open
};

private_module_t::private_module_t()
{
#define INIT_ZERO(obj) (memset(&(obj),0,sizeof((obj))))

	base.common.tag = HARDWARE_MODULE_TAG;
	base.common.version_major = 1;
	base.common.version_minor = 0;
	base.common.id = GRALLOC_HARDWARE_MODULE_ID;
	base.common.name = "Graphics Memory Allocator Module";
	base.common.author = "ARM Ltd.";
	base.common.methods = &gralloc_module_methods;
	base.common.dso = NULL;
	INIT_ZERO(base.common.reserved);

	base.registerBuffer = gralloc_register_buffer;
	base.unregisterBuffer = gralloc_unregister_buffer;
	base.lock = gralloc_lock;
	base.lock_ycbcr = gralloc_lock_ycbcr;
	base.unlock = gralloc_unlock;
	base.perform = gralloc_perform;
	INIT_ZERO(base.reserved_proc);

	framebuffer = NULL;
	fbFormat = 0;
	flags = 0;
	numBuffers = 0;
	bufferMask = 0;
	pthread_mutex_init(&(lock), NULL);
	pthread_mutex_init(&(fd_lock), NULL);
	currentBuffer = NULL;
	INIT_ZERO(info);
	INIT_ZERO(finfo);
	xdpi = 0.0f;
	ydpi = 0.0f;
	fps = 0.0f;
#if SPRD_ION
	mIonFd = -1;
	mIonBufNum = 0;
#endif

#undef INIT_ZERO
};

/*
 * HAL_MODULE_INFO_SYM will be initialized using the default constructor
 * implemented above
 */
struct private_module_t HAL_MODULE_INFO_SYM;

