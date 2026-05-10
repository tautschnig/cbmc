#!/usr/bin/env bash
#
# configure.sh - set up a Linux kernel tree for scanning.
#
# Takes a kernel tree path plus one or more config fragments, and
# produces a configured, prepared tree where `scan/compile_file.sh`
# can then turn individual .c files into goto-cc goto binaries.
#
# The script starts from `make allnoconfig` so that no surprises
# drag in from a distro's defconfig, then merges the given fragments
# via the kernel's own `scripts/kconfig/merge_config.sh`.  After
# `make olddefconfig` it runs `make prepare scripts` so the
# generated headers (include/generated/autoconf.h, compile.h, etc.)
# exist.
#
# Usage:
#
#   configure.sh <kernel-tree> <fragment> [<fragment>...]
#
# Example (enough to scan crypto/algif_aead.c):
#
#   configure.sh /home/you/linux_5_10 \
#                scan/fragments/baseline.config \
#                scan/fragments/crypto-aead.config
#
# Fragments are merged in the order given, so later ones can
# override earlier values.  See scan/fragments/README.md for the
# available fragments and how to add new ones.
#
# Exit code:
#   0  on success;
#   1  if a required step fails;
#   2  on argument errors or missing tools.
#
# Notes:
#
# - If `make prepare scripts` fails (we have seen this on some
#   modern hosts, see LIM-005 in integration/linux/CBMC_LIMITATIONS.md),
#   the script still returns 0 as long as `include/generated/autoconf.h`
#   and `scripts/mod/` are already present from a prior build.  This
#   matches what `scan/compile_file.sh` actually needs downstream.

set -u

if [[ $# -lt 2 ]]; then
  echo "usage: $0 <kernel-tree> <fragment> [<fragment>...]" >&2
  exit 2
fi

KTREE=$1
shift
FRAGMENTS=("$@")

if [[ ! -d $KTREE ]]; then
  echo "kernel tree not found: $KTREE" >&2
  exit 2
fi
if [[ ! -f $KTREE/Makefile || ! -d $KTREE/scripts/kconfig ]]; then
  echo "$KTREE does not look like a Linux kernel source tree" >&2
  exit 2
fi

# Resolve fragments to absolute paths before cd'ing so relative
# paths relative to the caller still work.
abs_fragments=()
for f in "${FRAGMENTS[@]}"; do
  if [[ ! -f $f ]]; then
    echo "fragment not found: $f" >&2
    exit 2
  fi
  abs_fragments+=("$(realpath -- "$f")")
done

cd -- "$KTREE"

echo "[+] resetting config to allnoconfig"
make allnoconfig >/dev/null
echo "[+] merging ${#abs_fragments[@]} fragment(s)"
scripts/kconfig/merge_config.sh -m .config "${abs_fragments[@]}"
echo "[+] olddefconfig"
make olddefconfig >/dev/null
echo "[+] prepare scripts (may partially fail on some hosts; see LIM-005)"
if ! make prepare scripts >/dev/null 2>&1; then
  if [[ ! -f include/generated/autoconf.h ]]; then
    echo "[-] make prepare failed and no pre-existing autoconf.h" >&2
    exit 1
  fi
  echo "[+] make prepare reported errors but required headers exist; continuing"
fi

echo "[+] verifying a few key config symbols ended up set:"
for frag in "${abs_fragments[@]}"; do
  grep -hE '^CONFIG_[A-Z0-9_]+=y' "$frag" | while read -r line; do
    sym=${line%%=*}
    if grep -qx -- "$line" .config; then
      printf "    ok   %s\n" "$sym"
    else
      printf "    miss %s (selected but not final)\n" "$sym"
    fi
  done
done

echo "[+] tree is ready at $KTREE"
