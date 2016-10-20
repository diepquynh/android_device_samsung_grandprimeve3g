#!/system/bin/sh

for i in 0 1 2 3; do
echo 257M > /sys/block/zram${i}/disksize;
mkswap /dev/block/zram${i};
swapon -p 05 /dev/block/zram${i};
done

echo 100 > /proc/sys/vm/swappiness
