#!/bin/bash
# scope_eval.sh -- per-subsystem (per-leaf) scoping harness.
#
# Query-level scoping (Scope.qll / ABC_SCOPE_PREFIX) prunes the per-candidate
# join and helps moderate shared DBs, but it does NOT prune the global helper
# relations (taint, SSA, dominance, forall-over-calls), so the heavy finders
# and caller_precondition still blow up on the GIANT whole-class DBs
# (612-net, 612-drivers).  The dependable tractability mechanism is therefore
# a small PER-LEAF DB: when the DB *is* the leaf, every base relation is cheap
# and all 8 finders + caller_precondition run in minutes -- which is what
# unblocks the drivers verdicts that whole-drivers queries could never
# produce.
#
#   ./scope_eval.sh <tree> <leaf> [leaf ...]
# e.g.
#   ./scope_eval.sh /home/ubuntu/linux_6_12 drivers/nfc/ drivers/hid/
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
SCAN="$(cd "$HERE/.." && pwd)"
TREE="${1:?usage: scope_eval.sh <tree> <leaf> [leaf ...]}"; shift
BUILD_TIMEOUT="${BUILD_TIMEOUT:-1200}"
Q_TIMEOUT="${Q_TIMEOUT:-300}"

dbs=()
for leaf in "$@"; do
  leaf="${leaf%/}/"                       # normalise trailing slash
  san="$(echo "$leaf" | tr '/' '-' | sed 's/-$//')"
  db="/tmp/leaf-${san}-db"
  if [ ! -d "$db" ]; then
    echo "build $leaf -> $db ..."
    ( ulimit -v 96000000
      timeout "$BUILD_TIMEOUT" "$SCAN/build-codeql-db.sh" "$TREE" "$db" "$leaf" \
        >/dev/null 2>&1 )
    if [ "$?" -eq 124 ]; then echo "  BUILD-TIMEOUT $leaf"; continue; fi
    [ -d "$db" ] || { echo "  BUILD-FAIL $leaf"; continue; }
  else
    echo "skip $leaf (cached $db)"
  fi
  dbs+=("$db")
done

[ "${#dbs[@]}" -gt 0 ] || { echo "no leaf DBs built"; exit 1; }

echo
echo "== per-leaf outcome distribution (${#dbs[@]} leaf DBs) =="
( ulimit -v 96000000
  python3 "$HERE/outcome_summary.py" --dbs "${dbs[*]}" --timeout "$Q_TIMEOUT" )
