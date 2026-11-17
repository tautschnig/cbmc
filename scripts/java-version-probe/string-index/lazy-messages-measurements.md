# Exception messages of the index checks: eager vs lazy (2026-11-16/17)

Wall-clock seconds, JBMC release build, one run each (MiniSat), tests from
jbmc/regression/jbmc-strings. "no version" = no --java-version (symex merges
the versions).

| variant | StringFormatErrors c13 (no version) | c14 | StringIndexMessages java15 | java21 | notJava8 (no version) |
|---|---|---|---|---|---|
| eager messages (bin30) | 342 (17: 27, 8: 3.8) | - | 9.8 | 5.2 | 1 |
| eager + --slice-formula | 59 | - | - | - | - |
| constant messages (experiment) | 4.3 (Java 17) | - | - | - | - |
| lazy, one message per case | 3.7 | - | ~255 | ~5 | 200-430 |
| lazy, one concatenation for all kinds | 3.2 | - | 111-301 | 8.8 | 8.6-32 |
| lazy, separate return per case | 3.1 | - | 24 | - | 174 |
| lazy, return per kind, pieces per version (chosen) | 2.8 | 3.1 | 56 | 6.6 | 87 |
| chosen + eager charAt | 54 | 44 | 11 | - | - |

The cost of eager messages: every index check that symex cannot decide
converts its numbers to strings in the string solver, even where no
program asks for the message. The cost of lazy messages: where a catch
merges exceptions of several kinds or versions, the message is built of
symbolic numbers and kinds; the more alternative concatenations, the
slower. charAt on Java 9-17 (either message, by CompactStrings) is such a
merge, which is why java9..17 are slower than java18+.

Parse messages (strings-smoke-tests/java_parseint_unknown_radix, 3 GB):
bin29 264 s, eager with radix suffix 664 s, without suffix 326 s, lazy
345 s (the test varied from 46 s to 685 s in earlier runs).

Follow-up: item #2 of the plan (string solver on strings of symbolic
integers) should make both cheaper.
