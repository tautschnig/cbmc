#!/usr/bin/env bash
#
# Integration tests for CBMC TypeScript frontend against real npm packages.
#
# What this does:
#   1. Downloads npm packages (tarball, not via npm install)
#   2. Verifies SHA256 hashes to prevent tampering
#   3. Runs CBMC against a harness that reimplements each package's
#      semantics and asserts invariants
#
# Dependabot integration:
#   Package versions are tracked in package.json. When Dependabot
#   proposes an update, also update packages.sh with the new SHA256:
#     curl -sL https://registry.npmjs.org/<pkg>/-/<pkg>-<version>.tgz | sha256sum
#
# Run locally:
#   ./run_tests.sh
#
# Environment:
#   CBMC           — path to cbmc binary (default: build/bin/cbmc from repo root)
#   NPM_CACHE_DIR  — directory for downloaded tarballs (default: ./cache)
#

set -euo pipefail

# Resolve repo root (2 levels up from this script)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

# Defaults
CBMC="${CBMC:-$REPO_ROOT/build/bin/cbmc}"
NPM_CACHE_DIR="${NPM_CACHE_DIR:-$SCRIPT_DIR/cache}"
EXTRACT_DIR="$NPM_CACHE_DIR/extracted"

# Load package metadata
source "$SCRIPT_DIR/packages.sh"

# Sanity checks
if [ ! -x "$CBMC" ]; then
    echo "ERROR: CBMC binary not found at $CBMC"
    echo "Set CBMC env var or build first with: cmake --build build --target cbmc"
    exit 1
fi

command -v curl > /dev/null || { echo "ERROR: curl required"; exit 1; }
command -v tar > /dev/null || { echo "ERROR: tar required"; exit 1; }
command -v sha256sum > /dev/null || { echo "ERROR: sha256sum required"; exit 1; }

mkdir -p "$NPM_CACHE_DIR" "$EXTRACT_DIR"

download_and_verify() {
    local name="$1" version="$2" expected_sha="$3"
    local tgz="$NPM_CACHE_DIR/${name}-${version}.tgz"
    local extract_subdir="$EXTRACT_DIR/${name}-${version}"

    if [ ! -f "$tgz" ]; then
        echo "Downloading ${name}@${version}..."
        local url="https://registry.npmjs.org/${name}/-/${name}-${version}.tgz"
        curl -sL --fail "$url" -o "$tgz" || {
            echo "ERROR: download failed for $url"
            return 1
        }
    fi

    local actual_sha
    actual_sha=$(sha256sum "$tgz" | awk '{print $1}')
    if [ "$actual_sha" != "$expected_sha" ]; then
        echo "ERROR: hash mismatch for ${name}@${version}"
        echo "  expected: $expected_sha"
        echo "  actual:   $actual_sha"
        echo "  (supply chain attack? update packages.sh if version changed)"
        return 1
    fi

    if [ ! -d "$extract_subdir" ]; then
        mkdir -p "$extract_subdir"
        tar -xzf "$tgz" -C "$extract_subdir" --strip-components=1
    fi

    return 0
}

run_harness() {
    local name="$1"
    local harness="$SCRIPT_DIR/harness/${name}.ts"

    if [ ! -f "$harness" ]; then
        echo "SKIP: ${name} (no harness at $harness)"
        return 0
    fi

    echo "Verifying ${name} harness..."
    # Run under ulimit to bound memory and CPU: 4 GB virtual, 120 s CPU.
    # Symbolic-input harnesses can consume significant resources if there
    # is a pathological case (e.g., regression in frontend causes many
    # branches to be explored).
    if ( ulimit -v 4000000 -t 120 && "$CBMC" "$harness" > /dev/null 2>&1 ); then
        echo "  OK"
        return 0
    else
        echo "  FAILED"
        ( ulimit -v 4000000 -t 120 && "$CBMC" "$harness" 2>&1 ) | tail -15
        return 1
    fi
}

# Main loop
failures=0
total=0
for pkg in "${PACKAGES[@]}"; do
    IFS=':' read -r name version sha <<< "$pkg"
    total=$((total + 1))

    if ! download_and_verify "$name" "$version" "$sha"; then
        failures=$((failures + 1))
        continue
    fi

    if ! run_harness "$name"; then
        failures=$((failures + 1))
    fi
done

echo ""
echo "Results: $((total - failures))/$total harnesses passed"
if [ $failures -gt 0 ]; then
    exit 1
fi
