#!/system/bin/sh
a=`getprop zram.disksize 600`
b=`getprop sys.vm.swappiness 100`
for i in `ls /sys/block/ | grep zram`; do
echo $(($a*1024*1024)) > /sys/block/${i}/disksize;
mkswap /dev/block/${i};
swapon -p 05 /dev/block/${i};
done
echo $(($b)) > /proc/sys/vm/swappiness



