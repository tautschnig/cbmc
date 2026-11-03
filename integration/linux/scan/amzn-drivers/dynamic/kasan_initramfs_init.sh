#!/bin/sh
export PATH=/bin:/sbin
mount -t proc none /proc
mount -t sysfs none /sys
echo "=== guest up: $(uname -r) ==="
T=$(cat /proc/cmdline | sed -n 's/.*enatest=\([0-9]*\).*/\1/p')
[ -z "$T" ] && T=3
echo "=== insmod enatest.ko test=$T ==="
insmod /mod/enatest.ko test=$T
echo "=== dmesg (KASAN + enatest) ==="
dmesg | grep -A 30 -E "enatest: running" | head -70
echo "=== GUEST-DONE ==="
poweroff -f
