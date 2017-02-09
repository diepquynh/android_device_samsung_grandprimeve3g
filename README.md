##Device configuration for Samsung Galaxy Grand Prime VE SPRD SM-G531H (grandprimeve3g)

=====================================

Basic   | Spec Sheet
-------:|:-------------------------
CPU     | Quad-core 1,3GHz Cortex-A7
CHIPSET | Spreadtrum SC7730SE sc8830
GPU     | Mali-400MP2
Memory  | 1 GB
Shipped Android Version | Android 5.1.1 with TouchWiz Essence
Storage | 8 GB
MicroSD | Up to 64 GB
Battery | 2600 mAh Li-Ion (removable)
Dimensions | 144.8 x 72.1 x 8.6 mm
Display | 540 x 960 pixels, 5.0"
Rear Camera  | 8.0 MP, LED flash
Front Camera | 5.0 MP
Release Date | June 2015

##Building instructions

### What do you need?
* 50GB left of your hard disk space
* Basic skills / knowledge of Linux

### Building steps
* 1. Sync Android source
* 2. Copy this file ([grandprimeve3g.xml](https://github.com/remilia15/android_local_manifests/raw/cm-13.0-grandprimeve3g/grandprimeve3g.xml)) to `.repo/local_manifests` (if that folder doesn't exist then "mkdir" it)
* 3. `repo sync` again
* 4. After syncing, type `. build/envsetup.sh && brunch grandprimeve3g`
* 5. Wait until your build is done. This step might take hours or minutes depends on your computer specs
