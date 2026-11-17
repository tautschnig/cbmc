# Timing the string solver

Tools to measure changes to the string solver (`src/solvers/strings`) on the
JBMC regression tests and on benchmarks. Bound the memory of each run
(`ulimit -v`): the scripts bound each JBMC run to 8 GB, but parallel runs
add up.

* `partest.sh DIR N CMD LOG` runs `regression/test.pl` on the test
  directories of `DIR` in `N` parallel groups (for example
  `jbmc/regression/jbmc-strings`, 6 groups, `CMD` a JBMC wrapper with a
  timeout).
* `cmplogs.py A.log B.log` compares two such logs: status changes, total
  time and the tests that changed most. `test.pl` reports whole seconds and
  parallel runs disturb each other, so re-time candidates with `cmpenv.sh`.
* `cmpenv.sh VAR JBMC DIR:NAME...` times regression tests without and with
  the environment variable `VAR=1`, for a spike gated by an environment
  variable.
* `intstr/run.sh JBMC CLASS OUT [FUNCTION...]` times the functions of a
  benchmark class.

## Benchmarks of strings of symbolic integers (`intstr`)

* `B`: lengths, witnesses, equality, round trips (`parseInt(toString(x))`)
  and injectivity of the strings of ints and longs.
* `M`, `K`: strings of integers merged at join points of the program.
* `P`: equalities of concatenations with strings of integers.
* `Q`: injectivity over parts of the range of int and long.
* `C`: chains of concatenations, of strings and of strings of ints.
* `S5`..`S40`: a symbolic character of a concatenation of the strings of
  5 to 40 ints, which measures the cost of the injectivity lemmas, which are
  quadratic in the number of conversions.

## Measurements

### Injectivity lemmas of the strings of integers

Equal strings of two integers of the same radix imply equal integers
(`add_axioms_for_string_of_int_with_radix`). The axioms imply it through
the sums of the digits, which the SAT solver relates poorly. Times in
seconds (300 s timeout) before and after the lemmas:

| benchmark | before | after |
|---|---|---|
| B.intInjective | timeout | 0.37 |
| B.longInjective | timeout | 1.02 |
| P.inj1 | timeout | 0.52 |
| Q.posAll | 132.6 | 1.16 |
| Q.negAll | 270.8 | 0.55 |
| Q.pos9 | 123.0 | 0.83 |
| Q.long6 | 6.29 | 5.12 |
| Q.pos6 | 4.22 | 4.64 |
| C.chainInt10 | 1.57 | 2.49 |

The other benchmarks changed by less than 0.2 s, except P.digitsOnly,
which loops without bound in both (with `--unwind 12`: 1.7 s). On jbmc-strings (1348
tests, 6 parallel groups) the total went from 2359 s to 2270 s with no
status change; re-timed alone: StringBuilderChecks/charAtNot8 23 -> 11 s,
StringIndexMessages/java17 63 -> 56 s, java14 70 -> 61 s, JavaLocale/ascii
15 -> 12 s; NumberParseMessages/input11 50 -> 58 s.

The lemmas are quadratic in the conversions whose characters the
constraints need (`S*`, variables and clauses of the SAT instance):

| conversions | clauses before | clauses after |
|---|---|---|
| 5 | 769567 | 810963 |
| 10 | 289232 | 346157 |
| 20 | 575616 | 815966 |
| 40 | 1150456 | 2137156 |

A first version of the lemmas did not take the guards of the conversions
(string_dependenciest) into account: it related conversions on different
paths of the program, which cost up to 47 % (StringFormatCallOrder/hash 4.0
-> 5.8 s, StringFormatBigNumbers/symbolicBad 19.5 -> 26.3 s), and which
the guards remove (3.3 -> 3.3 s, 17.1 -> 17.2 s).

### Rejected spikes

* Index terms `max(0, l)` of a length `l` replaced by `l`
  (`add_to_index_set`): StringFormatDecorations/c67 37 -> 5.5 s, german 17
  -> 11 s, java17 69 -> 59 s, but NumberParseMessages/input11 51 -> 66 s,
  StringIndexMessages/notJava8 86 -> 99 s, StringFormatValues/c28 8.6 ->
  12.4 s, StringFormatHexZeros/wide 9.7 -> 13.5 s. Combined with the first
  (unguarded) injectivity lemmas, java17 took 68 s, against 56 s with the
  lemmas alone. `spikes/index-normalize-and-cap.patch` (environment
  variables `CBMC_INDEX_NORMALIZE` and `CBMC_INDEX_CAP`).
* The number of new index terms per array and refinement round
  (`filter_index_set`, 2): StringIndexMessages/notJava8 86 s at 2, 121 s at
  4, 191 s at 8, over 300 s at 1000; StringBuilderChecks/charAtAny 10.5,
  12.9, 24, 82 s. The cap stays 2.
