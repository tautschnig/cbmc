# CBMC: an unexecutable solver binary is reported as per-property `ERROR`, with no diagnostic

**This note replaces a claim that turned out to be false.** Earlier notes in
this tree, and `doc/solver-backend-comparison-2026-11.md`, recorded that
`--cprover-smt2` "returns ERROR for byte-extract properties over anonymous
unions". There is no such limitation. The ERRORs were caused by the solver
binary not being on `PATH`, and the reportable defect is that CBMC says
nothing about it.

## What actually happens

`--cprover-smt2` runs the problem through the external `smt2_solver` binary,
which CBMC looks up by name (`src/solvers/smt2/smt2_dec.cpp:85-88`):

```cpp
case solvert::CPROVER_SMT2:
  argv = {solver_binary_name("smt2_solver")};
  stdin_filename = temp_file_problem();
  break;
```

When that binary cannot be executed, every property is reported as `ERROR`,
the summary line claims nothing failed, and the exit status is 6:

```
$ cat t.c
int main(void){ int x; __CPROVER_assert(x != 5, "x"); return 0; }

$ PATH=/usr/bin:/bin cbmc t.c --cprover-smt2
[main.assertion.1] line 1 x: ERROR

** 0 of 1 failed (1 iterations)
VERIFICATION ERROR
$ echo $?
6
```

**There is no message naming the missing binary, even at `--verbosity 10`.**
The only hint is the word `ERROR`, which reads as a property CBMC could not
decide rather than as a solver it could not start.

With the binary on `PATH` the same inputs verify normally:

| input | `PATH` without `build/bin` | `PATH` with `build/bin` |
|---|---|---|
| `int x; assert(x != 5)` | 1 ERROR, `VERIFICATION ERROR` | `VERIFICATION FAILED` |
| signed subtraction overflow check | 2 ERROR | `VERIFICATION FAILED` |
| byte-extract through an anonymous union | 2 ERROR | `VERIFICATION FAILED` |

So none of the three shapes I had blamed is involved. The third is the one
that produced the false "anonymous union" story: it was simply the first
failing case I looked at closely, and it happened to contain a union.

`--mathsat` behaves identically when MathSAT is absent, so this is not
specific to `smt2_solver`; it is how an unexecutable child solver is handled
in general.

## Why it is worth fixing

The failure mode is indistinguishable from a real incapacity of the solver,
which is exactly the wrong thing to be ambiguous about: it put a wrong row in
a backend comparison table and a wrong mechanism in two documents. A
verification tool reporting `ERROR` for every property, while also reporting
that none failed, is not recoverable by the user without reading the source.

## Suggested fix

Check whether the child was actually started, and if not, fail with a message
that names the binary and the search that failed, rather than mapping the
condition onto per-property `ERROR`. Something of the shape

```
cbmc: cannot execute solver binary 'smt2_solver' (not found on PATH)
```

and a distinct exit status would be enough. If a soft failure is preferred for
scripting, the summary should at least not claim `0 of N failed`.

A regression test would be awkward in-tree, since it depends on the absence of
a binary; a unit test over the decision-procedure construction path, or a test
that points the solver at a deliberately non-existent name, would cover it.

## Reproducer

`solver_binary_missing.c` in this directory is the one-line program above.
Run it twice, once with and once without `build/bin` on `PATH`.

## Scope note

The genuine `--pointer-check` oddity over these driver structs is separate and
still stands: pointer checks report an out-of-bounds write through
`*nb_hw_desc` even when it points at a file-scope static (see the runner
comment in `run_doorbell_budget.sh`). That one is not explained by this note
and has not been reduced.
