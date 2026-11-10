\page string-solver-results What the results of the string solver mean

**Date**: 2026-10-30
**Status**: Accepted

## Context

The string refinement solver (`src/solvers/strings`, used by JBMC by
default, by CBMC with `--refine-strings`, and by the Python and shell
front-ends with refined strings) decides formulas with string functions
by generating axioms for each function, instantiating the universal ones
at index terms, and checking each model of the propositional solver
against all axioms (`check_axioms`). A model that violates an axiom is
excluded by further instances or by counter-example lemmas, and the loop
goes on.

Before the audit of October 2026, the solver could report a result that it
had not established, in three ways:

1. **Giving up with a verdict.** When neither new index terms nor new
   counter-examples could exclude a model that violated the axioms, or
   after a fixed number of such rounds, the solver took the model as a
   counterexample (`D_SATISFIABLE`, with only a warning), so VERIFICATION
   FAILED was reported with a trace that the string semantics did not
   allow. Axioms that the axiom check could not decide were treated as
   satisfied.
2. **Silent approximations.** Several functions had partial axioms, which
   left the result unconstrained or only bounded for some arguments
   (regular expressions on symbolic subjects, replace of strings,
   unsupported format specifiers, code points), so a counterexample could
   be spurious without any indication.
3. **Wrong axioms.** Some axioms disagreed with Java or Python (see the
   commits of the audit, listed in `doc/string-solver-plan.md`). Where they
   contradicted the real semantics, paths became infeasible and false
   assertions were proved; elsewhere, true assertions were reported as
   failing.

The third kind are bugs and were fixed. This record is about the first
two: what the solver reports when it cannot establish a result exactly.

## Decision

A result of the string solver means one of three things.

- **UNSAT** (VERIFICATION SUCCESSFUL, or a property that holds): no
  assignment satisfies the axioms. By design, the axioms of each function
  are exact or over-approximate it, so this is sound, also where
  approximation warnings were given. (Bugs in the axioms, and in the
  language models that call the string functions, can still break this;
  the regression tests compare with Java and CPython.)
- **SAT** (VERIFICATION FAILED, with a trace): a model that satisfies every
  axiom, as checked in that model, and every checked conversion (see
  below). If an approximation warning was given, the counterexample may not
  be possible in the real program.
- **ERROR** (VERIFICATION ERROR): the solver stopped without establishing
  either. A warning before it says why.

### When the solver reports ERROR

Each case prints one of these messages (prefixed `string refinement:`
unless noted):

| Message | Cause |
|---|---|
| `a pointer is associated with two different arrays, which is not supported, so the result is not known` | The front-end associated one character pointer with two arrays. |
| `dec_solve: a pointer whose string length was set by an earlier call is now found with a length that may differ` (an error, not a warning) | The lengths of a string given by two calls may differ. |
| `the axioms could not all be checked in the model, so the result is not known` | The check of an axiom in a model returned ERROR, and no axiom was found violated. |
| `the model violates the string axioms, and neither index terms nor counter-examples at new positions exclude it, so the result is not known` | The refinement cannot make progress: no new index terms, and the counter-examples repeat positions already used. |
| `the model still violates the string axioms after 256 rounds of counter-examples without index terms, so the result is not known` | The counter-examples were at new positions in 256 consecutive rounds without new index terms. |
| `the strings of floating-point values were still wrong after 256 rounds, so the result is not known` | See the conversions of floating-point values below. |

The refinement loop itself is unbounded by default (`refinement_bound`),
so the last two caps are what bound the rounds that only counter-examples
or conversion lemmas advance. The caps are constants
(`max_empty_index_set_rounds`, `max_float_rounds` in
`string_refinement.cpp`); there is no command-line option for them.

A universal axiom whose form the instantiation does not support (for
example, a universal variable with a coefficient other than ±1) is not
dropped: it is not instantiated, but it is checked in every model, and its
counter-examples are added every round, as for `not_contains` axioms. Such
axioms can lead to the ERROR cases above, but never to a wrong verdict.

### Approximation warnings

Where the axioms of a function over-approximate its result, the generator
prints, once for each function and reason:

    string refinement: <function> <reason>, so a counterexample may be spurious

as the propositional encoding does for quantifiers it cannot instantiate.
The solver then proceeds; an UNSAT result is still sound, and a SAT result
may be a false alarm. The warnings are:

| Function | Reason |
|---|---|
| `regular expression matching`, `regular expression search positions`, `re.sub` | only modelled for constant, supported patterns and subjects (or arguments) |
| `cprover_string_repeat_func`, `cprover_string_compare_func`, `cprover_string_chr_func`, `cprover_string_index_of_from_func`, `cprover_string_split_func` (Phase 3 intrinsics, not emitted by any front-end yet) | not modelled |
| `a string literal` | not a constant |
| `replace` | only modelled for a character, or for constant arguments |
| `String.format` | a non-constant format string; an unsupported specifier (`%e %f %g %a %h %o %t`); flags, widths, precisions or missing arguments; `%x` of a value that may be negative |
| `codePointCount`, `offsetByCodePoints` | exact only for strings without surrogate pairs |
| `upper-case conversion`, `lower-case conversion` | only ASCII (UTF-8 strings, as for Python) or characters below U+0100 that convert within that range (UTF-16 strings, as for Java) |
| `equalsIgnoreCase` | only ASCII or characters below U+0100 |

These are given when the constraints for the function are generated,
whether or not the model depends on the approximated part: a warning says
that a counterexample *may* be spurious, not that it is. Only the case
conversions check their input first, and give no warning for a constant
string whose characters are all modelled; `%x` gives none for an argument
that simplifies to a non-negative constant.

### Conversions of floating-point values

The string of a float or double (Java's `Float.toString` and
`Double.toString` in plain and scientific notation, Python's `repr`) is
computed by `format_float` (`string_float_format.cpp`): the shortest
decimal that reads back as the value, with Java's rule of two digits where
one would do, in the notation of the language. A constant value gets the
axioms of its string. For a symbolic value, the result is left
unconstrained by the axioms and recorded; in each model, before the
axioms are checked, the solver compares the result with `format_float` of
the value in that model. Where they differ, it adds the lemma that this
value gives this string (under the guard of the call) and solves again.
A model is accepted only if every reached conversion is right. Each such
round excludes one value, so a constraint on the strings that many values
satisfy can take many rounds; after 256, the result is ERROR.

## Alternatives considered

- **Keep taking the model as a counterexample when giving up**, with a
  warning. Rejected: it reports a failure that may not exist, with a trace
  that does not explain it, and the warning was easy to miss. The cap of
  256 rounds would also have become a cliff beyond which results silently
  change.
- **Report ERROR whenever an approximation is present.** Measured on the
  regression suites: 8 tests reach a SAT result with an approximated
  function in the formula; 6 of them are real failures in Java or CPython,
  so ERROR would have hidden real bugs. Rejected in favour of the warning.
- **Validate approximated results in the model** with concrete evaluators,
  as for the conversions of floating-point values, and add lemmas where
  they disagree. Adopted for those conversions; for the other functions it
  needs an evaluator per function (for example, the regular expression
  matcher on the model's strings), and is future work.
- **A command-line option for the caps.** Not added for now: the most
  rounds seen were 47, for the pattern match of a name that `mktemp`
  makes (shell test `refined-mktemp-pattern`).

## Consequences

- A VERIFICATION ERROR from a string program means that the solver could
  not decide it, not that the program is wrong. Reducing
  `--max-nondet-string-length`, or constraining the strings involved, can
  help.
- A VERIFICATION FAILED preceded by an approximation warning should be
  checked: run the trace, or see whether the property depends on the
  function named in the warning.
- Tests that expect FAILED for an approximated function also match the
  warning, so a change that makes the function exact shows up in the tests
  (`StringApproximationWarnings`, `StringCodePointsExact`,
  `regex-symbolic-warning`).
