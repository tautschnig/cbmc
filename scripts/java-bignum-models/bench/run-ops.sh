#!/bin/bash
# run-ops.sh: time each Ops function with the models of core-models.jar
M=${MODELS:-$(cd "$(dirname "$0")"/../../.. && pwd)/jbmc/lib/java-models-library/target}
J=${JBMC:-jbmc}
for f in $(grep -o "public static void [a-zA-Z0-9]*" Ops.java | awk '{print $4}'); do
  out=$( (ulimit -v 30000000; /usr/bin/time -f "TIME %e %M" timeout ${TO:-300} $J --function Ops.$f --unwind ${UNWIND:-40} --unwinding-assertions --java-locale en-US --max-nondet-string-length 20 --cp .:${EXTRA:-.}:$M/core-models.jar:$M/cprover-api.jar Ops 2>&1) )
  v=$(echo "$out" | grep -o "^VERIFICATION [A-Z]*" | head -1); [ -z "$v" ] && v="TIMEOUT/CRASH"
  t=$(echo "$out" | grep "^TIME" | awk '{printf "%.1fs %dMB", $2, $3/1024}')
  unw=$(echo "$out" | grep -c "unwinding assertion.*FAILURE")
  printf "%-12s %-24s %-16s unwind-fail=%s\n" "$f" "$v" "$t" "$unw"
done
