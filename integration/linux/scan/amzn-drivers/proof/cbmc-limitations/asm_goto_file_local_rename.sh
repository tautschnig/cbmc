#!/bin/bash
# Expected: VERIFICATION SUCCESSFUL in all four rows.
# Observed on the asm-semantics build: the --export-file-local-symbols row
# aborts with "level0: failed to find
# arch_static_branch::$tmp::tmp_asm_goto_target".
set -u
D=$(cd "$(dirname "$0")" && pwd)
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
for b in build build-asm; do
  B=/home/ubuntu/cbmc-github.git/$b/bin
  for E in "" --export-file-local-symbols; do
    "$B/goto-cc" $E -c "$D/asm_goto_file_local_rename.c" -o "$T/a.o"
    "$B/goto-cc" "$T/a.o" -o "$T/a.gb"
    r=$( ( ulimit -v 8000000; timeout 60 "$B/cbmc" "$T/a.gb" 2>&1 ) |
         grep -E 'Reason|^VERIFICATION' | head -1)
    echo "$b ${E:-(no flag)}: ${r:-no output}"
  done
done
echo "names in the goto binary built with the flag:"
B=/home/ubuntu/cbmc-github.git/build-asm/bin
"$B/goto-cc" --export-file-local-symbols -c "$D/asm_goto_file_local_rename.c" -o "$T/a.o"
strings "$T/a.o" | grep 'tmp_asm_goto_target' | sed 's/^\s*/  /'
