#!/bin/bash
# F14: is ena_com_is_doorbell_needed a sound predictor of the LLQ lines
# ena_com_prepare_tx writes?  One CBMC run per configuration.
set -u
B=${B:-/home/ubuntu/cbmc-github.git/build/bin}
P=$(cd "$(dirname "$0")" && pwd)
W=${W:-/tmp/ena-proof/doorbell}
mkdir -p "$W"
FLAGS="--bounds-check --pointer-check --unwind 20 --unwindset memcmp.0:9 \
  --unwinding-assertions --sat-solver cadical \
  --max-field-sensitivity-array-size 256 --slice-formula"
run() {
  local name=$1; shift
  ( cd /tmp/dpdk/build-ql && ulimit -v 16000000 &&
    "$B/goto-cc" $(cat /tmp/ena-proof/flags.txt) -DPEDANTIC "$@" \
      "$P/harness_doorbell_budget.c" -o "$W/$name.gb" ) || return 1
  ( ulimit -v 60000000; timeout 1800 "$B/cbmc" "$W/$name.gb" $FLAGS --trace ) \
    > "$W/$name.log" 2>&1
  printf '%-34s %s   other failures: %s\n' "$name" \
    "$(grep -E '^VERIFICATION' "$W/$name.log")" \
    "$(grep -E ': FAILURE$' "$W/$name.log" | grep -vc 'F14\|vacuity')"
  grep -E 'vacuity witness' "$W/$name.log" | sed -E 's/^.*\] line [0-9]+ /    /'
}
run original                  -DBUDGET_FITS_PACKET
run original_offloads         -DBUDGET_FITS_PACKET -DWITH_OFFLOADS
run no_b1                     -DBUDGET_FITS_PACKET -DWITH_OFFLOADS -DONLY_B_GE_2
run no_meta_disabled_no_offl  -DBUDGET_FITS_PACKET -DONLY_META_CACHING
run no_meta_disabled_offl     -DBUDGET_FITS_PACKET -DONLY_META_CACHING -DWITH_OFFLOADS
run no_pushed_only            -DBUDGET_FITS_PACKET -DWITH_OFFLOADS -DONLY_NUM_BUFS_POSITIVE
run fixed                     -DBUDGET_FITS_PACKET -DWITH_OFFLOADS -DFIXED_ESTIMATE
run fixed_vacuity             -DBUDGET_FITS_PACKET -DWITH_OFFLOADS -DFIXED_ESTIMATE -DVACUITY_CHECK
