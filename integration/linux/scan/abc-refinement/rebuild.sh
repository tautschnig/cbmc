#!/bin/bash
# rebuild.sh -- reconstruct the CodeQL databases and goto binaries the
# abc-refinement triage pipeline depends on, from the kernel trees.
#
# The pipeline's analysis artefacts live in /tmp (ephemeral).  This script
# rebuilds them deterministically so pipeline_eval.py / triage_loop.py /
# auto_real_harness.py can run from scratch.
#
#   ./rebuild.sh synth     # synthetic fixture DBs only (fast, no kernel)
#   ./rebuild.sh gb        # goto binaries from the kernel trees (minutes)
#   ./rebuild.sh subsys    # subsystem CodeQL DBs (heavy; needs kernel build)
#   ./rebuild.sh all       # everything
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
SCAN="$(cd "$HERE/.." && pwd)"
ROOT="$(cd "$SCAN/../../.." && pwd)"
GOTOCC="$ROOT/build/bin/goto-cc"
export PATH=/home/ubuntu/codeql:$PATH
export CODEQL_ALLOW_INSTALLATION_ANYWHERE=true
PACKS=/home/ubuntu/codeql/qlpacks
LINUX_NEXT=/home/ubuntu/linux_next
LINUX_MAINLINE=/home/ubuntu/linux_mainline

# synthetic fixture -> DB (cheap, deterministic, no kernel needed)
synth_db() {  # <fixture.c> <db-out>
  local src="$HERE/$1" db="$2"
  ( ulimit -v 16000000
    timeout 200 codeql database create "$db" --language=cpp --overwrite \
      --command="gcc -c $src -o /tmp/$(basename "${1%.c}").o" >/dev/null 2>&1 )
  echo "  $2  <- $1  ($([ -d "$db" ] && echo ok || echo FAILED))"
}

# kernel TU -> goto binary (capture `make V=1`, swap gcc->goto-cc, -o, strip -Werror)
build_gb() {  # <tree> <obj> <gb-out>
  local tree="$1" obj="$2" gb="$3" base
  base="$(basename "${obj%.o}")"
  ( cd "$tree" || return 1
    ulimit -v 16000000
    rm -f "$obj"
    timeout 500 make V=1 "$obj" 2>&1 | grep -E "gcc .*-c .*$base\.c" | head -1 > "/tmp/${base}_cc.txt" )
  [ -s "/tmp/${base}_cc.txt" ] || { echo "  $gb  FAILED (no compile cmd)"; return 1; }
  local cmd
  cmd="$(sed 's#^  gcc #'"$GOTOCC"' #; s#-o '"$obj"'#-o '"$gb"'#; s/-Werror[=a-z-]*//g' "/tmp/${base}_cc.txt")"
  ( cd "$tree"; ulimit -v 32000000; timeout 600 bash -c "$cmd" >/dev/null 2>&1 )
  echo "  $gb  <- $tree/$obj  ($([ -f "$gb" ] && echo "$(stat -c%s "$gb") bytes" || echo FAILED))"
}

do_synth() {
  echo "== synthetic fixture DBs =="
  synth_db count_index_test.c   /tmp/count_db
  synth_db decoded_len_test.c   /tmp/declen_db
  synth_db bounded_cursor_test.c /tmp/bc_db
  synth_db skb_lencheck_test.c  /tmp/skb_db
  synth_db copyfail_test.c      /tmp/cf_db
  echo "  (cover_bisect_test.c, real_*_cover.c, real_*.c run directly under cbmc -- no DB)"
}

do_gb() {
  echo "== kernel goto binaries (auto_real_harness GB_REGISTRY) =="
  build_gb "$LINUX_NEXT"     net/can/gw.o        /tmp/gw.gb
  build_gb "$LINUX_NEXT"     net/rxrpc/rxkad.o   /tmp/rxkad.gb
  build_gb "$LINUX_NEXT"     net/ceph/osdmap.o   /tmp/osdmap.gb
  build_gb "$LINUX_MAINLINE" net/mac80211/mlme.o /tmp/mlme.gb
}

do_subsys() {
  echo "== subsystem CodeQL DBs (heavy) =="
  echo "  build-codeql-db.sh <tree> <db> <make-target>; e.g.:"
  echo "    $SCAN/build-codeql-db.sh $LINUX_NEXT     /tmp/broad-next-db 'net/rxrpc/ net/ceph/ net/can/ net/netfilter/ net/bridge/ net/mac80211/'"
  echo "    $SCAN/build-codeql-db.sh $LINUX_MAINLINE /tmp/rc7-db        'net/sctp/ net/ipv4/ net/mptcp/ net/6lowpan/ net/wireless/ net/mac80211/'"
  [ "${RUN_SUBSYS:-0}" = "1" ] || { echo "  (set RUN_SUBSYS=1 to actually build these)"; return; }
  "$SCAN/build-codeql-db.sh" "$LINUX_NEXT" /tmp/broad-next-db \
    "net/rxrpc/ net/ceph/ net/can/ net/netfilter/ net/bridge/ net/mac80211/"
  "$SCAN/build-codeql-db.sh" "$LINUX_MAINLINE" /tmp/rc7-db \
    "net/sctp/ net/ipv4/ net/mptcp/ net/6lowpan/ net/wireless/ net/mac80211/"
}

case "${1:-all}" in
  synth)  do_synth ;;
  gb)     do_gb ;;
  subsys) do_subsys ;;
  all)    do_synth; do_gb; do_subsys ;;
  *) echo "usage: $0 {synth|gb|subsys|all}"; exit 2 ;;
esac
