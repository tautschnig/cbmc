#!/bin/bash
# run-all.sh API_FILE CASES OUT_DIR [CLASS_PREFIX]: runs Probe on each JDK of
# JDKS (directories, "VERSION=JAVA_HOME" separated by spaces), 4 at a time.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
API=$(realpath "$1"); CASES=$2; OUT=$(realpath -m "$3"); PREFIX=${4:-}
: "${JDKS:?set JDKS to VERSION=JAVA_HOME pairs}"
mkdir -p "$OUT/cls"
JAVAC=$(echo $JDKS | tr ' ' '\n' | grep '^17=' | cut -d= -f2)/bin/javac
"$JAVAC" --release 8 -nowarn -d "$OUT/cls" "$HERE/Probe.java" 2>&1 | grep -v '^Note' || true
for pair in $JDKS; do
  v=${pair%%=*}; jh=${pair#*=}
  echo "$v $jh"
done | xargs -P 4 -L 1 bash -c '(ulimit -v 8000000; timeout 3600 "$1/bin/java" $([ "$0" = 10 ] && echo -Xint) -Xmx256m -cp "'"$OUT"'/cls" Probe "'"$API"'" '"$CASES"' "'"$OUT"'/$0.txt" '"$PREFIX"' > "'"$OUT"'/$0.log" 2>&1) || echo "JDK $0 failed"'
