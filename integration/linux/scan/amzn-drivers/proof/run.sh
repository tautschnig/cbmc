#!/bin/bash
# All-sequences safety proof for the DPDK ENA TX path (induction step).
#
# Prerequisites:
#   git clone --depth 1 -b v24.11 https://github.com/DPDK/dpdk /tmp/dpdk
#   cp -a amzn-drivers/userspace/dpdk/ena/. /tmp/dpdk/drivers/net/ena/
#   python3 -m venv /tmp/dpdk-venv && /tmp/dpdk-venv/bin/pip install pyelftools
#   cd /tmp/dpdk && PATH=/tmp/dpdk-venv/bin:$PATH meson setup build-ql \
#       -Ddefault_library=static -Denable_drivers=net/ena -Dtests=false \
#       -Dexamples='' -Dmax_numa_nodes=1 && ninja -C build-ql
#
# Needs a CBMC containing diffblue/cbmc#8747 (constructor priority attribute),
# otherwise goto-cc cannot parse DPDK's RTE_PMD_REGISTER_PCI.
set -eu
DPDK=${DPDK:-/tmp/dpdk}
BIN=${BIN:-/home/ubuntu/cbmc-github.git/build/bin}
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-/tmp/ena-proof}
mkdir -p "$OUT"

# the driver's own compile flags, so the code under proof is the shipped code
python3 - "$DPDK/build-ql/compile_commands.json" > "$OUT/flags.txt" <<'PY'
import json, shlex, sys
for e in json.load(open(sys.argv[1])):
    if e['file'].endswith('ena_ethdev.c'):
        t = shlex.split(e['command']); out = []; i = 0
        while i < len(t):
            x = t[i]
            if x == '-include':
                out += [x, t[i + 1]]; i += 2; continue
            if x.startswith(('-I', '-D', '-std')):
                out.append(x)
            i += 1
        print(' '.join(out)); break
PY

cd "$DPDK/build-ql"
# -DPEDANTIC: selects DPDK's non-GCC branch for the log-format static_assert,
# which CBMC cannot constant-fold.  No driver code is modified.
( ulimit -v 32000000
  "$BIN/goto-cc" $(cat "$OUT/flags.txt") -DPEDANTIC \
    "$HERE/harness_tx_allseq.c" -o "$OUT/harness.gb" )

# Abstract the internal callees by their contracts (see section 6.1 of
# doc/cbmc-driver-proof-design-2026-11.md); CBMC reports each as
# "no body for callee", i.e. the assumption is visible in the output.
( ulimit -v 32000000
  "$BIN/goto-instrument" \
    --remove-function-body ena_xmit_mbuf \
    --remove-function-body ena_tx_cleanup \
    --remove-function-body ena_tx_map_mbuf \
    "$OUT/harness.gb" "$OUT/harness_mod.gb" )

( ulimit -v 48000000
  timeout 1800 "$BIN/cbmc" "$OUT/harness_mod.gb" \
    --bounds-check --pointer-check --unwind 12 --unwinding-assertions \
    > "$OUT/proof.log" 2>&1 ) || true

echo "=== memory-safety properties inside eth_ena_xmit_pkts ==="
grep -E "eth_ena_xmit_pkts\.(pointer_dereference|array_bounds)" "$OUT/proof.log" |
  sed -E 's/.*(line [0-9]+).*: (\w+)$/\1 \2/' | sort | uniq -c
echo "=== stated assumptions surfaced by CBMC (unmodelled callees) ==="
grep -oE "no body for callee [a-zA-Z_0-9.$]+" "$OUT/proof.log" | sort -u
