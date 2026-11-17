#!/bin/bash
# cmpenv.sh VAR JBMC DIR:NAME...: times the JBMC regression tests DIR/NAME.desc
# (of jbmc-strings, else of jbmc) without and with the environment variable
# VAR=1, one after the other (8 GB, 600 s each). JBMC must be an absolute
# path.
VAR=$1; J=$2; shift 2
TOP=$(cd "$(dirname "$0")/../.." && pwd)
R=$TOP/jbmc/regression
M=$TOP/jbmc/lib/java-models-library/target
TMP=$(mktemp -d)
for t in "$@"; do
  d=${t%:*}; n=${t#*:}
  base=$R/jbmc-strings; [ -d "$base/$d" ] || base=$R/jbmc
  args=$(sed -n 3p "$base/$d/$n.desc" | sed 's/--cp `.*`//; s/-cp `.*`//')
  main=$(sed -n 2p "$base/$d/$n.desc" | sed 's/\.class$//')
  for e in 0 1; do
    (
      cd "$base/$d"
      if [ $e = 1 ]; then export "$VAR"=1; else unset "$VAR"; fi
      ulimit -v 8000000
      /usr/bin/time -f "%e" -o "$TMP/time" timeout 600 "$J" $main $args \
        -cp .:$M/core-models.jar:$M/cprover-api.jar > "$TMP/out" 2>&1
    )
    echo "$n $VAR=$e: $(grep -oE 'VERIFICATION [A-Z]+' "$TMP/out" | tail -1)" \
      "$(tail -1 "$TMP/time") s"
  done
done
rm -rf "$TMP"
