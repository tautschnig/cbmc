# TypeScript Taint-Flow Integration Tests

Tests the three-step taint-analysis pipeline:

1. **`cbmc --export-symex-ready-goto prog.gb prog.ts`**
   Compile the TypeScript source to a goto-program binary.

2. **`goto-analyzer --taint spec.json --write-goto-binary prog-i.gb prog.gb`**
   Read the goto binary, instrument with taint-flow assertions per
   the `spec.json`, lower the instrumentation to plain assertions
   (replacing `get_may` calls with concrete booleans from the
   data-flow analysis and `set_may`/`clear_may` other-instructions
   with SKIP), and write the instrumented binary to `prog-i.gb`.

3. **`cbmc prog-i.gb`**
   Verify the instrumented assertions. A tainted path from source
   to sink without sanitizer manifests as an assertion failure with
   a concrete CBMC counterexample.

The orchestration is in `chain.sh`. Each test directory contains:
- `main.ts`     — the program under test
- `taint.json`  — the source/sink/sanitizer rules
- `test.desc`   — expected output

## Taint specification format

```json
[
  {
    "kind": "source" | "sink" | "sanitizer",
    "function": "typescript::FUNCTION_NAME",
    "where": "return_value" | "parameter1" | "parameter2" | ... | "this",
    "taint": "tag_name",
    "id": "rule-id",
    "message": "human-readable diagnostic"
  },
  ...
]
```

## Limitations

The underlying static analysis (`custom_bitvector_analysist`) tracks
taint state on **pointer-typed** values. TypeScript values in our
model are value-typed structs (strings, arrays, objects), so the
analysis falls back to over-approximation: any value that could
syntactically reach a sink is reported as potentially tainted, even
when no source actually fed it.

The workflow is therefore **sound** (no real taint flow is missed)
but **imprecise** (false positives are common). It is most useful
as a coarse triage. For precise per-CVE-class detection, prefer the
contract-style assertion primitives (`__CPROVER_assert_*`) under
`regression/typescript/sec-*` and `regression/typescript/integration-*`.

See [`doc/typescript-known-limitations.md`](../../doc/typescript-known-limitations.md)
§3.4 for the full discussion.

## Running

```bash
make test
```
