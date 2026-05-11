# Deferred Work — Precision Plan Continuation

This document records the remaining items from the
13-task continuation plan. They were not landed in the
current session because each has either large scope or
requires infrastructure work that deserves dedicated
attention.

## PLR §4 Execution Model — probed, no new bugs

Tested patterns for PLR §4.2.1 (default-argument
evaluation timing), §4.2.2 (class-body scope),
§6.2.4 (comprehension scope), and basic closure
nesting. All work correctly on our frontend.

Not a "done" in the sense of implementing new work —
more a clean bill of health for name-resolution
semantics. The deeper PLR §4 subsections (metaclass
interactions, descriptor protocol) were not probed
and might still contain issues.

## #3 Structural match patterns (MatchSequence,
MatchClass, MatchMapping)

Status: match/case already covers MatchValue,
MatchSingleton, MatchOr, MatchAs, and guards
(regression `match-case`). The remaining pattern
kinds are:

- `MatchSequence`: `case [a, b, *rest]:` — destructure a
  sequence by length and bind positions.
- `MatchClass`: `case Point(x, y):` — isinstance check
  + attribute extraction.
- `MatchMapping`: `case {"key": v}:` — dict key check
  + value bind.

Each needs pattern-compilation work:

- Sequence: check `isinstance(subj, list)` + length
  match + per-element recursive compile_pattern.
- Class: `isinstance(subj, T)` + for each positional
  pattern, extract `T.__match_args__[i]` attribute +
  recursive compile_pattern. Keyword patterns: extract
  named attribute.
- Mapping: similar to sequence but on dict keys.

Rough estimate: 200-250 lines, all within the
existing match-case compile_pattern lambda.

## #4 sorted(key=callable)

The builtin `sorted(lst, key=lambda x: x.attr)` requires:

- Evaluating the lambda per element — our lambdas are
  already supported (function_aliases mechanism), so
  the per-element call is feasible.
- A comparator-aware sort. The current bubble-sort
  compares elements directly; with key=, we need to
  compute key(a) vs key(b) per comparison.

Scope: ~80-120 lines. Defer pending sorting
infrastructure work that would also benefit sorted()
without key= (e.g. merge-sort unroll).

## #6 str.replace content for symbolic strings

The solver exposes `cprover_string_replace_func`. We
currently only handle constant strings at parse time;
symbolic strings fall through to nondet.

Wiring this up requires:

- A new helper that takes 3 string args (src, old,
  new) and emits `cprover_string_replace_func` +
  associate_* pre-declarations (same pattern as
  of_int).
- Routing str.replace's symbolic path through the
  helper.

Rough estimate: 50-80 lines. Straightforward once
str.format content tracking (a similar pattern) is
landed.

## #9 Exception groups (`except*`, `TryStar`, `ExceptionGroup`)

PEP 654 (Python 3.11) introduces:

- `raise ExceptionGroup("msg", [ExcA(), ExcB()])` —
  raise multiple exceptions at once.
- `try: ... except* ExceptionType:` — match any
  exception in a group of that type; unmatched ones
  re-raise.
- AST node `TryStar` mirrors `Try`.

Current state: our frontend converts `TryStar` via the
same `convert_try` as regular `Try`, which does NOT
understand group semantics. Verification runs but
every `except*` matches the whole flag, not just the
matching types.

Proper implementation requires:

- Track groups as lists of exception-type hashes.
- `except* T:` matches any group element with type T;
  unmatched elements re-raise.
- Partial matching: a group with {A, B} running through
  `except* A:` catches A; B re-raises to the next
  handler.

Rough estimate: 150-200 lines in convert_raise /
convert_try plus a new group-state symbol in the
exception flag machinery.

## Summary

The 13-task continuation landed 8 substantive commits,
all correctness-oriented, with 5 latent bugs fixed via
PLR cross-referencing. The 5 deferred items are
enumerated above with scope estimates.

Picking up from here, the next session can directly
tackle any one of the five without needing to re-plan.
