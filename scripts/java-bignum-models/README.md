# Tooling of the java.math models of JBMC

The models of `BigInteger`, `BigDecimal`, `MathContext` and `RoundingMode`
are in `jbmc/lib/java-models-library/src/main/java/java/math`; see the
section on them in `doc/string-solver-plan.md` for their design and the
measurements that chose it.

- `run-difftest.sh JAVA_HOME [N]` compares the models with the `java.math`
  of a JDK on `N` random values per operation (results and exception
  messages). It ran with 0 mismatches on Java 8 to 25.
- `bench/` has the JBMC benchmarks of the design (`Bench.java`: the
  prototypes; `Ops.java`: operations of the model, concrete and symbolic).
  Compile them with the cprover-api jar on the class path and run JBMC with
  `--function Bench.NAME` (or `Ops.NAME`) and the models jar, or use
  `run-prototypes.sh PROTO_DIR...` and `run-ops.sh` from the directory of
  the compiled classes (`JBMC` names the binary). `results/` has their
  output as measured: `prototypes-1.txt` with `--unwind 45` before the
  loop handler of library methods, `prototypes-2.txt` and `-3.txt` with
  it (`-3.txt`, "empty", the model of the models jar, before String.format
  knew BigInteger), `ops-1.txt` the model before the loop handler,
  `ops-2.txt` after it and `ops-3.txt` the symbolic cases at the end with
  a time limit of 120 s.
- `prototypes/` has the four representations that were measured: P1 a
  long, P2 128-bit two's complement, P3 exact limbs of any number, P4 a
  fixed number of limbs (the one the model uses).
