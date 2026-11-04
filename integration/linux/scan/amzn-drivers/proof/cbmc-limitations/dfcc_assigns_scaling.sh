#!/bin/bash
# Isolate what makes a DFCC assigns clause expensive.  Each block varies ONE
# dimension; the last one is the dominant term.
#
#   ./dfcc_assigns_scaling.sh            # all blocks
set -eu
BIN=${BIN:-/home/ubuntu/cbmc-github.git/build/bin}
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
cd "$T"

time_one() { # $1 = source file
  "$BIN/goto-cc" "$1" -o b.gb 2>/dev/null
  "$BIN/goto-instrument" --no-malloc-may-fail --dfcc main --apply-loop-contracts \
      b.gb b_lc.gb >/dev/null 2>&1
  local t0 t1
  t0=$(date +%s.%N)
  ( ulimit -v 32000000; timeout 900 "$BIN/cbmc" b_lc.gb --no-standard-checks > o.log 2>&1 ) || true
  t1=$(date +%s.%N)
  printf "%7.1fs  %s\n" "$(echo "$t1-$t0" | bc)" \
         "$(grep -oE '^VERIFICATION \w+' o.log || echo TIMEOUT)"
}

# (1) unrelated objects, CONCRETE write pointer
gen1() { { for i in $(seq 0 $(($1 - 1))); do echo "int o$i[4];"; done
  echo 'unsigned nd(void){unsigned v; return v;}'
  echo 'int main(void){ unsigned n = nd();'
  echo '  for(unsigned i = 0; i < n; i++)'
  echo '    __CPROVER_assigns(i, __CPROVER_object_whole(o0))'
  echo '    __CPROVER_loop_invariant(i <= n) __CPROVER_decreases(n - i)'
  echo '  { o0[0] = (int)i; } return 0; }'; } > g.c; }
echo "== (1) unrelated objects, concrete write pointer =="
for n in 4 16 64 256; do gen1 $n; printf "objects=%-5s " $n; time_one g.c; done

# (2) number of assigns targets, CONCRETE write pointer
gen2() { { for i in $(seq 0 $(($1 - 1))); do echo "int o$i[4];"; done
  echo 'unsigned nd(void){unsigned v; return v;}'
  echo 'int main(void){ unsigned n = nd();'
  echo '  for(unsigned i = 0; i < n; i++)'
  printf '    __CPROVER_assigns(i'; for i in $(seq 0 $(($1 - 1))); do printf ', __CPROVER_object_whole(o%d)' $i; done; echo ')'
  echo '    __CPROVER_loop_invariant(i <= n) __CPROVER_decreases(n - i)'
  echo '  { o0[0] = (int)i; } return 0; }'; } > g.c; }
echo "== (2) assigns targets, concrete write pointer =="
for t in 1 4 16 32; do gen2 $t; printf "targets=%-5s " $t; time_one g.c; done

# (3) size of a single object_whole target
gen3() { { echo "struct inner { int buf[4]; };"
  echo "static struct inner pool[$1]; static struct inner *p;"
  echo 'unsigned nd(void){unsigned v; return v;}'
  echo 'int main(void){ p = pool; unsigned n = nd();'
  echo '  for(unsigned i = 0; i < n; i++)'
  echo '    __CPROVER_assigns(i, __CPROVER_object_whole(p))'
  echo '    __CPROVER_loop_invariant(i <= n) __CPROVER_decreases(n - i)'
  echo '  { p[0].buf[0] = (int)i; } return 0; }'; } > g.c; }
echo "== (3) object_whole target size =="
for n in 8 512 2048 8192; do gen3 $n; printf "bytes=%-7s " $((n * 16)); time_one g.c; done

# (4) THE DOMINANT TERM: how many objects the write pointer may designate
gen4() { { echo "struct inner { int buf[4]; };"
  for i in $(seq 0 $(($1 - 1))); do echo "static struct inner ob$i[8];"; done
  echo 'static struct inner *p; unsigned nd(void){unsigned v; return v;}'
  echo 'int main(void){ unsigned n = nd(); unsigned sel = nd();'
  echo "  switch(sel % $1) {"
  for i in $(seq 0 $(($1 - 1))); do echo "  case $i: p = ob$i; break;"; done
  echo '  default: p = ob0; }'
  echo '  for(unsigned i = 0; i < n; i++)'
  echo '    __CPROVER_assigns(i, __CPROVER_object_whole(p))'
  echo '    __CPROVER_loop_invariant(i <= n) __CPROVER_decreases(n - i)'
  echo '  { p[0].buf[0] = (int)i; } return 0; }'; } > g.c; }
echo "== (4) objects a SYMBOLIC write pointer may designate =="
for k in 2 8 32 128; do gen4 $k; printf "K=%-9s " $k; time_one g.c; done
