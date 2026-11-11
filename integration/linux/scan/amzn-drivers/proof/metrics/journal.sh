#!/bin/bash
# Log one process event.  Durations are derived from consecutive events, so
# the rule is: log an event whenever the activity changes.
#
#   journal.sh READ      ena_rss.c          "reading the reta paths"
#   journal.sh INVARIANT INV_CLEAN#3        "added the no-wrap bound"
#   journal.sh TRIAGE    cleanup_step       "part 3 failed on INV_STARTED"
#   journal.sh HARNESS   harness_rss_reta   "first version"
#   journal.sh DOC       F19                "write-up"
#
# Categories (keep to these so the summary stays meaningful):
#   READ       reading driver or library source to find out what it does
#   HARNESS    writing or editing a harness / runner
#   INVARIANT  devising or strengthening an invariant, pre- or post-condition
#   TRIAGE     diagnosing why a run failed or was vacuous
#   TOOL       running something other than cbmc (CodeQL, builds, greps)
#   DOC        writing up a finding or a method note
#   WAIT       waiting on a long background verification
set -u
CSV=${METRICS_ACTIVITY:-$(cd "$(dirname "$0")" && pwd)/activity.csv}
CAT=${1:?category}; SUBJ=${2:?subject}; shift 2 || true
NOTE=${*:-}
[ -f "$CSV" ] || echo "ts,category,subject,note" > "$CSV"
printf '%s,%s,%s,"%s"\n' "$(date -Is)" "$CAT" "$SUBJ" "$NOTE" >> "$CSV"
