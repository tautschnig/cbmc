# Common setup for every run.sh under integration/linux/.
# Source this at the top of each script:
#
#   source "$(dirname "${BASH_SOURCE[0]}")/../../scan/_lib.sh"
#
# Provides:
#   - ulimit -v based on $SCAN_MEMORY_LIMIT_KB (default 4 GiB).
#   - Timeout budgets in $GOTOCC_TIMEOUT / $GI_TIMEOUT / $CBMC_TIMEOUT,
#     overridable via env.
#   - Helpers run_gotocc / run_gi / run_cbmc that wrap each tool in
#     `timeout`, capturing stdout+stderr and exit code.  All three can
#     hang on complex inputs (goto-instrument with --generate-function-body
#     being the classic case), so wrapping is not optional.
#   - Path resolution: SCRIPT_DIR and REPO_ROOT are set.

set -u

: "${SCAN_MEMORY_LIMIT_KB:=$((4 * 1024 * 1024))}"  # 4 GiB
: "${GOTOCC_TIMEOUT:=120}"
: "${GI_TIMEOUT:=120}"
: "${CBMC_TIMEOUT:=60}"

# Apply the memory cap to this shell (inherited by every subprocess).
ulimit -v "$SCAN_MEMORY_LIMIT_KB"

# The caller determines SCRIPT_DIR; REPO_ROOT is derived from it.
if [[ -z ${SCRIPT_DIR:-} ]]; then
  echo "_lib.sh: caller must set SCRIPT_DIR before sourcing" >&2
  exit 2
fi
REPO_ROOT=$(cd -- "$SCRIPT_DIR" && while [[ ! -d .git && $PWD != / ]]; do cd ..; done; pwd)

: "${CBMC:=$REPO_ROOT/build/bin/cbmc}"
: "${GOTOCC:=$REPO_ROOT/build/bin/goto-cc}"
: "${GI:=$REPO_ROOT/build/bin/goto-instrument}"

for tool in "$CBMC" "$GOTOCC" "$GI"; do
  if [[ ! -x $tool ]]; then
    echo "required tool not found: $tool" >&2
    echo "set CBMC / GOTOCC / GI to override, or build the tree first." >&2
    exit 2
  fi
done
if ! command -v timeout &>/dev/null; then
  echo "'timeout' not found in PATH" >&2
  exit 2
fi

# run_gotocc <args...>
# run_gi     <args...>
# run_cbmc   <args...>
#
# Each runs the tool under a wall-clock timeout, with the memory cap
# already inherited from this shell.  stdout+stderr go to the caller's
# fds; exit code is the tool's (or 124 on timeout).
run_gotocc() { timeout "$GOTOCC_TIMEOUT" "$GOTOCC" "$@"; }
run_gi()     { timeout "$GI_TIMEOUT"     "$GI"     "$@"; }
run_cbmc()   { timeout "$CBMC_TIMEOUT"   "$CBMC"   "$@"; }
