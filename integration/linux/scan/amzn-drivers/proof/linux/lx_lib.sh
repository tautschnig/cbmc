# Shared build steps for the Linux ENA runners.  Source after setting
# HERE, OUT, BIN, LENA, KTREE.
SCAN=$(cd "$HERE/../../.." && pwd)
[ -f "$LENA/gb/ena_ethtool.gb" ] || { echo "run build_linux_ena.sh first"; exit 1; }

# lx_build NAME HARNESS "DRIVER_GBS" "REMOVE_BODIES" DEFINE...
# Compiles HARNESS (with the given #defines) against the kernel tree, links
# it with the driver goto binaries after removing the named function bodies
# from them (so the harness's models take their place).
lx_build() {
  local n=$1 h=$2 gbs=$3 rm=$4 d g f; shift 4
  local w="$LENA/ena/lxh_$n.c"
  { for d in "$@"; do echo "#define $d"; done
    echo "#include \"$HERE/$h\""; } > "$w"
  rm -f "$OUT/$n.o" "$OUT/$n.gb" "$OUT/${n}_drv.gb"
  ( ulimit -v 16000000
    SOURCE_INCLUDE_DIR=$LENA/common/ena_com GOTOCC=$BIN/goto-cc KBUILD_MODNAME_OVERRIDE=ena \
      "$SCAN/compile_file.sh" "$KTREE" "$w" "$OUT/$n.o" > "$OUT/$n.cc.log" 2>&1 )
  [ -f "$OUT/$n.o" ] || { echo "BUILD FAILED ($n), refusing to run stale binaries"; grep -m3 -i error "$OUT/$n.cc.log"; exit 1; }
  g=""; for f in $gbs; do g="$g $LENA/gb/$f.gb"; done
  ( ulimit -v 16000000; "$BIN/goto-cc" $g -o "$OUT/${n}_drv.gb" ) > "$OUT/$n.ld.log" 2>&1
  if [ -n "$rm" ]; then
    local r=""; for f in $rm; do r="$r --remove-function-body $f"; done
    ( ulimit -v 16000000; "$BIN/goto-instrument" $r "$OUT/${n}_drv.gb" "$OUT/${n}_drv.gb" ) >> "$OUT/$n.ld.log" 2>&1
  fi
  ( ulimit -v 16000000; "$BIN/goto-cc" "$OUT/$n.o" "$OUT/${n}_drv.gb" -o "$OUT/$n.gb" ) >> "$OUT/$n.ld.log" 2>&1
  [ -f "$OUT/$n.gb" ] || { echo "LINK FAILED ($n)"; tail -3 "$OUT/$n.ld.log"; exit 1; }
}

# lx_report LOG OBLIGATION_RE NOBODY_OK_RE
lx_report() {
  local log=$1 ob=$2 ok=${3:-^$}
  grep -qE '^VERIFICATION' "$log" || { echo "   NO VERDICT"; tail -3 "$log" | sed 's/^/     /'; }
  sed -E 's/\] line [0-9]+ /] /; s/^/   /' "$log" | grep -E "$ob|^   VERIFICATION" | sort -u
  printf '   other FAILURE: %s   no-body reached: %s   UNKNOWN: %s   unwind FAIL: %s\n' \
    "$(grep -E ': FAILURE$' "$log" | grep -v 'no body for callee' | grep -vcE "$ob|witness")" \
    "$(grep -E 'no body for callee.*FAILURE' "$log" | grep -vcE "$ok")" \
    "$(grep -cE ': UNKNOWN$' "$log")" \
    "$(grep -cE 'unwind.*: FAILURE' "$log")"
}
