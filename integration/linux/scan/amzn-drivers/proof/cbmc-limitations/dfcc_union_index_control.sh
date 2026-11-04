#!/bin/bash
# Control cited in dfcc-assigns-performance-2026-11.md section 5: a write
# through a pointer held in an anonymous union, and a symbolic INDEX into a
# single known object, do not make a DFCC loop contract measurably more
# expensive.  Four combinations of {union, plain pointer} x {symbolic index,
# constant index}; each should take about 0.1 s.
#
# This is the shape of the ENA TX path (struct ena_ring keeps tx_buffer_info
# in an anonymous union, and the loop writes tx_buffer_info[req_id]), which is
# why K -- the number of objects a write pointer may designate -- stays at 1-2
# there.
set -eu
BIN=${BIN:-/home/ubuntu/cbmc-github.git/build/bin}
N=${N:-8}
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
cd "$T"

gen() { # $1 = union 0/1, $2 = symbolic index 0/1
  cat > v.c <<EOF
#define N $N
struct inner { int buf[4]; };
struct ring {
#if $1
  union { struct inner *a; struct inner *b; };  /* as in struct ena_ring */
#else
  struct inner *a;
#endif
  unsigned size;
};
static struct inner pool[N];
static struct ring r;
unsigned nd(void) { unsigned v; return v; }
int main(void)
{
  r.a = pool; r.size = N;
  unsigned n = nd();
  for(unsigned i = 0; i < n; i++)
    __CPROVER_assigns(i, __CPROVER_object_whole(r.a))
    __CPROVER_loop_invariant(i <= n)
    __CPROVER_decreases(n - i)
  {
#if $2
    unsigned k = nd(); if(k < N) r.a[k].buf[0] = (int)i;
#else
    r.a[0].buf[0] = (int)i;
#endif
  }
  return 0;
}
EOF
}

printf '%-7s %-15s %8s  %s\n' union symbolic_index time result
for u in 0 1; do
  for s in 0 1; do
    gen $u $s
    "$BIN/goto-cc" v.c -o v.gb
    "$BIN/goto-instrument" --no-malloc-may-fail --dfcc main \
      --apply-loop-contracts v.gb v_lc.gb >/dev/null 2>&1
    t0=$(date +%s.%N)
    ( ulimit -v 32000000
      timeout 900 "$BIN/cbmc" v_lc.gb --no-standard-checks > o.log 2>&1 ) || true
    t1=$(date +%s.%N)
    printf '%-7s %-15s %7.1fs  %s\n' $u $s "$(echo "$t1-$t0" | bc)" \
      "$(grep -oE '^VERIFICATION \w+' o.log || echo TIMEOUT)"
  done
done
