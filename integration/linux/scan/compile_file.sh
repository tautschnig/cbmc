#!/usr/bin/env bash
#
# compile_file.sh - build a single Linux kernel source file to a
# goto-cc goto binary without driving the kernel's make system.
#
# Takes an already-configured kernel tree (must have
# include/generated/autoconf.h, i.e. 'make olddefconfig && make
# prepare scripts' has succeeded at least once) and a path to a .c
# source file relative to the tree root.
#
# Writes the goto binary to the second argument.
#
# Usage:
#   compile_file.sh <kernel-tree> <source.c> <output.gb>
#
# Example:
#   compile_file.sh /home/ubuntu/linux_5_10 crypto/algif_aead.c /tmp/algif.gb
#
# The compile flags mirror the ones the kernel's own V=1 build logs
# for x86_64 allnoconfig + KVM + crypto user-API.  They have been
# trimmed to drop gcc-specific flags that goto-cc does not parse
# cleanly, and extended with -DKBUILD_* defaults.  If your target
# file has architecture-specific flag requirements, check the
# corresponding .o.cmd file in the tree and extend this script.

set -u

if [[ $# -ne 3 ]]; then
  echo "usage: $0 <kernel-tree> <source.c> <output.gb>" >&2
  exit 2
fi

KTREE=$1
SOURCE=$2
OUT=$3

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)
REPO_ROOT=$(cd -- "$SCRIPT_DIR/../../.." &>/dev/null && pwd)
GOTOCC=${GOTOCC:-"$REPO_ROOT/build/bin/goto-cc"}

if [[ ! -x $GOTOCC ]]; then
  echo "goto-cc not found at $GOTOCC" >&2
  exit 2
fi
if [[ ! -f $KTREE/include/generated/autoconf.h ]]; then
  echo "$KTREE looks unconfigured (no include/generated/autoconf.h)" >&2
  echo "run 'make olddefconfig && make prepare scripts' there first." >&2
  exit 2
fi
if [[ ! -f $KTREE/$SOURCE && ! -f $SOURCE ]]; then
  echo "source file not found: $KTREE/$SOURCE (and not at $SOURCE either)" >&2
  exit 2
fi

# Derive a reasonable basename for KBUILD_* defines.
base=$(basename -- "$SOURCE" .c)

# Accept either a kernel-tree-relative path or an absolute path
# outside the kernel tree.  External files still compile against the
# kernel's includes because we cd into the kernel tree below.
if [[ $SOURCE = /* ]]; then
  # absolute path; goto-cc will find it directly
  source_for_gotocc=$SOURCE
  modfile=$base
else
  source_for_gotocc=$SOURCE
  modfile=${SOURCE%.c}
fi

cd -- "$KTREE"
"$GOTOCC" --native-compiler gcc \
  -nostdinc \
  -isystem "$(gcc -print-file-name=include)" \
  -I./arch/x86/include -I./arch/x86/include/generated \
  -I./include -I./arch/x86/include/uapi -I./arch/x86/include/generated/uapi \
  -I./include/uapi -I./include/generated/uapi \
  -include ./include/linux/kconfig.h \
  -include ./include/linux/compiler_types.h \
  -D__KERNEL__ -std=gnu89 \
  -m64 -mno-sse -mno-mmx -mno-sse2 -mno-3dnow -mno-avx \
  -mno-80387 -mno-fp-ret-in-387 \
  -mtune=generic -mno-red-zone -mcmodel=kernel \
  -fno-stack-protector -fomit-frame-pointer \
  -fno-strict-aliasing -fno-common -fshort-wchar -fno-PIE \
  -O2 \
  -DKBUILD_MODFILE="\"$modfile\"" \
  -DKBUILD_BASENAME="\"$base\"" \
  -DKBUILD_MODNAME="\"$base\"" \
  -c -o "$OUT" "$source_for_gotocc"
