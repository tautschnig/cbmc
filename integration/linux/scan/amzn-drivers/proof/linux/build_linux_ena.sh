#!/bin/bash
# goto-cc build of the out-of-tree Linux ENA driver (amzn-drivers
# kernel/linux/ena) against a configured kernel tree.
#
# 1. A normal gcc kbuild in a scratch copy, which runs the driver's own
#    configure step and so writes config.h for this kernel (kcompat.h keys
#    every compatibility branch off it).  Only modpost fails, because the
#    tree's Module.symvers is not installed; every object compiles.
# 2. Each translation unit recompiled with goto-cc via scan/compile_file.sh.
#
# Output: $LENA/gb/<file>.gb and <file>.log.
set -u
SRC=${SRC:-/home/ubuntu/amzn-drivers/kernel/linux}
KTREE=${KTREE:-/home/ubuntu/linux_6_12}
LENA=${LENA:-/tmp/lena-oot}
GOTOCC=${GOTOCC:-/home/ubuntu/cbmc-github.git/build-asm/bin/goto-cc}
HERE=$(cd "$(dirname "$0")" && pwd)
SCAN=$(cd "$HERE/../../.." && pwd)
[ -x "$SCAN/compile_file.sh" ] || { echo "no compile_file.sh under $SCAN"; exit 1; }

rm -rf "$LENA"; mkdir -p "$LENA/gb"
cp -r "$SRC/ena" "$SRC/common" "$LENA/"
( cd "$LENA/ena" && ulimit -v 16000000 &&
  timeout 1200 make -j16 KERNEL_BUILD_DIR="$KTREE" \
    BUILD_KERNEL="$(make -s -C "$KTREE" kernelversion)" V=1 > build.log 2>&1 )
[ -f "$LENA/ena/config.h" ] || { echo "configure did not write config.h"; exit 1; }
grep -E '^ *gcc -Wp,-MMD.* -c -o ' "$LENA/ena/build.log" |
  sed 's/^ *//; s/ *; *\.\/tools\/objtool.*$//' > "$LENA/ena/cmds.txt"
n=$(wc -l < "$LENA/ena/cmds.txt")
[ "$n" -gt 0 ] || { echo "no compile commands in build.log"; exit 1; }

for src in $(awk '{print $NF}' "$LENA/ena/cmds.txt"); do
  src=$(realpath "$src"); b=$(basename "$src" .c)
  # ena_com/ needs the ena/ directory for kcompat.h; ena/ needs ena_com/.
  case "$src" in
    */ena_com/*) inc=$LENA/ena;;
    *) inc=$LENA/common/ena_com;;
  esac
  ( ulimit -v 16000000
    SOURCE_INCLUDE_DIR=$inc GOTOCC=$GOTOCC KBUILD_MODNAME_OVERRIDE=ena \
      timeout 900 "$SCAN/compile_file.sh" "$KTREE" "$src" "$LENA/gb/$b.gb" \
      > "$LENA/gb/$b.log" 2>&1
    echo "$b rc=$?" ) &
done
wait
ls "$LENA"/gb/*.gb | wc -l | sed "s/^/goto binaries: /;s/$/ of $n/"
