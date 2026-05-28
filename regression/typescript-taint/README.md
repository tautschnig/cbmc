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

## Precision

The underlying static analysis (`custom_bitvector_analysist`) tracks
taint state on both pointer-typed and value-typed values (the
latter as of commit extending the analysis to handle
non-pointer operands). For TypeScript struct values (strings,
arrays, objects), set_may / clear_may / get_may operate on the
struct identifier and propagate through member-by-member struct
copies via `assign_struct_rec`.

The pipeline is therefore both **sound** (no real taint flow is
missed) and **precise enough for many real cases**: if the
workflow reports VERIFICATION SUCCESSFUL, no source-to-sink path
exists; a VERIFICATION FAILED result usually corresponds to a real
flow.

The contract-style assertion primitives (`__CPROVER_assert_*` under
`regression/typescript/sec-*` and `regression/typescript/integration-*`)
remain useful for finer-grained checks at specific program points
where the taint hooks are coarse.

## Running

```bash
make test
```
