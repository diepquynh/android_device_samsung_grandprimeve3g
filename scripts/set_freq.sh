#!/system/xbin/bash

devNode=/sys/devices/platform/scxx30-dmcfreq.0/devfreq/scxx30-dmcfreq.0/ondemand/set_freq

while [ ! -e $devNode ]; do sleep 10; done
chown media.system $devNode
chmod 660 $devNode
