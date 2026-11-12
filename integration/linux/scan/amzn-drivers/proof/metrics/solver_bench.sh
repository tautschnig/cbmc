#!/bin/bash
# Compare solver backends on identical goto-binaries: outcome, CPU, memory.
#
# Solving is most of CBMC's time on these artefacts, so the backend is the
# obvious lever.  Each backend runs on the SAME .gb with the SAME flags.
#
# Outcomes are classified rather than lumped together, because "it did not
# produce a verdict" has several very different causes:
#   SUCCESSFUL/FAILED  a real verdict (these must AGREE across backends)
#   CRASH              CBMC threw (e.g. the SMT2 backend's map::at)
#   UNSUPPORTED        the backend is not compiled in, or rejects the option
#   TIMEOUT            hit the time budget
#   OOM                hit the address-space cap
# A faster backend that DISAGREES is a bug to investigate, not a win.
#
# CPU and peak RSS come from /usr/bin/time, so memory is comparable across
# backends -- SMT2 backends in particular are run as child processes and their
# cost does not show up in CBMC's own reporting.
set -u
B=${B:-/home/ubuntu/cbmc-github.git/build/bin}
CSV=${CSV:-$(cd "$(dirname "$0")" && pwd)/solver-bench.csv}
TO=${TO:-1800}
MEM=${MEM:-150000000}
MODEL=${1:?model.gb}; LABEL=${2:?label}; shift 2
FLAGS=${*:---bounds-check --unwinding-assertions --unwind 12 --slice-formula}
# Space inside an option is written as ~ because the list is word-split.
BACKENDS=${BACKENDS:-"minisat2:--sat-solver~minisat2 cadical:--sat-solver~cadical glucose:--sat-solver~glucose z3:--z3 cvc5:--cvc5 bitwuzla:--bitwuzla cprover-smt2:--cprover-smt2 smt2-default:--smt2"}
[ -f "$CSV" ] || echo "label,backend,kind,wall_s,cpu_s,peak_rss_kb,outcome,failures,props,detail" > "$CSV"
for cfg in $BACKENDS; do
  name=${cfg%%:*}; opt=$(echo "${cfg#*:}" | tr '~' ' ')
  case "$name" in minisat2|cadical|glucose) kind=SAT;; *) kind=SMT2;; esac
  log=$(mktemp); tf=$(mktemp)
  /usr/bin/time -f 'TF %e %U %S %M' -o "$tf" \
    bash -c "ulimit -v $MEM; timeout $TO '$B/cbmc' '$MODEL' $opt $FLAGS" > "$log" 2>&1
  rc=$?
  read -r _t wall _u _s rss <<< "$(grep '^TF' "$tf")"
  cpu=$(echo "${_u:-0} + ${_s:-0}" | bc)
  v=$(grep -oE '^VERIFICATION [A-Z]+' "$log" | head -1 | sed 's/VERIFICATION //')
  detail=""
  if [ -z "$v" ]; then
    if grep -qiE "unknown solver|unknown option|not supported" "$log"; then v=UNSUPPORTED
      detail=$(grep -oiE "unknown solver '[^']*'|unknown option[^\"]*" "$log" | head -1)
    elif grep -qiE "out of memory|std::bad_alloc" "$log"; then v=OOM
    elif [ "$rc" = 124 ]; then v=TIMEOUT
    elif grep -qE "^VERIFICATION ERROR|: ERROR$" "$log"; then v=ERROR
      detail=$(grep -m1 -oE "[A-Za-z ]*not (yet )?(supported|implemented)[^\"]*" "$log" | head -1)
    else v=CRASH
      detail=$(tail -1 "$log" | cut -c1-60)
    fi
  fi
  f=$(grep -cE ': FAILURE$' "$log"); p=$(grep -cE ': (SUCCESS|FAILURE)$' "$log")
  printf '%-22s %-13s %-5s %7.1fs cpu=%7.1fs rss=%6.0fMB  %-11s %s\n' \
    "$LABEL" "$name" "$kind" "${wall:-0}" "$cpu" "$(echo "${rss:-0}/1024" | bc)" "$v" "$detail"
  printf '%s,%s,%s,%.1f,%.1f,%s,%s,%s,%s,"%s"\n' \
    "$LABEL" "$name" "$kind" "${wall:-0}" "$cpu" "${rss:-}" "$v" "$f" "$p" "$detail" >> "$CSV"
  rm -f "$log" "$tf"
done
