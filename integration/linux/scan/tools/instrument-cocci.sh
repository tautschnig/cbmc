#!/usr/bin/env bash
# instrument-cocci.sh — Coccinelle-based per-file
# instrumentation for the synthetic-checkpoint property
# modules.
#
# Replaces the regex-based instrument.py with proper
# AST-aware structural matching.  Coccinelle handles
# compound LHS, variable scope, and statement-context
# correctly.
#
# Usage:
#   instrument-cocci.sh <input.c> <output.c> [--shapes SHAPES]
#
# SHAPES is comma-separated; default applies all available.

set -u

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)
COCCI_DIR="$SCRIPT_DIR/instrument-cocci"

ALL_SHAPES=(
  null_after_alloc
  resource_leak_on_error_path
  use_after_free_generic
  integer_overflow_in_alloc_size
  copy_from_user_size_check
)

# Header that must be prepended to instrumented sources so
# they compile standalone with the property modules' contracts.
HEADER='/* AUTO-INSERTED by integration/linux/scan/tools/instrument-cocci.sh */
extern void __assert_safe_to_deref(const void *p);
extern void __assert_no_leak_at_exit(const void *p);
extern void __assert_not_freed(const void *p);
extern void __assert_size_safe(unsigned long n, unsigned long elem_size);
extern void __assert_copy_safe(unsigned long dst_capacity, unsigned long len);
extern void leak_alloc_track(const void *p);
'

if [[ $# -lt 2 ]]; then
  echo "Usage: $0 <input.c> <output.c> [--shapes SHAPES]" >&2
  exit 2
fi

INPUT="$1"; shift
OUTPUT="$1"; shift

SHAPES=("${ALL_SHAPES[@]}")
while [[ $# -gt 0 ]]; do
  case "$1" in
    --shapes) IFS=, read -ra SHAPES <<< "$2"; shift 2 ;;
    *) echo "unknown arg: $1" >&2; exit 2 ;;
  esac
done

if [[ ! -f $INPUT ]]; then
  echo "input not found: $INPUT" >&2
  exit 2
fi

# Apply each cocci file in sequence.  --in-place modifies
# the file; we make a working copy so the original is
# untouched.
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cp "$INPUT" "$WORK/inst.c"

INSERTED=0
for shape in "${SHAPES[@]}"; do
  cocci_file="$COCCI_DIR/$shape.cocci"
  if [[ ! -f $cocci_file ]]; then
    echo "warning: no cocci file for shape '$shape'" >&2
    continue
  fi
  # spatch may emit warnings on parse failures from kernel headers;
  # those go to stderr.  Capture and discard them.
  if ! timeout 60 spatch --sp-file "$cocci_file" \
      --in-place --very-quiet \
      "$WORK/inst.c" >/dev/null 2>"$WORK/spatch.err"; then
    # spatch failure on this shape — skip and continue.
    continue
  fi
done

# Count insertions for reporting.
INSERTED=$(grep -cE "__assert_(safe_to_deref|no_leak_at_exit|not_freed|size_safe|copy_safe)|leak_alloc_track\(" \
  "$WORK/inst.c" 2>/dev/null || echo 0)

# Prepend header and write output.
{
  printf '%s\n' "$HEADER"
  cat "$WORK/inst.c"
} > "$OUTPUT"

echo "  cocci-instrumented: $INSERTED insertion(s)" >&2
exit 0
