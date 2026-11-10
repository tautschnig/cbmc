#!/bin/bash
# Phase 8, pair 1: the reset-request handshake under interleaving.
set -u
B=${B:-/home/ubuntu/cbmc-github.git/build/bin}
P=$(cd "$(dirname "$0")" && pwd)
W=${W:-/tmp/ena-proof/conc}; mkdir -p "$W"
for SO in 0 1; do
  echo "== store order $SO ($([ $SO = 0 ] && echo 'reason then flag, as written' || echo 'flag then reason, the permitted reordering'))"
  for V in 0 1; do
    D=""; [ $V = 1 ] && D=-DVACUITY_CHECK
    rm -f "$W/c$SO$V.gb"
    ( ulimit -v 16000000; "$B/goto-cc" -DSTORE_ORDER=$SO $D "$P/harness_conc_reset.c" -o "$W/c$SO$V.gb" 2>&1 | grep -i error | head )
    [ -f "$W/c$SO$V.gb" ] || { echo "BUILD FAILED, refusing to run stale binaries"; exit 1; }
  done
  ( ulimit -v 60000000; timeout 900 "$B/cbmc" "$W/c${SO}0.gb" --bounds-check \
    --pointer-check --unwind 8 --unwinding-assertions --sat-solver cadical ) > "$W/x$SO.log" 2>&1
  sed -E 's/\] line [0-9]+ /] /; s/^/   /' "$W/x$SO.log" | grep -E 'CC_|^   VERIFICATION'
  ( ulimit -v 60000000; timeout 900 "$B/cbmc" "$W/c${SO}1.gb" --unwind 8 --sat-solver cadical ) > "$W/v$SO.log" 2>&1
  sed -E 's/\] line [0-9]+ /] /; s/^/   /' "$W/v$SO.log" | grep 'vacuity witness'
done
