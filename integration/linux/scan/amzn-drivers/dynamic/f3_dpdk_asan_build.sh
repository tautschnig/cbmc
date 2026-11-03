#!/bin/bash
# Confirm F3 (ena_com_get_hash_key trusts the device-written key_parts) under
# AddressSanitizer, against the REAL amzn-drivers DPDK ena_com.c.
#
#   git clone --depth 1 -b v24.11 https://github.com/DPDK/dpdk /tmp/dpdk
#   cp -a amzn-drivers/userspace/dpdk/ena/. /tmp/dpdk/drivers/net/ena/
#   python3 -m venv /tmp/dpdk-venv && /tmp/dpdk-venv/bin/pip install pyelftools
#   cd /tmp/dpdk && PATH=/tmp/dpdk-venv/bin:$PATH meson setup build-asan \
#       -Db_sanitize=address -Ddefault_library=static \
#       -Denable_drivers=net/ena -Dtests=false -Dexamples='' -Dmax_numa_nodes=1
#   ninja -C build-asan
#
# Then run this script.  ASAN needs a large virtual reservation, so bound
# memory with a cgroup (systemd-run), NOT with ulimit -v.
set -eu
DPDK=${DPDK:-/tmp/dpdk}
B=$DPDK/build-asan
OBJ=$B/drivers/libtmp_rte_net_ena.a.p/net_ena_base_ena_com.c.o
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-/tmp/ena-asan}
mkdir -p "$OUT"

# reuse the exact flags meson used for ena_com.c
python3 - "$B/compile_commands.json" > "$OUT/flags.txt" <<'PY'
import json, shlex, sys
for e in json.load(open(sys.argv[1])):
    if 'ena_com.c' in e['file']:
        t = shlex.split(e['command']); out = []; i = 0
        while i < len(t):
            x = t[i]
            if x == '-include':
                out += [x, t[i + 1]]; i += 2; continue
            if x.startswith(('-I', '-D', '-fsanitize', '-fno-omit', '-std',
                             '-march', '-mrtm', '-fPIC')):
                out.append(x)
            i += 1
        print(' '.join(out)); break
PY

cd "$B"   # the -I paths in flags.txt are relative to the build dir
gcc $(cat "$OUT/flags.txt") -g -O1 -c "$HERE/f3_dpdk_asan_harness.c" -o "$OUT/harness.o"
gcc -fsanitize=address -g -O1 -c "$HERE/f3_dpdk_asan_stubs.c" -o "$OUT/stubs.o"
gcc -fsanitize=address -g "$OUT/harness.o" "$OBJ" "$OUT/stubs.o" -lpthread -o "$OUT/harness"

for mode in heap stack; do
  echo "===== $mode destination ====="
  systemd-run --user --scope -p MemoryMax=4G --quiet timeout 60 "$OUT/harness" \
    $([ $mode = stack ] && echo stack) 2>&1 | head -12 || true
done
