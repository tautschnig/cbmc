# String solver: plan after the soundness audit

This is a working plan spanning several sessions. Update the status of each
item as it progresses, and remove this file once all items are closed. It
is committed on its own so that it can be dropped from any branch prepared
for review.

Context: on branch `shell-strings`, the refinement string solver was audited
for places that should report ERROR rather than a spurious result, and the
bugs found were fixed. The relevant commits, oldest first:

- ERROR instead of spurious results: `c00be10602` (give-ups, undecided
  axiom checks, malformed universal axioms), `0de7a2aa3d` (a pointer
  associated with two arrays), `52d981d754` (counter-example rounds).
- Performance on shared strings: `97ed672a22`, `c5d07d9216`, `f43fa8be22`;
  ADR `89c3241014`.
- Semantic fixes: `13374a0b47` (constant indexOf/lastIndexOf), `01169976b1`
  (empty needle), `60f59713ea` (Python `find` start), `8634edad72` (format
  specifiers), `c9377143db` (int-to-string length), `a92942ad1b`
  (lastIndexOf overflow), `7e82d9dc7b` (counter-example lemmas rewritten),
  `e8abf3d2cd` (code points), `aa57450b0d` (format flags/width/precision),
  `68f2e5d1ca` (string struct fields), `5a9a7c7265` (case conversion),
  `1c7094e7db` (equalsIgnoreCase), `1d783b82a0` (integer parsing),
  `aa623ed417` (float printing).
- Approximation warnings: `8d3420680a`.
- Tests for kept behaviour: `fb539b38fe`.

Status values: open, in progress, done (with the commit or PR).

## 1. Split the solver work into reviewable pull requests

Status: open

`shell-strings` carries about 20 solver commits on top of the shell
front-end work. Many fix bugs that also exist upstream (diffblue/cbmc): the
semantic fixes above apart from the Python one. Prepare them as a separate
series, grouped by topic (ERROR verdicts; indexOf; format; integer
conversions; case; float printing; warnings), each with its tests. Pushing
and opening PRs needs the user's go-ahead.

## 2. Differential testing against the JVM and CPython

Status: open

All bugs so far were found by reading code and hand-written probes. Write a
generator of small programs applying one or two string functions to
constant or lightly constrained inputs, run them with jbmc/cbmc and with
the real runtime, and compare verdicts. Each discrepancy is a bug and a
ready regression test. The float formatter was validated this way, with a
script comparing against Java 25 and CPython 3.12 on 27949 values; the edge
cases are in `unit/solvers/strings/string_float_format/format_float.cpp`.

## 3. Audit the native SMT string path

Status: open

Only the refinement solver was audited. The translation in `smt2_conv` for
`--cvc5`, `--python-smt-strings` and `--shell-strings native` likely has its
own mismatches (index_of start clamping, find, replace, conversions). The
tester of item 2 should cover both back ends.

## 4. Replace the most frequent approximations

Status: open

The solver now warns: `string refinement: <function> <reason>, so a
counterexample may be spurious`. Count where the warnings fire across the
regression suites and the shell, Python and Java benchmarks, then model the
most frequent cases exactly. Likely candidates: regular expressions on
symbolic subjects, replace with symbolic arguments, non-ASCII case
conversion for Python (needs UTF-8 decoding), and the strings of floats
whose results are currently checked per model.

## 5. Java models library gaps

Status: open

These need changes in the `jbmc/lib/java-models-library` submodule:

- `offsetByCodePoints` does not throw when the result is out of range.
- `String.format` passes `""` for missing arguments instead of throwing
  MissingFormatArgumentException.
- `isValidInt`/`isValidLong` reject strings with more leading zeros than the
  maximum length allows, which Java accepts.
- `CProver.doubleToFloat` assumes that the double is a float; string
  conversion no longer uses it (`aa623ed417`), but check other users.

## 6. Performance costs that were accepted

Status: open

- `StringToLowerCase/test_dependency`: 0 s to 10 s (case conversion that
  may change the length; constraining the whole suffix took over 300 s).
- `int-octal-fail` kernel: 1.5 s to 2.7 s (range check of isValidLong).
- `refined-mktemp-pattern`: 41 s to 53 s (counter-example rewriting); also
  `verify-patterns-unbounded` went from 26 s to 14 s.
- `at-command/words` (native cvc5) takes 460 s, before and after these
  changes; consider tagging it THOROUGH.

Profile these for cheap improvements.

## 7. Probe the new ERROR limits

Status: open

The float-conversion check returns ERROR after 256 rounds, as do the
counter-example rounds without new index terms. Write tests where a
symbolic float is constrained through string predicates, and programs that
hit the counter-example cap, to see whether realistic programs reach them.

## 8. Documentation

Status: open

Extend `doc/ADR/dynamic_structure_resolution.md` (or a new ADR) with: when
the string solver reports ERROR, the approximation warnings and how to read
them, and the per-model check of float conversions.
