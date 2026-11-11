#!/bin/bash
# Phase 8, pair 2: the admin submit/complete handshake under interleaving.
set -u
B=${B:-/home/ubuntu/cbmc-github.git/build/bin}
P=$(cd "$(dirname "$0")" && pwd)
W=${W:-/tmp/ena-proof/conc2}; mkdir -p "$W"
for cfg in "any_device:" "well_behaved:-DWELL_BEHAVED_DEVICE"; do
  n=${cfg%%:*}; d=${cfg#*:}
  echo "== $n"
  for V in 0 1; do
    vd=""; [ $V = 1 ] && vd=-DVACUITY_CHECK
    rm -f "$W/$n$V.gb"
    ( ulimit -v 16000000; "$B/goto-cc" $d $vd "$P/harness_conc_admin.c" -o "$W/$n$V.gb" 2>&1 | grep -i error | head )
    [ -f "$W/$n$V.gb" ] || { echo "   BUILD FAILED, refusing to run stale binaries"; exit 1; }
  done
  ( ulimit -v 60000000; timeout 900 "$B/cbmc" "$W/${n}0.gb" --bounds-check \
    --pointer-check --unwind 10 --unwinding-assertions --sat-solver cadical ) > "$W/$n.log" 2>&1
  sed -E 's/\] line [0-9]+ /] /; s/^/   /' "$W/$n.log" | grep -E 'CC2_|^   VERIFICATION'
  ( ulimit -v 60000000; timeout 900 "$B/cbmc" "$W/${n}1.gb" --unwind 10 --sat-solver cadical ) > "$W/${n}_v.log" 2>&1
  sed -E 's/\] line [0-9]+ /] /; s/^/   /' "$W/${n}_v.log" | grep 'vacuity witness'
done
