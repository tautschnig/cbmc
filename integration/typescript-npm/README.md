# TypeScript NPM Integration Tests

This directory contains integration tests that verify the CBMC TypeScript
frontend against real npm packages. Each package has a harness that
reimplements the package's core semantics in TypeScript and asserts
invariants that must hold.

## Quick start

```bash
# From this directory
./run_tests.sh
```

Or from the repo root:
```bash
integration/typescript-npm/run_tests.sh
```

## How it works

1. **Package list**: `packages.sh` defines pinned versions and SHA256 hashes.
2. **Download**: The script downloads npm tarballs and verifies hashes.
3. **Harness**: For each package, `harness/<name>.ts` reimplements the
   package's core logic and asserts invariants over **symbolic inputs**
   (`nondet_number()` + `__CPROVER_assume()`). This exercises CBMC's
   branch exploration rather than just checking concrete constants.
4. **Verification**: CBMC verifies the harness passes all assertions.
5. **Mutation testing** (`run_mutation_tests.sh`): introduces known bugs
   into each harness and requires each bug to be detected. Guards against
   harnesses silently degrading into tautologies over time.

We **don't** run the raw JavaScript from npm — those packages are .js files
that CBMC's TypeScript frontend can't directly verify. Instead, harnesses
document the expected semantics in TypeScript and verify structural
invariants. Dependabot tracks the package versions so we know when upstream
releases new versions (and can update harnesses to reflect any semantic changes).

## Mutation testing

`run_mutation_tests.sh` runs a set of sed-based mutations against each
harness (e.g., swapping `>=` for `>`, dropping a field comparison) and
requires each mutation to be caught by the harness's assertions. If any
mutation is NOT caught, the harness is tautological for that property
and the script exits non-zero.

This runs in CI alongside the main integration tests. Whenever a harness
is added or edited, consider adding a representative mutation that
exercises the new property — otherwise the harness could silently become
a tautology.

## Package tracking

Versions are listed in:
- `package.json` — enables Dependabot to propose version bumps via PRs
- `packages.sh` — adds SHA256 hashes for tamper detection

When updating:
1. Dependabot proposes a version bump in `package.json`
2. Maintainer updates `packages.sh` with the new SHA256:
   ```bash
   curl -sL https://registry.npmjs.org/<pkg>/-/<pkg>-<version>.tgz | sha256sum
   ```
3. Re-run `./run_tests.sh` to ensure harness still passes
4. If the package's semantics changed, update `harness/<name>.ts` accordingly

## Currently covered packages

| Package | Version | Harness focus |
|---------|---------|---------------|
| ms | 2.1.3 | Time unit constants and classification |
| classnames | 2.5.1 | String concatenation invariants |
| uuid | 9.0.1 | UUID structural equality |
| left-pad | 1.3.0 | Length-based padding properties |

## Adding a new package

1. Add entry to `package.json` devDependencies
2. Compute SHA256 and add to `packages.sh`
3. Write harness at `harness/<name>.ts` that verifies package invariants
   using symbolic inputs (`nondet_number()` + `__CPROVER_assume()`)
4. Add at least one entry to `MUTATIONS` in `run_mutation_tests.sh` that
   represents a plausible bug in the new harness
5. Run `./run_tests.sh` and `./run_mutation_tests.sh` to confirm

## CI integration

CI runs `./run_tests.sh` as a separate job. It's kept in `integration/`
(not `regression/typescript/`) because:
- It has external dependencies (npm registry)
- It's slower (downloads)
- It's more fragile (upstream could change, network could fail)
- It's a different kind of test (ecosystem compatibility vs unit correctness)

## Environment

- `CBMC` — path to cbmc binary (default: `$REPO_ROOT/build/bin/cbmc`)
- `NPM_CACHE_DIR` — cache directory for downloaded tarballs (default: `./cache`)

## Requirements

- `curl` (for downloads)
- `tar` (for extraction)
- `sha256sum` (for hash verification)
- A built CBMC binary with TypeScript frontend
