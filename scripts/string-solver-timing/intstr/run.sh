#!/bin/bash
# run.sh JBMC CLASS OUT [FUNCTION...]: compiles the benchmark CLASS.java and
# times its functions (all public static ones if none are given) with JBMC
# (8 GB; TO seconds, default 600), writing "function verdict seconds KB"
# lines to OUT. Set environment variables (e.g. of a spike) in the caller.
J=$1; C=$2; OUT=$3; shift 3
cd "$(dirname "$0")"
TOP=$(cd ../../.. && pwd)
M=$TOP/jbmc/lib/java-models-library/target
TMP=$(mktemp -d)
javac -Xlint:-options --release 8 -cp "$M/cprover-api.jar" -d "$TMP" "$C.java" || exit 1
F=${*:-$(grep -oE 'public static void [A-Za-z0-9_]+\(' "$C.java" |
  awk '{print $4}' | tr -d '(' | grep -v '^main$')}
: > "$OUT"
for f in $F; do
  ( ulimit -v 8000000; cd "$TMP"
    /usr/bin/time -f "%e %M" -o "$TMP/time" timeout "${TO:-600}" "$J" "$C" \
      --function "$C.$f" --java-locale en-US \
      -cp .:$M/core-models.jar:$M/cprover-api.jar > "$TMP/out" 2>&1 )
  v=$(grep -oE 'VERIFICATION [A-Z]+' "$TMP/out" | tail -1)
  echo "$f ${v:-TIMEOUT} $(tail -1 "$TMP/time")" >> "$OUT"
done
rm -rf "$TMP"
