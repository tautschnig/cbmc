# Differential testing of the inline-assembly semantics

These scripts generate the `regression/cbmc/asm_{x86,aarch64,arm}` tests and
can be used to extend them. Each test case (in `asm_differential_cases.py`) is
a small C function with one inline assembly statement. The harness compiles
and runs it on several input vectors, on real hardware for x86-64 and with the
[Unicorn](https://www.unicorn-engine.org/) emulator for AArch64 and AArch32,
then writes a CBMC regression test whose assertions pin the observed results.

If CBMC's semantics for any instruction were wrong, or if a statement were
approximated (its outputs havocked) rather than interpreted, the generated
assertions would no longer hold, so running the test under CBMC checks the
semantics against the real machine.

## Requirements

* `gcc` (x86-64 cases, run natively);
* `aarch64-linux-gnu-gcc` (AArch64 cases);
* `clang` targeting `armv7a-none-eabi` (AArch32 cases);
* Python packages `unicorn` and `pyelftools` for the emulated architectures.

## Use

```sh
python3 -m venv .venv && .venv/bin/pip install unicorn pyelftools
scripts/asm_differential/asm_differential.py x86_64  regression/cbmc/asm_x86
scripts/asm_differential/asm_differential.py aarch64 regression/cbmc/asm_aarch64
scripts/asm_differential/asm_differential.py arm     regression/cbmc/asm_arm
```

Set `ASM_DIFF_MAX_VECTORS` to cap the number of input vectors per case (the
committed tests use 3). The output is passed through `clang-format` when it is
available, so regenerating in a checkout reproduces the committed files.

## Adding a case

Add a `Case` to the relevant list in `asm_differential_cases.py`: the
statement, the C variables it reads and writes (in the order the harness
stores them), and optionally fixed input vectors or a `constrain` callback for
inputs that must satisfy a precondition (for example a shift count below the
width). A case must be deterministic: it must not read uninitialised storage,
a timer, or I/O.
