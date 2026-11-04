#!/bin/bash
# The decisive control for dfcc-assigns-performance-2026-11.md: the
# superlinear cost seen in dfcc_assigns_phases.sh is present WITHOUT DFCC, a
# contract, or a loop.  One dereference of a pointer that may designate any of
# K objects is enough.
#
#   A  K-way switch assigning p, no dereference   -> linear in K
#   B  K-way switch assigning p, one dereference  -> ~7-8x per doubling of K
#   C  p loaded from a K-entry pointer table, one dereference -> same as B
#
# B and C agree, so the cost is the dereference of a K-target pointer, not
# how the pointer was produced.  The final SSA is only linear in K (see the
# document), so the time goes into building and simplifying intermediate
# expressions during symex.
#
# --object-bits 12 lifts the default 256-object limit so K=256 is measurable.
#
#   KS="32 64 128 256" ./deref_k_targets.sh
set -eu
BIN=${BIN:-/home/ubuntu/cbmc-github.git/build/bin}
KS=${KS:-"32 64 128 256"}
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
cd "$T"

objs() { # $1 = K
  echo 'struct inner { int buf[4]; };'
  for i in $(seq 0 $(($1 - 1))); do echo "static struct inner ob$i[8];"; done
  echo 'unsigned nd(void){unsigned v; return v;}'
}

gen_switch() { # $1 = K, $2 = deref|none
  {
    objs "$1"
    echo 'static struct inner *p;'
    echo 'int main(void){ unsigned sel = nd();'
    echo "  switch(sel % $1) {"
    for i in $(seq 0 $(($1 - 1))); do echo "  case $i: p = ob$i; break;"; done
    echo '  default: p = ob0; }'
    [ "$2" = deref ] && echo '  p[0].buf[0] = 1;'
    echo '  return 0; }'
  } > a.c
}

gen_table() { # $1 = K
  {
    objs "$1"
    printf 'static struct inner *tbl[%d] = {' "$1"
    for i in $(seq 0 $(($1 - 1))); do printf 'ob%d,' "$i"; done
    echo '};'
    echo "int main(void){ unsigned sel = nd(); struct inner *p = tbl[sel % $1];"
    echo '  p[0].buf[0] = 1; return 0; }'
  } > a.c
}

run() {
  "$BIN/goto-cc" a.c -o a.gb
  ( ulimit -v 32000000
    timeout 600 "$BIN/cbmc" a.gb --no-standard-checks --object-bits 12 \
      --verbosity 9 > o.log 2>&1 ) || true
  printf '%10s %8s  %s\n' \
    "$(grep -oE 'Runtime Symex: [0-9.]+' o.log | grep -oE '[0-9.]+$' | cut -c1-7)" \
    "$(grep -oE 'program expression: [0-9]+' o.log | grep -oE '[0-9]+$')" \
    "$(grep -oE '^VERIFICATION \w+' o.log || echo TIMEOUT)"
}

printf '%-30s %10s %8s  %s\n' case symex steps result
for k in $KS; do printf 'K=%-4s A switch, no deref      ' "$k"; gen_switch "$k" none; run; done
for k in $KS; do printf 'K=%-4s B switch + deref        ' "$k"; gen_switch "$k" deref; run; done
for k in $KS; do printf 'K=%-4s C table + deref         ' "$k"; gen_table "$k"; run; done
