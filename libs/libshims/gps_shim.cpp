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

#include <stdlib.h>
#include <malloc.h>

extern "C" int _ZN7android13SensorManager5sLockE;
extern "C" int _ZN7android9SingletonINS_13SensorManagerEE5sLockE = _ZN7android13SensorManager5sLockE;

extern "C" int _ZN7android13SensorManager17sPackageInstancesE;
extern "C" int _ZN7android9SingletonINS_13SensorManagerEE9sInstanceE = _ZN7android13SensorManager17sPackageInstancesE;

extern "C" int _ZN7android13SensorManagerD1Ev();
extern "C" int _ZN7android13SensorManagerC1Ev() {
    return _ZN7android13SensorManagerD1Ev();
}

extern "C" int _ZN7android13SensorManager16createEventQueueENS_7String8Ei();
extern "C" int _ZN7android13SensorManager16createEventQueueEv() {
    return _ZN7android13SensorManager16createEventQueueENS_7String8Ei();
}

extern "C" void *CRYPTO_malloc(int num, const char *file, int line);
extern "C" void *CRYPTO_malloc(int num, const char *file, int line) {
    return num <= 0 ? NULL : malloc(num);
}
