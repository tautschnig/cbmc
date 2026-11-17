#!/bin/bash
# partest.sh DIR N CMD LOG: runs regression/test.pl on the test directories
# of DIR in N parallel groups, and concatenates their logs to LOG (each
# group's log is LOG.<group>). Bound the memory in the caller (ulimit -v).
DIR=$1; N=$2; CMD=$3; LOG=$4
cd "$DIR"
TESTPL=$(cd "$(git rev-parse --show-toplevel)/regression" && pwd)/test.pl
GRP=$(mktemp -d)
dirs=$(find . -mindepth 2 -maxdepth 2 -name "*.desc" | cut -d/ -f2 | sort -u)
i=0; for d in $dirs; do echo "$d" >> "$GRP/$((i % N))"; i=$((i+1)); done
for g in $(seq 0 $((N-1))); do
  ( $TESTPL -C -p -c "$CMD" $(cat "$GRP/$g") > "$LOG.$g" 2>&1 ) &
done
wait
rm -rf "$GRP"
cat "$LOG".* > "$LOG"
