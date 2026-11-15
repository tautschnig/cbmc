#!/bin/bash
# Symex cost of the DPDK x86 spinlock asm under the asm-semantics build.
# Expected if the lock word were simplified: no rte spin-loop unwinding, cost
# independent of --unwind.  Observed: cost grows with --unwind once N >= 2.
set -u
B=${BIN:-/home/ubuntu/cbmc-github.git/build-asm/bin}
D=$(cd "$(dirname "$0")" && pwd)
ulimit -v 16000000
for W in "" -DWIDE; do for N in 1 4; do for U in 5 10 20 40; do
  L=$(mktemp)
  /usr/bin/time -o "$L.t" -f "%e s, %M KB" timeout 600 \
    "$B/cbmc" "$D/asm_spinlock_unwinds.c" -DKNOWN $W -DN=$N --unwind $U \
    --unwindset main.0:$((N + 1)) --verbosity 9 > "$L" 2>&1
  echo "${W:-32-bit} N=$N unwind=$U: $(grep -cE 'Unwinding loop spin_lock' "$L") spin-loop" \
    "unwindings, $(cat "$L.t")"
  rm -f "$L" "$L.t"
done; done; done
