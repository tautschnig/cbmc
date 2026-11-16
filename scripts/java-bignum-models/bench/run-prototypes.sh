#!/bin/bash
# run-prototypes.sh PROTO_DIR... : time each Bench function with each prototype of java.math.BigInteger
M=${MODELS:-$(cd "$(dirname "$0")"/../../.. && pwd)/jbmc/lib/java-models-library/target}
J=${JBMC:-jbmc}
FUNCS="addConst parseConst square succ fact20 fact25 fib100 fmtLen mulNoOverflow bigRoundTrip wideCompare mulOverflowBad succBad fact25Bad"
for p in "$@"; do
  for f in $FUNCS; do
    out=$( (ulimit -v 30000000; /usr/bin/time -f "TIME %e %M" timeout ${TO:-300} $J --function Bench.$f --unwind ${UNWIND:-45} --unwinding-assertions --java-locale en-US --max-nondet-string-length 60 --cp .:$p:$M/core-models.jar:$M/cprover-api.jar Bench 2>&1) )
    v=$(echo "$out" | grep -o "^VERIFICATION [A-Z]*" | head -1); [ -z "$v" ] && v="TIMEOUT/CRASH"
    t=$(echo "$out" | grep "^TIME" | awk '{printf "%.1fs %dMB", $2, $3/1024}')
    unw=$(echo "$out" | grep -c "unwinding assertion.*FAILURE")
    printf "%-4s %-15s %-24s %-16s unwind-fail=%s\n" "$(basename $p)" "$f" "$v" "$t" "$unw"
  done
done
