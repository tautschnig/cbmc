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

if [[ $# -lt 3 || $# -gt 4 ]]; then
  echo "usage: $0 <kernel-tree> <source.c> <output.gb> [extra-define]" >&2
  exit 2
fi

KTREE=$1
SOURCE=$2
OUT=$3
EXTRA_DEFINE=${4:-}

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

# Derive a reasonable basename for KBUILD_* defines.  An optional
# environment override KBUILD_MODNAME_OVERRIDE allows the caller to
# pin the module name to that of a different translation unit.  This
# matters when several .c files are linked together and macros such
# as NL_SET_ERR_MSG_MOD bake KBUILD_MODNAME into static-const-char
# arrays inside inline header functions: without a matching MODNAME
# those arrays end up with mismatched lengths across TUs and the
# linker reports them as conflicting variables.
base=$(basename -- "$SOURCE" .c)
modname=${KBUILD_MODNAME_OVERRIDE:-$base}

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

EXTRA_ARGS=()
if [[ -n $EXTRA_DEFINE ]]; then
  EXTRA_ARGS+=("-D$EXTRA_DEFINE")
fi

# Automatically add the source file's directory as an include
# path so #include "foo.h" forms (relative-to-source-file)
# resolve correctly.  This is the kernel build's default
# behaviour.  For instrumented copies at absolute paths,
# SOURCE_INCLUDE_DIR overrides this with the original
# source's directory.
#
# Many subsystem headers are organised so a file at
# drivers/<sub>/<dir1>/<dir2>/foo.c includes a header in
# drivers/<sub>/<sibling>/.  Add ancestor directories up
# to two levels above the source file's directory so the
# #include "header.h" form resolves more often without
# kernel-build-system involvement.
auto_inc=""
if [[ $SOURCE = /* ]]; then
  auto_inc=$(dirname -- "$SOURCE")
else
  auto_inc=$(dirname -- "$KTREE/$SOURCE")
fi
if [[ -n "$auto_inc" && -d "$auto_inc" ]]; then
  EXTRA_ARGS+=("-I" "$auto_inc")
  parent=$(dirname -- "$auto_inc")
  if [[ -n "$parent" && -d "$parent" && "$parent" != "$auto_inc" ]]; then
    EXTRA_ARGS+=("-I" "$parent")
    grandparent=$(dirname -- "$parent")
    if [[ -n "$grandparent" && -d "$grandparent" \
          && "$grandparent" != "$parent" ]]; then
      EXTRA_ARGS+=("-I" "$grandparent")
    fi
  fi
fi

# When SOURCE_INCLUDE_DIR is set in the environment, add it
# to the include path.  This is used when the source file is
# an instrumented copy at an absolute path outside the kernel
# tree: #include "foo.h" forms in the kernel TU need to
# resolve against the original source's directory.
if [[ -n "${SOURCE_INCLUDE_DIR:-}" ]]; then
  EXTRA_ARGS+=("-I" "$SOURCE_INCLUDE_DIR")
fi

# Build the goto-cc command as an array so we can re-run it
# below after auto-resolving missing-header errors.
GOTOCC_CMD=(
  "$GOTOCC" --native-compiler gcc
  --export-file-local-symbols
  -Wall
  -nostdinc
  -isystem "$(gcc -print-file-name=include)"
  -I./arch/x86/include -I./arch/x86/include/generated
  -I./include -I./arch/x86/include/uapi -I./arch/x86/include/generated/uapi
  -I./include/uapi -I./include/generated/uapi
  -include ./include/linux/kconfig.h
  -include ./include/linux/compiler_types.h
  -include "${SCAN_COMPAT_H:-$SCRIPT_DIR/fragments/scan-compat.h}"
  -D__KERNEL__ -std=gnu89
  -m64 -mno-sse -mno-mmx -mno-sse2 -mno-3dnow -mno-avx
  -mno-80387 -mno-fp-ret-in-387
  -mtune=generic -mno-red-zone -mcmodel=kernel
  -fno-stack-protector -fomit-frame-pointer
  -fno-strict-aliasing -fno-common -fshort-wchar -fno-PIE
  -O2
  -DKBUILD_MODFILE="\"$modfile\""
  -DKBUILD_BASENAME="\"$base\""
  -DKBUILD_MODNAME="\"$modname\""
  "${EXTRA_ARGS[@]}"
  -c -o "$OUT" "$source_for_gotocc"
)

# Run the goto-cc compile.  If it fails with
# "fatal error: foo.h: No such file or directory", do a
# bounded `find` for foo.h in the kernel tree, add that
# directory to the include path, and retry once.  This
# recovers many drivers/<sub>/<group>/foo.c files that
# include sibling-subdirectory headers
# (e.g. drivers/media/usb/em28xx/em28xx-cards.c needs
# tuner-xc2028.h from drivers/media/tuners/).
COMPILE_LOG=$(mktemp)
trap 'rm -f "$COMPILE_LOG"' EXIT
"${GOTOCC_CMD[@]}" 2>"$COMPILE_LOG"
rc=$?
if [[ $rc -ne 0 ]]; then
  # Look for missing-header errors in the log.
  missing_headers=$(grep -oE 'fatal error: [^:]+: No such file or directory' \
                    "$COMPILE_LOG" \
                    | sed -E 's/^fatal error: //; s/: No such file or directory$//' \
                    | sort -u)
  added_paths=()
  for header in $missing_headers; do
    # Bounded find: limit max-depth to keep the cost
    # bounded on large kernel trees.
    found_path=$(find . -type f -name "$header" \
                 -not -path '*/.git/*' \
                 -not -path '*/Documentation/*' \
                 2>/dev/null | head -1)
    if [[ -n "$found_path" ]]; then
      header_dir=$(dirname -- "$found_path")
      if [[ -n "$header_dir" ]]; then
        added_paths+=("-I" "$header_dir")
      fi
    fi
  done
  if [[ ${#added_paths[@]} -gt 0 ]]; then
    # Rebuild with the added include paths.
    GOTOCC_CMD=(
      "$GOTOCC" --native-compiler gcc
      --export-file-local-symbols
      -Wall
      -nostdinc
      -isystem "$(gcc -print-file-name=include)"
      -I./arch/x86/include -I./arch/x86/include/generated
      -I./include -I./arch/x86/include/uapi -I./arch/x86/include/generated/uapi
      -I./include/uapi -I./include/generated/uapi
      -include ./include/linux/kconfig.h
      -include ./include/linux/compiler_types.h
      -include "${SCAN_COMPAT_H:-$SCRIPT_DIR/fragments/scan-compat.h}"
      -D__KERNEL__ -std=gnu89
      -m64 -mno-sse -mno-mmx -mno-sse2 -mno-3dnow -mno-avx
      -mno-80387 -mno-fp-ret-in-387
      -mtune=generic -mno-red-zone -mcmodel=kernel
      -fno-stack-protector -fomit-frame-pointer
      -fno-strict-aliasing -fno-common -fshort-wchar -fno-PIE
      -O2
      -DKBUILD_MODFILE="\"$modfile\""
      -DKBUILD_BASENAME="\"$base\""
      -DKBUILD_MODNAME="\"$modname\""
      "${EXTRA_ARGS[@]}"
      "${added_paths[@]}"
      -c -o "$OUT" "$source_for_gotocc"
    )
    "${GOTOCC_CMD[@]}" 2>"$COMPILE_LOG"
    rc=$?
  fi
fi

# Replay the final compile log to stderr so callers see
# warnings/errors as before.
cat "$COMPILE_LOG" >&2
exit $rc
