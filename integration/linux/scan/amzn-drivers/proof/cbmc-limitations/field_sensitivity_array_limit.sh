#!/bin/bash
# The points-to set of a pointer read from a struct that sits inside an array
# of more than 64 elements (CBMC's default field-sensitivity array limit)
# contains `unknown` and `integer_address`, so a write through it is treated
# as a write to anything.  At 64 elements, or with the limit raised, the set
# is the one object actually stored.  Anonymous unions are not involved: the
# no-union variant behaves the same way.
set -u
BIN=${BIN:-/home/ubuntu/cbmc-github.git/build/bin}
HERE=$(cd "$(dirname "$0")" && pwd)
cd "$(mktemp -d)"
widest() {
  python3 -c "
import json,sys
d=json.load(open('$1'))
p=[e for e in d if isinstance(e,dict) and 'Pointer' in e and ('empty_tx_reqs' in e['Pointer'] or 'anon0' in e['Pointer'])]
print(max((len(e['PointsToSet']) for e in p), default=0))"
}
for src in field_sensitivity_array_limit.c field_sensitivity_array_limit_nounion.c; do
  for N in 64 65 128; do
    "$BIN/goto-cc" -DN_RINGS=$N "$HERE/$src" -o m.gb 2>/dev/null
    for extra in "" "--max-field-sensitivity-array-size 256"; do
      ( ulimit -v 4000000; timeout 120 "$BIN/cbmc" m.gb --no-standard-checks --unwind 5 \
          --show-points-to-sets --json-ui $extra > p.json 2>/dev/null )
      printf '%-42s N=%-4s %-42s widest points-to set: %s\n' "$src" "$N" "${extra:-(default limit 64)}" "$(widest p.json)"
    done
  done
done
