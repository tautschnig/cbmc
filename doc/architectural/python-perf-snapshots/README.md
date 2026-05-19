\file
# Persisted perf-report snapshots — AWS Python benchmark suite

Three text files per benchmark, captured under
`RelWithDebInfo` `cbmc` at the same revision used by the
performance analysis in
`../python-perf-analysis.md`:

* `<bench>.report-self.txt` — `perf report --no-children
  --percent-limit 0.5`. Self-time only (cycles spent
  literally executing the function in question, not its
  callees). Useful for spotting tight inner loops.
* `<bench>.report-children.txt` — `perf report --children
  --no-call-graph --percent-limit 0.5`. Self + descendant
  time per symbol, no call-graph context. The headline
  table for "where is the run spending its budget".
* `<bench>.report-callgraph.txt` — `perf report --children
  --percent-limit 5.0 -g graph,5,callee`. Trimmed
  callgraphs (5 % threshold, top-5 callee chains) for the
  hottest paths.

## Benchmarks captured

| Benchmark | Backend | Wall (s) | Notes |
|---|---|---:|---|
| `aws_untagged-default`              | default | 34.2  | Heaviest CBMC-bound benchmark |
| `test_bedrock_guardrails-cvc5`      | cvc5    | 51.4  | Mixed CBMC + cvc5 |
| `s3_backup_restore-cvc5`            | cvc5    | 64.5  | Solver-bound (86.5 % cvc5) |
| `ecs_utils-cvc5-pre-fastpath`       | cvc5    | 31.6  | **Before** `irept::compare` SHARING fast-path |
| `ecs_utils-cvc5-post-fastpath`      | cvc5    |  3.4  | After the fast-path; 10× faster |

## Raw `perf.data` archive

The original `perf.data` files (not committed here — too
large) live at:

```
~/python-perf-archive/<bench>.perf.data
~/python-perf-archive/<bench>.perf.data.zst
```

Total raw: 1.4 GB. Total compressed: 38 MB.

To regenerate the text reports from a `.perf.data` file:

```bash
zstd -d <bench>.perf.data.zst   # if compressed
perf report -i <bench>.perf.data --stdio --percent-limit 0.5 \
    --no-children > <bench>.report-self.txt
perf report -i <bench>.perf.data --stdio --percent-limit 0.5 \
    --children --no-call-graph > <bench>.report-children.txt
perf report -i <bench>.perf.data --stdio --percent-limit 5.0 \
    --children -g graph,5,callee > <bench>.report-callgraph.txt
```

To regenerate from scratch (full recipe is in
`../python-perf-analysis.md`):

```bash
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=RelWithDebInfo -DWITH_JBMC=OFF
cmake --build build-debug --target cbmc -j$(nproc)
sudo sysctl kernel.perf_event_paranoid=-1
export PYTHONPATH=$HOME/python-verification-benchmarks/stubs-full-python
ulimit -v unlimited
perf record -g --call-graph dwarf,8192 -F 997 -e cycles:u \
    -o <bench>.perf.data \
    -- build-debug/bin/cbmc \
    $HOME/python-verification-benchmarks/python-sources/<bench>.py \
    --object-bits 12 --no-unwinding-assertions --unwind 3 \
    --python-no-exception-checks --python-required-kwarg-checks \
    --python-check-typeddict-fields                    # for default
    [--smt2 --cvc5]                                    # for cvc5
```
