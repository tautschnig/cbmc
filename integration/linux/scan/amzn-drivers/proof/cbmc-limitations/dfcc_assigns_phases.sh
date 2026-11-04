#!/bin/bash
# Per-phase breakdown for the dominant term of dfcc_assigns_scaling.sh:
# family (4), a symbolic write pointer that may designate any of K objects,
# with an __CPROVER_object_whole(p) assigns target.
#
# For each K it reports CBMC's own phase timings (symex, SSA conversion,
# solver) and formula size (symex steps, SAT variables, clauses), so the cost
# can be attributed to a phase and its growth exponent estimated.
#
# Variants (VARIANT=...):
#   whole    __CPROVER_assigns(i, __CPROVER_object_whole(p))   (default)
#   precise  __CPROVER_assigns(i, p[0].buf[0])
#   nodfcc   same program, no contract, --unwind 2 (no write-set machinery)
#
#   KS="2 8 32 64 128 256" VARIANT=whole ./dfcc_assigns_phases.sh
set -eu
BIN=${BIN:-/home/ubuntu/cbmc-github.git/build/bin}
KS=${KS:-"2 4 8 16 32 64 128 256"}
VARIANT=${VARIANT:-whole}
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
cd "$T"

gen() { # $1 = K
  local k=$1 tgt
  case "$VARIANT" in
    whole|nodfcc) tgt='__CPROVER_object_whole(p)' ;;
    precise) tgt='p[0].buf[0]' ;;
    *) echo "unknown VARIANT=$VARIANT" >&2; exit 1 ;;
  esac
  {
    echo 'struct inner { int buf[4]; };'
    for i in $(seq 0 $((k - 1))); do echo "static struct inner ob$i[8];"; done
    echo 'static struct inner *p; unsigned nd(void){unsigned v; return v;}'
    echo 'int main(void){ unsigned n = nd(); unsigned sel = nd();'
    echo "  switch(sel % $k) {"
    for i in $(seq 0 $((k - 1))); do echo "  case $i: p = ob$i; break;"; done
    echo '  default: p = ob0; }'
    echo '  for(unsigned i = 0; i < n; i++)'
    if [ "$VARIANT" != nodfcc ]; then
      echo "    __CPROVER_assigns(i, $tgt)"
      echo '    __CPROVER_loop_invariant(i <= n) __CPROVER_decreases(n - i)'
    fi
    echo '  { p[0].buf[0] = (int)i; } return 0; }'
  } > g.c
}

field() { grep -oE "$1" o.log | head -1 | grep -oE '[0-9.]+' | head -1; }

printf '%-6s %8s %8s %8s %8s %10s %10s %11s  %s\n' \
  K total symex convert solver steps vars clauses result
for k in $KS; do
  gen "$k"
  "$BIN/goto-cc" g.c -o b.gb
  if [ "$VARIANT" = nodfcc ]; then
    cp b.gb b_lc.gb; extra="--unwind 2"
  else
    "$BIN/goto-instrument" --no-malloc-may-fail --dfcc main \
      --apply-loop-contracts b.gb b_lc.gb >/dev/null 2>&1
    extra=""
  fi
  t0=$(date +%s.%N)
  ( ulimit -v 32000000
    timeout 900 "$BIN/cbmc" b_lc.gb --no-standard-checks $extra \
      --verbosity 9 > o.log 2>&1 ) || true
  t1=$(date +%s.%N)
  printf '%-6s %8.1f %8s %8s %8s %10s %10s %11s  %s\n' "$k" \
    "$(echo "$t1-$t0" | bc)" \
    "$(field 'Runtime Symex: [0-9.]+')" \
    "$(field 'Runtime Convert SSA: [0-9.]+')" \
    "$(field 'Runtime Solver: [0-9.]+')" \
    "$(field 'program expression: [0-9]+')" \
    "$(grep -oE '^[0-9]+ variables' o.log | grep -oE '[0-9]+' | head -1)" \
    "$(grep -oE '[0-9]+ clauses' o.log | grep -oE '[0-9]+' | head -1)" \
    "$(grep -oE '^VERIFICATION \w+' o.log || echo TIMEOUT)"
done
