/*
 * Copyright (C) 2008 The Android Open Source Project
 * Copyright (c) 2011, Code Aurora Forum. All rights reserved.
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

#define LOG_TAG "MemoryHeapIon"

#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#include <cutils/log.h>

#include "MemoryHeapIon.h"
#include <binder/MemoryHeapBase.h>

#ifdef USE_TARGET_SIMULATOR_MODE
#include <linux/ion.h>
//modify for make sdk
struct ion_phys_data {
    int fd_buffer;
    unsigned long phys;
    size_t size;
};

struct ion_msync_data {
    int fd_buffer;
    void *vaddr;
    void *paddr;
    size_t size;
};

enum ION_SPRD_CUSTOM_CMD {
    ION_SPRD_CUSTOM_PHYS,
    ION_SPRD_CUSTOM_MSYNC
};
#else
#include <linux/ion.h>
#include <video/ion_sprd.h>
#endif

namespace android {

int MemoryHeapIon::Get_phy_addr_from_ion(int buffer_fd, int *phy_addr, int *size) {
	int fd = open("/dev/ion", O_SYNC);

	if(fd<0){
		ALOGE("%s:open dev ion error!",__func__);
		return -1;
	} else {
		struct ion_phys_data phys_data;
		struct ion_custom_data  custom_data;
		phys_data.fd_buffer = buffer_fd;
		custom_data.cmd = ION_SPRD_CUSTOM_PHYS;
		custom_data.arg = (unsigned long)&phys_data;
		*phy_addr = phys_data.phys;
		*size = phys_data.size;
		close(fd);
	}
	return 0;
}

int MemoryHeapIon::get_phy_addr_from_ion(int *phy_addr, int *size) {
	if(mIonDeviceFd<0) {
		ALOGE("%s:open dev ion error!",__func__);
		return -1;
	} else {
		struct ion_phys_data phys_data;
		struct ion_custom_data  custom_data;
		phys_data.fd_buffer = MemoryHeapBase::getHeapID();
		custom_data.cmd = ION_SPRD_CUSTOM_PHYS;
		custom_data.arg = (unsigned long)&phys_data;
		*phy_addr = phys_data.phys;
		*size = phys_data.size;
	}
	return 0;
}

int MemoryHeapIon::get_gsp_iova(int *mmu_addr, int *size) {
	if(mIonDeviceFd<0) {
		ALOGE("%s:open dev ion error!",__func__);
		return -1;
	} else {
		struct ion_mmu_data mmu_data;
		struct ion_custom_data  custom_data;
		mmu_data.fd_buffer = MemoryHeapBase::getHeapID();
		custom_data.cmd = ION_SPRD_CUSTOM_GSP_MAP;
		custom_data.arg = (unsigned long)&mmu_data;
		*mmu_addr = mmu_data.iova_addr;
		*size = mmu_data.iova_size;
	}
	return 0;
}

int MemoryHeapIon::free_gsp_iova(int mmu_addr, int size) {
	if(mIonDeviceFd<0) {
		ALOGE("%s:open dev ion error!",__func__);
		return -1;
	} else {
		struct ion_mmu_data mmu_data;
		struct ion_custom_data  custom_data;
		mmu_data.fd_buffer = MemoryHeapBase::getHeapID();
		mmu_data.iova_addr = mmu_addr;
		mmu_data.iova_size = size;
		custom_data.cmd = ION_SPRD_CUSTOM_GSP_UNMAP;
		custom_data.arg = (unsigned long)&mmu_data;
	}
	return 0;
}

int MemoryHeapIon::get_mm_iova(int *mmu_addr, int *size) {
	if(mIonDeviceFd<0) {
		ALOGE("%s:open dev ion error!",__func__);
		return -1;
	} else {
		struct ion_mmu_data mmu_data;
		struct ion_custom_data  custom_data;
		mmu_data.fd_buffer = MemoryHeapBase::getHeapID();
		custom_data.cmd = ION_SPRD_CUSTOM_MM_MAP;
		custom_data.arg = (unsigned long)&mmu_data;
		*mmu_addr = mmu_data.iova_addr;
		*size = mmu_data.iova_size;
	}
	return 0;
}

int MemoryHeapIon::free_mm_iova(int mmu_addr, int size) {
	if(mIonDeviceFd<0) {
		ALOGE("%s:open dev ion error!",__func__);
		return -1;
	} else {
		struct ion_mmu_data mmu_data;
		struct ion_custom_data  custom_data;
		mmu_data.fd_buffer = MemoryHeapBase::getHeapID();
		mmu_data.iova_addr = mmu_addr;
		mmu_data.iova_size = size;
		custom_data.cmd = ION_SPRD_CUSTOM_MM_UNMAP;
		custom_data.arg = (unsigned long)&mmu_data;
	}
	return 0;
}

int MemoryHeapIon::Get_gsp_iova(int buffer_fd, int *mmu_addr, int *size) {
	int fd = open("/dev/ion", O_SYNC);
	if(fd<0) {
		ALOGE("%s:open dev ion error!",__func__);
		return -1;
	} else {
		struct ion_mmu_data mmu_data;
		struct ion_custom_data  custom_data;
		mmu_data.fd_buffer = buffer_fd;
		custom_data.cmd = ION_SPRD_CUSTOM_GSP_MAP;
		custom_data.arg = (unsigned long)&mmu_data;
		*mmu_addr = mmu_data.iova_addr;
		*size = mmu_data.iova_size;
		close(fd);
	}
	return 0;
}

int MemoryHeapIon::Get_mm_iova(int buffer_fd, int *mmu_addr, int *size) {
	int fd = open("/dev/ion", O_SYNC);
	if(fd<0) {
		ALOGE("%s:open dev ion error!",__func__);
		return -1;
	} else {
		struct ion_mmu_data mmu_data;
		struct ion_custom_data  custom_data;

		mmu_data.fd_buffer =  buffer_fd;
		custom_data.cmd = ION_SPRD_CUSTOM_MM_MAP;
		custom_data.arg = (unsigned long)&mmu_data;
		*mmu_addr = mmu_data.iova_addr;
		*size = mmu_data.iova_size;
		close(fd);
	}
	return 0;
}

int MemoryHeapIon::Free_gsp_iova(int buffer_fd, int mmu_addr, int size){
	int fd = open("/dev/ion", O_SYNC);
	if(fd<0) {
		ALOGE("%s:open dev ion error!",__func__);
		return -1;
	} else {
		struct ion_mmu_data mmu_data;
		struct ion_custom_data  custom_data;

		mmu_data.fd_buffer = buffer_fd;
		mmu_data.iova_addr = mmu_addr;
		mmu_data.iova_size = size;
		custom_data.cmd = ION_SPRD_CUSTOM_GSP_UNMAP;
		custom_data.arg = (unsigned long)&mmu_data;
		close(fd);
	}
	return 0;
}

int MemoryHeapIon::Free_mm_iova(int buffer_fd, int mmu_addr, int size){
	int fd = open("/dev/ion", O_SYNC);
	if(fd<0) {
		ALOGE("%s:open dev ion error!",__func__);
		return -1;
	} else {
		struct ion_mmu_data mmu_data;
		struct ion_custom_data  custom_data;

		mmu_data.fd_buffer = buffer_fd;
		mmu_data.iova_addr = mmu_addr;
		mmu_data.iova_size = size;
		custom_data.cmd = ION_SPRD_CUSTOM_MM_UNMAP;
		custom_data.arg = (unsigned long)&mmu_data;
		close(fd);
	}
	return 0;
}

bool MemoryHeapIon::Gsp_iommu_is_enabled(void)
{
	if(access("/dev/sprd_iommu_gsp",F_OK)<0)
	{
		return false;
	}
	return true;
}

bool MemoryHeapIon::Mm_iommu_is_enabled(void)
{
	if(access("/dev/sprd_iommu_mm",F_OK)<0)
	{
		return false;
	}
	return true;
}

int  MemoryHeapIon::Flush_ion_buffer(int buffer_fd, void *v_addr, void *p_addr, int size){
	int fd = open("/dev/ion", O_SYNC);
	if(fd<0) {
		ALOGE("%s:open dev ion error!",__func__);
		return -1;
	} else {
		struct ion_msync_data msync_data;
		struct ion_custom_data  custom_data;

		msync_data.fd_buffer = buffer_fd;
		msync_data.vaddr = v_addr;
		msync_data.paddr = p_addr;
		msync_data.size = size;
		custom_data.cmd = ION_SPRD_CUSTOM_MSYNC;
		custom_data.arg = (unsigned long)&msync_data;
		close(fd);
	}
	return 0;
}

int MemoryHeapIon::flush_ion_buffer(void *v_addr, void *p_addr, int size) {
	if(mIonDeviceFd<0) {
		return -1;
	} else {
		struct ion_msync_data msync_data;
		struct ion_custom_data  custom_data;
		if (((v_addr) < (MemoryHeapBase::getBase())) || ((((char *)v_addr) + size) > (((char *)MemoryHeapBase::getBase()) + MemoryHeapBase::getSize()))) {
			ALOGE("flush_ion_buffer error: mBase=0x%x, mSize=0x%x", MemoryHeapBase::getBase(), MemoryHeapBase::getSize());
			ALOGE("flush_ion_buffer error: v_addr=0x%x, p_addr=0x%x, size=0x%x",v_addr, p_addr, size);
			return -3;
		}
	msync_data.fd_buffer = MemoryHeapBase::getHeapID();
	msync_data.vaddr = v_addr;
	msync_data.paddr = p_addr;
	msync_data.size = size;
	custom_data.cmd = ION_SPRD_CUSTOM_MSYNC;
	custom_data.arg = (unsigned long)&msync_data;
	}
	return 0;
}

MemoryHeapIon::MemoryHeapIon() : mIonDeviceFd(-1)
{
}

MemoryHeapIon::MemoryHeapIon(const char* device, size_t size,
			     uint32_t flags, unsigned long memory_types) : MemoryHeapBase()
{
	int open_flags = O_RDONLY;

	if (flags & NO_CACHING)
		open_flags |= O_SYNC;

	int fd = open(device, open_flags);
	if (fd >= 0) {
		const size_t pagesize = getpagesize();
		size = ((size + pagesize-1) & ~(pagesize-1));
		if (mapIonFd(fd, size, memory_types, flags) == NO_ERROR) {
			MemoryHeapBase::setDevice(device);
		}
	} else {
		ALOGE("open ion fail");
	}
}

status_t MemoryHeapIon::ionInit(int ionFd, void *base, int size, int flags,
			        const char* device, ion_user_handle_t handle,
			        int ionMapFd) {
	mIonDeviceFd = ionFd;
	mIonHandle = handle;
	MemoryHeapBase::init(ionMapFd, base, size, flags, device);
	return NO_ERROR;
}


status_t MemoryHeapIon::mapIonFd(int fd, size_t size, unsigned long memory_type, int uflags)
{
	/* If size is 0, just fail the mmap. There is no way to get the size
	* with ion
	*/
	int map_fd;

	struct ion_allocation_data data;
	struct ion_fd_data fd_data;
	struct ion_handle_data handle_data;
	void *base = NULL;

	data.len = size;
	data.align = getpagesize();
#if (ION_DRIVER_VERSION == 1)
	data.heap_id_mask = memory_type;
	//if cached buffer , force set the lowest two bits 11
	if((memory_type&(1<<31))) {
		data.flags = ((memory_type&(1<<31)) | 3);
	} else {
		data.flags = 0;
	}
#else
	data.flags = memory_type;
#endif

	if (ioctl(fd, ION_IOC_ALLOC, &data) < 0) {
		ALOGE("%s: ION_IOC_ALLOC error!",__func__);
		close(fd);
		return -errno;
	}

	if ((uflags & DONT_MAP_LOCALLY) == 0) {
		int flags = 0;

		fd_data.handle = data.handle;

		if (ioctl(fd, ION_IOC_SHARE, &fd_data) < 0) {
			ALOGE("%s: ION_IOC_SHARE error!",__func__);
			handle_data.handle = data.handle;
			ioctl(fd, ION_IOC_FREE, &handle_data);
			close(fd);
			return -errno;
		}

		base = (uint8_t*)mmap(0, size,
			PROT_READ|PROT_WRITE, MAP_SHARED|flags, fd_data.fd, 0);
		if (base == MAP_FAILED) {
			ALOGE("mmap(fd=%d, size=%u) failed (%s)",
			fd, uint32_t(size), strerror(errno));
			handle_data.handle = data.handle;
			ioctl(fd, ION_IOC_FREE, &handle_data);
			close(fd);
			return -errno;
		}
	}
	mIonHandle = data.handle;
	mIonDeviceFd = fd;

	/*
	* Call this with NULL now and set device with set_device
	* above for consistency sake with how MemoryHeapPmem works.
	*/
	MemoryHeapBase::init(fd_data.fd, base, size, uflags, NULL);

	return NO_ERROR;
}

MemoryHeapIon::~MemoryHeapIon() {
	struct ion_handle_data data;

	data.handle = mIonHandle;

	/*
	* Due to the way MemoryHeapBase is set up, munmap will never
	* be called so we need to call it ourselves here.
	*/
	munmap(MemoryHeapBase::getBase(), MemoryHeapBase::getSize());
	if (mIonDeviceFd > 0) {
		ioctl(mIonDeviceFd, ION_IOC_FREE, &data);
		close(mIonDeviceFd);
	}
}

// ---------------------------------------------------------------------------
}; // namespace android
