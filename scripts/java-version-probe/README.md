# Java versions of the JBMC models: probe and check

Tools that compare the JDKs of Java 8 to 25 with each other and with JBMC
on the public methods that the models of `jbmc/lib/java-models-library`
define (see section 11 of `doc/string-solver-plan.md`).

1. `api.py JAVAP_OUTPUT > api.txt` lists the methods, from
   `javap -public -cp core-models.jar CLASS...` (java.math, reflection,
   Thread, Class, Object and the CProver classes left out).
2. `run-all.sh api.txt CASES OUT_DIR [CLASS_PREFIX]` runs `Probe.java` on
   each JDK of `JDKS` (`VERSION=JAVA_HOME` pairs): it calls each method on
   `CASES` generated cases (the same on every JDK) and writes one line per
   case with the arguments, the result (doubles as bits, exceptions as class
   and message) and the arguments as Java expressions.
3. `compare.py OUT_DIR --json diff.json [--agreeing N]` lists the cases
   whose results differ between versions, with the versions of each result,
   and with `--agreeing` also up to N cases per method where they agree.
4. `check_jbmc.py diff.json OUT_DIR/25.txt CHECK_DIR --api api.txt
   [--agreeing]` writes a harness per case that asserts, for each JDK's
   result, that the call did not give it, and runs JBMC on it
   (`--throw-runtime-exceptions --unwind 40 --unwinding-assertions`; `JAVAC`
   names a javac that compiles for release 8 and later). A
   result is "excluded" when JBMC proves its assertion: a false proof for
   any version that may run the program. Each harness also reports whether
   JBMC lets the call throw, return, throw the JDK's class, and give a null
   message, and whether it reaches the code after the call at all
   ("unreachable": every behaviour excluded).
5. `triage.py CHECK_DIR/results.json` groups the cases by method;
   `rerun.py CHECK_DIR JBMC [REGEX]` reruns the harnesses that excluded a
   result with another JBMC.

Example (about 20 minutes for 200 cases per method on 18 JDKs, and an hour
for the check):

    JDKS="8=/path/jdk8 9=/path/jdk9 ... 25=/path/jdk25" \
      ./run-all.sh api.txt 200 out
    ./compare.py out --json out/diff.json --agreeing 3
    ./check_jbmc.py out/diff.json out/25.txt chk --api api.txt

`string-index/` measures the exceptions of String's index methods and
generates a JBMC regression test from them (see its README).

`string-builder/` generates the models of StringBuilder and StringBuffer
from one template (see its README).
