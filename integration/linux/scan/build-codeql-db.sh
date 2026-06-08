#!/bin/bash
# build-codeql-db.sh — standard CodeQL DB build recipe for the Linux kernel.
# Applies the percpu.h __seg_gs workaround, cleans target objects, creates the
# DB, measures body coverage, and reverts the percpu.h change.
#
# Usage: ./build-codeql-db.sh <kernel-tree> <db-output-path> <make-target>
# Example: ./build-codeql-db.sh /home/ubuntu/linux_6_12 /tmp/bt-db net/bluetooth/
set -euo pipefail

TREE="${1:?Usage: $0 <kernel-tree> <db-output-path> <make-target>}"
DB="${2:?Usage: $0 <kernel-tree> <db-output-path> <make-target>}"
TARGET="${3:?Usage: $0 <kernel-tree> <db-output-path> <make-target>}"
JOBS="${4:-$(nproc)}"
PERCPU="$TREE/arch/x86/include/asm/percpu.h"

export PATH=/home/ubuntu/codeql:$PATH
export CODEQL_ALLOW_INSTALLATION_ANYWHERE=true
export CCACHE_DISABLE=1

echo "=== [1/5] Apply __seg_gs (named-address-space) workaround ==="
# The gate location moved across kernel versions; patch whichever form
# is present.  Goal: force __percpu_seg_override empty so current.h does
# not use the __seg_gs qualifier the EDG extractor frontend can't parse.
PERCPU_TYPES="$TREE/arch/x86/include/asm/percpu_types.h"
PATCHED=0
# (a) <= 6.x form: percpu.h gate `#ifdef CONFIG_CC_HAS_NAMED_AS`
if grep -q '^#ifdef CONFIG_CC_HAS_NAMED_AS' "$PERCPU"; then
  sed -i 's/^#ifdef CONFIG_CC_HAS_NAMED_AS/#if 0 \/* codeql: disable __seg_gs *\//' "$PERCPU"
  echo "  patched percpu.h gate"; PATCHED=1
fi
# (b) 7.x form: percpu_types.h gate `#if defined(CONFIG_SMP) && defined(CONFIG_CC_HAS_NAMED_AS)`
if [ -f "$PERCPU_TYPES" ] && \
   grep -q '^#if defined(CONFIG_SMP) && defined(CONFIG_CC_HAS_NAMED_AS)' "$PERCPU_TYPES"; then
  sed -i 's/^#if defined(CONFIG_SMP) && defined(CONFIG_CC_HAS_NAMED_AS)$/#if 0 \/* codeql: disable __seg_gs *\//' "$PERCPU_TYPES"
  echo "  patched percpu_types.h gate"; PATCHED=1
fi
[ "$PATCHED" = 0 ] && echo "  no known gate found (already patched or new layout?)"

echo "=== [2/5] Clean target objects ==="
# $TARGET may list MULTIPLE space-separated make targets; clean each one
# individually (a single `find "$TREE/$TARGET"` would treat the whole
# space-joined string as one nonexistent path and silently clean nothing,
# leaving stale .o files that block recompilation -> empty extraction).
for _t in $TARGET; do
  find "$TREE/$_t" -name '*.o' -delete 2>/dev/null || true
done
echo "  cleaned $TARGET"

echo "=== [3/5] Create CodeQL database ==="
cd "$TREE"
codeql database create "$DB" --language=cpp --overwrite \
  --command="make -j$JOBS $TARGET" 2>&1 | tail -5

echo "=== [4/5] Coverage self-check (function bodies per file) ==="
# Run from the abc-refinement qlpack directory so 'import cpp' resolves.
SCRIPTDIR="$(cd "$(dirname "$0")" && pwd)"
QLDIR="$SCRIPTDIR/abc-refinement"
cat > "$QLDIR/_cov_check.ql" <<'QLEOF'
/** @kind problem @id abc/cov-check */
import cpp
from File f
where f.getExtension() = "c"
  and exists(Function fn | fn.getFile() = f and fn.hasDefinition()
             and fn.getBlock().getNumStmt() > 0)
select f, f.getAbsolutePath()
QLEOF
codeql query run --database="$DB" --additional-packs=/home/ubuntu/codeql/qlpacks \
  --output=/tmp/_cov.bqrs "$QLDIR/_cov_check.ql" 2>&1 | tail -1
TOTAL_C=$(for _t in $TARGET; do find "$TREE/$_t" -name '*.c'; done | wc -l)
BODIES=0
for _t in $TARGET; do
  BODIES=$((BODIES + $(codeql bqrs decode --format=csv /tmp/_cov.bqrs 2>/dev/null | grep -c "$_t")))
done
echo "  $TARGET: $BODIES / $TOTAL_C .c files have extracted bodies"
if [ "$BODIES" -lt "$((TOTAL_C * 7 / 10))" ]; then
  echo "  WARNING: <70% coverage — check extractor logs for parse errors"
fi
rm -f "$QLDIR/_cov_check.ql" /tmp/_cov.bqrs

echo "=== [5/5] Revert percpu.h ==="
sed -i 's|^#if 0 /\* codeql: disable __seg_gs \*/|#ifdef CONFIG_CC_HAS_NAMED_AS|' "$PERCPU"
if [ -f "$PERCPU_TYPES" ]; then
  sed -i 's|^#if 0 /\* codeql: disable __seg_gs \*/|#if defined(CONFIG_SMP) \&\& defined(CONFIG_CC_HAS_NAMED_AS)|' "$PERCPU_TYPES"
fi
echo "  reverted"
echo "=== DONE: $DB ==="
