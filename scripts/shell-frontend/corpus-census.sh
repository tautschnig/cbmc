#!/bin/bash
# Census of the shell constructs that block conversion, on the sh and bash
# scripts installed on this machine (see section 7.4 of
# doc/architectural/shell-frontend-plan.md).
#
# Usage: corpus-census.sh <cbmc> <work-dir> [cbmc options...]
#
# Collects the scripts whose first line is a sh or bash interpreter line,
# removes duplicates by content, converts each one (4 GB, 60 s), and
# prints how many convert and the first unsupported construct of the
# others. Needs CBMC_SHFMT or shfmt on the PATH.

set -euo pipefail

if [ $# -lt 2 ]; then
  echo "usage: $0 <cbmc> <work-dir> [cbmc options...]" >&2
  exit 1
fi
cbmc=$(realpath "$1")
work=$2
shift 2
mkdir -p "$work/files"

for dir in /var/lib/dpkg/info /usr/bin /usr/sbin /usr/lib /usr/libexec \
  /usr/share /etc; do
  find "$dir" -xdev -type f -size -200k 2>/dev/null || true
done | while read -r file; do
  line=$(head -c 64 "$file" 2>/dev/null | head -n 1 | tr -d '\0') || true
  case "$line" in
    '#!/bin/sh'* | '#! /bin/sh'* | '#!/usr/bin/env sh'*) ext=sh ;;
    '#!/bin/bash'* | '#! /bin/bash'* | '#!/usr/bin/env bash'*) ext=bash ;;
    *) continue ;;
  esac
  hash=$(sha1sum "$file" | cut -c1-12)
  [ -e "$work/files/$hash.$ext" ] || cp "$file" "$work/files/$hash.$ext"
done

convert_one() {
  local file=$1 cbmc=$2
  shift 2
  local output status
  output=$(cd "$(dirname "$file")" &&
    (ulimit -v 4000000; timeout 60 "$cbmc" "$@" --show-goto-functions \
      "$(basename "$file")" 2>&1 >/dev/null)) && status=0 || status=$?
  local reason
  reason=$(printf '%s\n' "$output" |
    grep -m1 -E 'unsupported shell construct|exceeds|rror' |
    grep -v 'CONVERSION ERROR' |
    sed -E 's/^file [^ ]+ line [0-9]+ column [0-9]+: //;
            s/^unsupported shell construct: //') || true
  printf '%s\t%s\t%s\n' "$(basename "$file")" "$status" "$reason"
}
export -f convert_one

find "$work/files" -type f | sort |
  xargs -P "$(nproc)" -I{} bash -c 'convert_one "$@"' _ {} "$cbmc" "$@" \
    > "$work/results.tsv"

total=$(wc -l < "$work/results.tsv")
converted=$(awk -F'\t' '$2 == 0' "$work/results.tsv" | wc -l)
echo "scripts: $total, converted: $converted"
echo "first unsupported construct of the others:"
awk -F'\t' '$2 != 0 { print $3 }' "$work/results.tsv" |
  sed -E 's/string literal of [0-9]+ bytes/string literal of N bytes/' |
  sort | uniq -c | sort -rn
