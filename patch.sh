#!/bin/bash
LOCALDIR=$(pwd)

# Copy the patches
cp ${LOCALDIR}/patches/build.patch ../../../build
cp ${LOCALDIR}/patches/external_tinyalsa.patch ../../../external/tinyalsa
cp ${LOCALDIR}/patches/frameworks_av.patch ../../../frameworks/av
cp ${LOCALDIR}/patches/frameworks_base.patch ../../../frameworks/base
cp ${LOCALDIR}/patches/system_core.patch ../../../system/core

# Apply them
cd ../../..
cd build && patch -p1 < *.patch
cd ../external/tinyalsa && patch -p1 < *.patch
cd ../../frameworks/av && patch -p1 < *.patch
cd ../../frameworks/base && patch -p1 < *.patch