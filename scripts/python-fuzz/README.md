# CBMC Python Front-End Fuzz Testing

This directory contains a fuzz-testing harness for CBMC's Python front-end,
adapted from Strata Contributors' upstream
`Tools/Python/scripts/{hypothesmith.sh,gen_random_python.py,gen_unrestricted.py}`
(`origin/tautschnig/hypothesmith` branch on
[github.com/strata-org/Strata](https://github.com/strata-org/Strata)).

The Strata version drives Strata's `pyAnalyzeLaurel` pipeline; the version
here drives CBMC instead. The program-generator scripts
(`gen_random_python.py`, `gen_unrestricted.py`) are unchanged in logic from
upstream — only docstrings and copyright headers were updated to reflect
the new consumer.

## Files

| File                    | Source              | Purpose                              |
|-------------------------|---------------------|--------------------------------------|
| `python-fuzz.sh`        | adapted from `hypothesmith.sh` | Driver: invokes the generator and runs CBMC |
| `gen_random_python.py`  | upstream verbatim   | Two-mode program generator           |
| `gen_unrestricted.py`   | upstream verbatim   | ~60 extended generators              |

## Modes

* **Syntax mode** uses the
  [hypothesmith](https://github.com/Zac-HD/hypothesmith) library's
  `from_grammar()` strategy to produce syntactically valid Python from the
  grammar. CBMC is run on each program; the failures of interest are
  CBMC crashes (`Invariant check failed`, panics, segfaults) on
  CPython-acceptable input. Programs CBMC's frontend declines as
  unsupported are reported as `SKIP`.

* **Semantic mode** uses a custom generator that emits typed Python
  programs with assertions whose expected values are computed by CPython
  at generation time (the CSmith-style "checksum" idea). Each program is
  first sanity-checked under CPython. If CBMC reports
  `VERIFICATION FAILED` on a CPython-correct program, that is a semantic
  modelling bug in the Python -> goto translation.

## Quick start

```sh
# From the repository root, with CBMC built (build/bin/cbmc):

# 10 programs in each mode, seed = current timestamp
./scripts/python-fuzz/python-fuzz.sh

# Reproduce a specific failure:
./scripts/python-fuzz/python-fuzz.sh 20 1234567890 both --unrestricted
```

The script auto-creates a Python virtual environment at
`scripts/python-fuzz/.venv/` and installs `hypothesmith` into it on first
run. Subsequent runs reuse the venv.

## Result classification

Each generated program ends in one of:

| Outcome           | Meaning                                                  |
|-------------------|----------------------------------------------------------|
| `OK`              | CBMC produced a clean verdict (matches CPython where applicable). |
| `SKIP (parse)`    | Frontend rejected the program at parse / type-check time. |
| `SKIP (unsupported)` | Frontend printed a `warning: ignoring` / `no body for callee`. |
| `TIMEOUT`         | CBMC exceeded the per-program time budget.               |
| `FAIL (crash)`    | CBMC hit an Invariant violation, panic, or signal.       |
| `FAIL (verification)` | CPython accepts the program; CBMC reports VERIFICATION FAILED. |
| `BUG (generator)` | The generated program itself fails under CPython (generator bug, not CBMC). |

When the script exits non-zero, the failing program's full source code is
printed alongside the CBMC output for reproduction.

## CI integration

`.github/workflows/python-fuzz.yaml` runs `python-fuzz.sh 20 <seed> both
--unrestricted` on every PR against `develop`. The seed is generated from
the current nanosecond timestamp and printed in the workflow log so any
failure can be reproduced deterministically with the same seed locally.

The fuzz workflow is not a required check — it surfaces regressions and
new bugs but does not gate merges.

## Environment variables

| Variable        | Default                              |
|-----------------|--------------------------------------|
| `CBMC`          | `<repo-root>/build/bin/cbmc`         |
| `CBMC_TIMEOUT`  | `30` (seconds per program)           |
| `CBMC_UNWIND`   | `5` (loop unwind bound)              |
| `CBMC_MEM_MB`   | `4096` (per-CBMC memory cap, MiB; `0` disables) |
| `PYTHON`        | `<this dir>/.venv/bin/python3`       |

### Memory safety

The harness sets `ulimit -v $((CBMC_MEM_MB * 1024))` at the top of the
shell so every subprocess it spawns — CBMC, the hypothesmith generator,
the CPython sanity-check run — is bounded. Without this, a pathological
fuzz program can blow up CBMC's solver memory and OOM-kill the host. The
default 4 GiB is comfortably above CBMC's steady-state on the small
generated programs; raise `CBMC_MEM_MB` if you see legitimate fuzz
programs hitting the cap, or set `CBMC_MEM_MB=0` for diagnostic runs.

## License / attribution

The Strata-derived files are licensed under Apache-2.0 OR MIT. CBMC-side
adaptations follow CBMC's existing 4-clause BSD terms.
