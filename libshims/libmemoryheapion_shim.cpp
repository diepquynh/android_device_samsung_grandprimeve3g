/*
 * Copyright (C) 2016 The Android Open Source Project
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

#include <utils/Singleton.h>

extern "C" int _ZN7android13MemoryHeapIon21Get_phy_addr_from_ionEiPmPj();
extern "C" int _ZN7android13MemoryHeapIon21Get_phy_addr_from_ionEiPiS1_() {
	return _ZN7android13MemoryHeapIon21Get_phy_addr_from_ionEiPmPj();
}

extern "C" int _ZN7android13MemoryHeapIon21get_phy_addr_from_ionEPmPj();
extern "C" int _ZN7android13MemoryHeapIon21get_phy_addr_from_ionEPiS1_() {
	return _ZN7android13MemoryHeapIon21get_phy_addr_from_ionEPmPj();
}

extern "C" int _ZN7android13MemoryHeapIon16Flush_ion_bufferEiPvS1_j();
extern "C" int _ZN7android13MemoryHeapIon16Flush_ion_bufferEiPvS1_i() {
	return _ZN7android13MemoryHeapIon16Flush_ion_bufferEiPvS1_j();
}

extern "C" int _ZN7android13MemoryHeapIon16flush_ion_bufferEPvS1_j();
extern "C" int _ZN7android13MemoryHeapIon16flush_ion_bufferEPvS1_i() {
	return _ZN7android13MemoryHeapIon16flush_ion_bufferEPvS1_j();
}

extern "C" int _ZN7android13MemoryHeapIon12get_gsp_iovaEPmPj();
extern "C" int _ZN7android13MemoryHeapIon12get_gsp_iovaEPiS1_() {
	return _ZN7android13MemoryHeapIon12get_gsp_iovaEPmPj();
}

extern "C" int _ZN7android13MemoryHeapIon13free_gsp_iovaEmj();
extern "C" int _ZN7android13MemoryHeapIon13free_gsp_iovaEii() {
	return _ZN7android13MemoryHeapIon13free_gsp_iovaEmj();
}

extern "C" int _ZN7android13MemoryHeapIon11get_mm_iovaEPmPj();
extern "C" int _ZN7android13MemoryHeapIon11get_mm_iovaEPiS1_() {
	return _ZN7android13MemoryHeapIon11get_mm_iovaEPmPj();
}

extern "C" int _ZN7android13MemoryHeapIon12free_mm_iovaEmj();
extern "C" int _ZN7android13MemoryHeapIon12free_mm_iovaEii() {
	return _ZN7android13MemoryHeapIon12free_mm_iovaEmj();
}

extern "C" int _ZN7android13MemoryHeapIon12Get_gsp_iovaEiPmPj();
extern "C" int _ZN7android13MemoryHeapIon12Get_gsp_iovaEiPiS1_() {
	return _ZN7android13MemoryHeapIon12Get_gsp_iovaEiPmPj();
}

extern "C" int _ZN7android13MemoryHeapIon11Get_mm_iovaEiPmPj();
extern "C" int _ZN7android13MemoryHeapIon11Get_mm_iovaEiPiS1_() {
	return _ZN7android13MemoryHeapIon11Get_mm_iovaEiPmPj();
}

extern "C" int _ZN7android13MemoryHeapIon13Free_gsp_iovaEimj();
extern "C" int _ZN7android13MemoryHeapIon13Free_gsp_iovaEiii() {
	return _ZN7android13MemoryHeapIon13Free_gsp_iovaEimj();
}

extern "C" int _ZN7android13MemoryHeapIon12Free_mm_iovaEimj();
extern "C" int _ZN7android13MemoryHeapIon12Free_mm_iovaEiii() {
	return _ZN7android13MemoryHeapIon12Free_mm_iovaEimj();
}
