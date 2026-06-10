#!/bin/bash
# run_threat_survey.sh -- reproducible multi-subsystem threat-model survey.
#
# Builds the CodeQL DB manifest below (whole-class builds cover many leaf
# subsystems at once), TRACKING build timeouts, then runs the coverage
# census.  Re-runnable: existing DBs are skipped.  This is the automation
# behind the >50% subsystem coverage and the timeout accounting.
#
#   ./run_threat_survey.sh build     # build all DBs in the manifest
#   ./run_threat_survey.sh census    # coverage census over /tmp/*-db
#   ./run_threat_survey.sh all       # build + census (default)
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
SCAN="$(cd "$HERE/.." && pwd)"
NEXT=/home/ubuntu/linux_next
M612=/home/ubuntu/linux_6_12
# per-DB wall-clock cap (s).  Whole-class builds are ~6-10 min EXCEPT the
# 6.12 whole-drivers build (broad distro config, ~78 min, 36 GB) -- hence a
# generous default so it completes rather than tripping BUILD-TIMEOUT.
BUILD_TIMEOUT="${BUILD_TIMEOUT:-6000}"
LOG=/tmp/threat_survey.log

# manifest: tree|make-target|dbname.  Whole-class builds (net/ fs/ drivers/
# sound/) cover the configured subset of each class; two trees are unioned
# (linux-next + 6.12) because each enables a different subset.
MANIFEST=(
  "$NEXT|net/|net-all-db"          "$M612|net/|612-net-db"
  "$NEXT|fs/|fs-all-db"            "$M612|fs/|612-fs-db"
  "$NEXT|drivers/|drivers-all-db"  "$M612|drivers/|612-drivers-db"
  "$NEXT|sound/|sound-all-db"      "$M612|sound/|612-sound-db"
  "$NEXT|block/|block-db"          "$NEXT|io_uring/|iouring-db"
  "$NEXT|ipc/|ipc-db"              "$NEXT|security/|security-db"
  "$M612|crypto/|cryp-db"
)

do_build() {
  : > "$LOG"
  local ok=0 skip=0 to=0 fail=0
  for entry in "${MANIFEST[@]}"; do
    IFS='|' read -r tree tgt name <<< "$entry"
    local db="/tmp/$name"
    if [ -d "$db" ]; then echo "skip  $name"; skip=$((skip+1)); continue; fi
    echo "build $name ($tgt) ..."
    ( ulimit -v 96000000
      timeout "$BUILD_TIMEOUT" "$SCAN/build-codeql-db.sh" "$tree" "$db" "$tgt" "$(nproc)" \
        >/dev/null 2>&1 )
    local rc=$?
    if [ "$rc" -eq 124 ]; then echo "  BUILD-TIMEOUT $name" | tee -a "$LOG"; to=$((to+1))
    elif [ -d "$db" ]; then echo "  ok $name"; ok=$((ok+1))
    else echo "  BUILD-FAIL $name" | tee -a "$LOG"; fail=$((fail+1)); fi
  done
  echo "== built=$ok skipped=$skip build-timeout=$to build-fail=$fail =="
}

do_census() {
  python3 "$HERE/subsystem_census.py" --tree "$NEXT" --dbs "/tmp/*-db"
}

case "${1:-all}" in
  build)  do_build ;;
  census) do_census ;;
  all)    do_build; echo; do_census ;;
  *) echo "usage: $0 {build|census|all}"; exit 2 ;;
esac
