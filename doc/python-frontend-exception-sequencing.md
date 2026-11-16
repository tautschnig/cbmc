# Structured exception propagation

Exceptions are modelled by `__exception_active` / `__exception_type` /
`__exception_payload` (see "Exception model" in
python-frontend-architecture.md). This document describes how the code after
a possible raise is sequenced so that PLR 8.4 holds, and why it is cheap.

## Invariant

While `__exception_active` holds, no statement runs except the ones that
observe the exception: handler dispatch, `finally` bodies, `__exit__` calls
and the uncaught-exception properties.

Before this work the invariant held only for the top-level statements of a
try body. Three shapes broke it, each a false proof:

* a raise in a nested block (if/loop) of a try body did not skip the rest of
  that block or loop;
* after a call that raised, the calling function continued;
* (consequently) a raise in a loop inside a callee was not seen by the
  caller's later code.

## Mechanism

Two passes run once all bodies are converted (end of `convert()`), over every
function body and the module body.

1. **Which functions can raise** (`compute_raising_functions`): a function
   raises if its body sets the flag, calls an unknown target, or calls a
   function that raises (fixpoint). A call of a function that cannot raise
   leaves the flag unchanged and needs no guard.

2. **Uncatchable raises end the path** (`lower_uncatchable_raise`). PLR 8.4:
   an exception that no handler can catch ends the program. Outside every
   try/with body, a raise (explicit or a runtime check such as an index or
   zero-division check) whose type no handler can catch is reported where it
   is raised and the path ends (`assert !raised; assume !raised`), instead of
   setting the flag. Whether a handler can catch it comes from a call graph
   over the AST (`collect_assert_raise_functions`): each call inside a
   protected region gives the callee the handler names of that region, and
   callees inherit their callers' names; `handler_catches_exception` matches
   by name, the built-in hierarchy and user MROs. A bare except, a finally
   clause or a with body counts as catching anything; a with statement whose
   managers cannot suppress and whose `__exit__` has no checks or calls is
   transparent. A missed call edge can only turn a catchable raise into a
   reported failure, never hide one. In `--function` mode the path ends
   without a property, as before. Iteration (`StopIteration`) and bare
   `raise` keep the flag.

3. **Sequencing** (`sequence_exceptions`), for the raises that remain:
   * in every block, each statement after one that may raise is guarded by
     `!__exception_active`;
   * a `for` loop over a container whose body may raise gets its body
     guarded (the loop runs out); a `while` loop gets the flag in its
     condition;
   * a call that may raise inside an expression: the front-end tags the call
     and appends a SKIP that ends its Python statement
     (`tag_raising_calls`); after goto conversion, which lifts the call out
     of the expression, `instrument_python_call_exceptions` inserts
     `IF __exception_active THEN GOTO <that SKIP>` right after the call
     (cbmc's process_goto_program, before remove_skip). This covers the
     rest of the expression, the store, return values, the selected branch
     of an if, and short-circuit operands;
   * a raise in a function no longer returns early; the function leaves with
     the exception at its end.

   Writes of the exception record (type, payload) are part of raising and are
   never guarded; handler dispatch, finally and `__exit__` lowerings are
   marked (`ID_python_exc_observer`, `ID_python_exc_opaque`,
   `ID_python_exc_try`) when generated.

Generated code that has no location of its own gets the location of the
Python statement it belongs to (`convert_statement_inner`), so properties
from runtime checks are attributed to their line.

## Why it is cheap

The flag only stays symbolic where an exception can actually be caught. With
sequencing alone (no step 2), every runtime check made the flag symbolic, and
pyhard's symex steps went from 97 k to 453 k; the lowering style (guards,
nested guards, breaks, returns) did not change that, because the cost was the
symbolic flag itself. With step 2, pyhard is back at 94 k steps.

## Measurements

| Run | Old model | Final |
|---|---|---|
| pyhard benchmark | 34 s, 97 k steps, 0/102 failed | 38 s, 94 k steps, 0/120 failed (uncaught-exception properties now per raise site) |
| real-world checker, 4 concrete rows (c1) | symex 408 s, 1.10 M steps, solver out of memory | 16 s, verdict correct |
| same, other verdict (c2) | same | 11 s, verdict correct |
| same, 16+ rows (c3, c4) | not run | 49-55 s, verdicts correct |

Every concrete verdict was also checked with the harness expectation
inverted (must fail). The remaining c1 solver wall was string refinement on
`int(text, 16)` of constant strings, now evaluated during symbolic execution
(`simplify_string_parse_int`), with the parse of a runtime-typed value done
only under its STR tag.

## Not covered

* Runtime checks of a statement are emitted before the whole statement, not
  in Python's evaluation order: in `box[i] = f()` the IndexError check on
  `box[i]` runs before `f` (PLR 6.16: the right-hand side first). If both
  would raise, the wrong exception type is modelled.
* Handler and finally bodies keep their own internal sequencing.
