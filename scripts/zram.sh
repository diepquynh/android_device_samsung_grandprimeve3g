#!/system/bin/sh

num_cores=`grep processor /proc/cpuinfo | wc -l`
totalmem=`free | grep -e "^Mem:" | sed -e 's/^Mem: *//' -e 's/  *.*//'`
mem=$(((totalmem / ${num_cores}) * 1024))

for i in $(seq ${num_cores}); do
DEVNUMBER=$((i - 1))
echo ${mem} > /sys/block/${DEVNUMBER}/disksize;
mkswap /dev/block/${DEVNUMBER};
swapon /dev/block/${DEVNUMBER};
done

echo 100 > /proc/sys/vm/swappiness
