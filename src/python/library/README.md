\file
# CBMC Python model library

This directory contains verification-optimized Python models of
selected modules. The CBMC Python front-end consults this directory
first when resolving an `import`; only when a module is not present
here does it fall back to the user's `PYTHONPATH` and then to the
system CPython standard library.

## Goals

* Keep the modelled surface as small as possible (signatures and
  bounded behaviour only) so verification is tractable.
* Preserve Python semantics for the API contract a typical caller
  relies on — types, nullability, obvious invariants — but leave
  internals nondet.

## Non-goals

* Full behavioural fidelity to CPython. Where CPython's
  implementation is complex or performance-tuned, we intentionally
  over-approximate with nondet.
* Replacing the built-in C library models. Modules such as `math`
  are handled by mapping Python function calls directly to the
  corresponding C built-ins (`math.sin(x)` → `sin(x)`); they have
  no entry here.

## Layout

The layout mirrors the Python standard library:

    library/
      urllib/parse.py       # model of urllib.parse
      ...

A model file at `library/foo/bar.py` is used for
`import foo.bar`, `from foo.bar import baz`, and attribute access
`foo.bar.baz`.

## When to add a model

Add a model here when:

1. The module is commonly imported by Python code we want to verify
   and the real CPython source is too complex for our front-end to
   ingest (too slow, triggers an unsupported feature, or returns
   wildly over-approximate values that hurt verification precision).
2. You can write a smaller Python file that captures the
   externally-visible behaviour the caller actually relies on.

If the real CPython source parses cleanly and gives useful results,
do **not** add a model — the system source is preferred.

## Overriding the model

A user can bypass this library entirely by passing
`--python-use-stdlib-source` on the cbmc command line. In that
mode the front-end resolves imports only via `PYTHONPATH` and the
system Python installation, skipping this directory.
