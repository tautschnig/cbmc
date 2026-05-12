#!/usr/bin/env bash
# Smoke-check all three in-tree validated LTS kernels.
# Each LTS path is checked only if it exists (don't hard-fail
# CI if a tree was never fetched).
set -u
SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)
ok=0; total=0
for v in 5_10 6_1 6_6 6_12; do
  tree=/home/ubuntu/linux_$v
  [[ ! -d "$tree" ]] && continue
  total=$((total + 1))
  echo "=============== Linux $v ==============="
  if "$SCRIPT_DIR/smoke-newer-kernel.sh" "$tree"; then
    ok=$((ok + 1))
  fi
  echo
done
echo "$ok / $total LTS smoke tests passed"
[[ $ok -eq $total ]] || exit 1
