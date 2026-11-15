#!/bin/bash
# Launch, inspect and stop long background runs by recorded process group,
# never by matching command lines.
#
#   bg.sh start NAME CMD...   run CMD in its own session; log to
#                             $RUNS/NAME.log, record the session in NAME.pgid
#   bg.sh status [NAME]       show whether each recorded run is alive
#   bg.sh stop NAME           SIGTERM the whole session, then SIGKILL after 10 s
#
# Why this exists: `pkill -f PATTERN` and `pgrep -f PATTERN | xargs kill`
# match the shell running the command too, because the pattern is on that
# shell's own command line.  That killed the controlling shell twice in one
# session, and once left orphaned cbmc processes of a "stopped" sweep running
# for minutes afterwards.  Recording the process group at launch removes the
# pattern match entirely: stop acts on exactly the processes start created,
# including every cbmc child, and on nothing else.
#
# CMD runs under `setsid`, and stop acts on the SESSION, not the process
# group.  The first version of this script used the process group and its own
# test showed why that is not enough: GNU `timeout` calls setpgid(0,0) to put
# itself in a group of its own, so `kill -- -PGID` reached the shell but not
# `timeout` or the process under it -- and every runner wraps cbmc in
# `timeout`.  Those escaped as orphans, which is exactly the failure this
# script exists to prevent.  timeout does not create a new session, so
# `pkill -s SID` reaches all of them.  -s matches a session id, not a command
# line, so it cannot match the controlling shell, which is in another session.
set -u
RUNS=${RUNS:-/tmp/ena-proof/runs}
mkdir -p "$RUNS"

alive() { pgrep -s "$1" > /dev/null 2>&1; }

case "${1:-}" in
  start)
    name=${2:?name}; shift 2
    [ $# -gt 0 ] || { echo "bg.sh start: no command" >&2; exit 2; }
    if [ -f "$RUNS/$name.pgid" ] && alive "$(cat "$RUNS/$name.pgid")"; then
      echo "bg.sh: $name is already running (pgid $(cat "$RUNS/$name.pgid"))" >&2
      exit 1
    fi
    setsid "$@" > "$RUNS/$name.log" 2>&1 < /dev/null &
    pid=$!
    echo "$pid" > "$RUNS/$name.pgid"
    date -Is > "$RUNS/$name.started"
    echo "$name started, pgid $pid, log $RUNS/$name.log"
    ;;
  status)
    for f in "$RUNS"/${2:-*}.pgid; do
      [ -f "$f" ] || continue
      n=$(basename "$f" .pgid); g=$(cat "$f")
      if alive "$g"; then
        echo "$n  running  pgid $g  since $(cat "$RUNS/$n.started" 2>/dev/null)"
      else
        echo "$n  finished pgid $g"
      fi
    done
    ;;
  stop)
    name=${2:?name}
    [ -f "$RUNS/$name.pgid" ] || { echo "bg.sh: no record of $name" >&2; exit 1; }
    g=$(cat "$RUNS/$name.pgid")
    if ! alive "$g"; then echo "$name already finished"; exit 0; fi
    pkill -TERM -s "$g" 2>/dev/null
    for _ in 1 2 3 4 5 6 7 8 9 10; do alive "$g" || break; sleep 1; done
    alive "$g" && pkill -KILL -s "$g" 2>/dev/null
    sleep 1
    alive "$g" && { echo "bg.sh: $name (pgid $g) survived SIGKILL" >&2; exit 1; }
    echo "$name stopped (pgid $g)"
    ;;
  *)
    echo "usage: bg.sh start NAME CMD... | status [NAME] | stop NAME" >&2
    exit 2
    ;;
esac
