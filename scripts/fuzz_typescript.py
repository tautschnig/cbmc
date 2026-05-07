#!/usr/bin/env python3
"""
Fuzzer for the CBMC TypeScript frontend.

Strategy:
1. Generate random small TypeScript programs using a grammar-based
   approach (each production biased toward well-typed outputs).
2. Type-check via tsc as an oracle: if tsc reports errors, skip the
   program (we're not trying to fuzz tsc, we're fuzzing cbmc).
3. Run cbmc on the type-checked program under ulimit (memory and CPU
   bounds) and timeout.
4. Record crashes (SIGABRT, SIGSEGV) and hangs (timeout) separately
   from expected behavior (VERIFICATION SUCCESSFUL, VERIFICATION
   FAILED, assertion errors from user code).
5. Print a summary and save minimal reproducers.

Why this design:
- tsc as oracle: ensures we exercise code that's actually valid TS,
  so any cbmc crash is on legitimate input.
- Bounded programs: keeps individual runs under a few seconds.
- Resource bounds: prevents runaway memory/CPU via ulimit.
"""

import argparse
import hashlib
import os
import random
import subprocess
import sys
import tempfile
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_CBMC = REPO_ROOT / "build" / "bin" / "cbmc"

# Simple grammar fragments that generate valid TypeScript.
NUM_LITERALS = ["0", "1", "-1", "42", "100", "3.14", "0.5"]
STRING_LITERALS = ['"a"', '"hello"', '"x"', '""']
BOOL_LITERALS = ["true", "false"]

BINARY_OPS_NUM = ["+", "-", "*"]  # skip / to avoid div-by-zero noise
BINARY_OPS_NUM_CMP = ["===", "!==", "<", "<=", ">", ">="]
BINARY_OPS_BOOL = ["&&", "||"]


class Generator:
    def __init__(self, seed: int, depth: int = 3):
        self.rng = random.Random(seed)
        self.max_depth = depth
        self.var_counter = 0

    def new_var(self) -> str:
        self.var_counter += 1
        return f"v{self.var_counter}"

    def gen_num_expr(self, depth: int) -> str:
        if depth <= 0 or self.rng.random() < 0.4:
            return self.rng.choice(NUM_LITERALS)
        op = self.rng.choice(BINARY_OPS_NUM)
        return f"({self.gen_num_expr(depth - 1)} {op} {self.gen_num_expr(depth - 1)})"

    def gen_bool_expr(self, depth: int) -> str:
        if depth <= 0 or self.rng.random() < 0.3:
            return self.rng.choice(BOOL_LITERALS)
        if self.rng.random() < 0.5:
            op = self.rng.choice(BINARY_OPS_NUM_CMP)
            return f"({self.gen_num_expr(depth - 1)} {op} {self.gen_num_expr(depth - 1)})"
        op = self.rng.choice(BINARY_OPS_BOOL)
        return f"({self.gen_bool_expr(depth - 1)} {op} {self.gen_bool_expr(depth - 1)})"

    def gen_string_expr(self, depth: int) -> str:
        if depth <= 0 or self.rng.random() < 0.5:
            return self.rng.choice(STRING_LITERALS)
        return f"({self.gen_string_expr(depth - 1)} + {self.gen_string_expr(depth - 1)})"

    def gen_declaration(self) -> str:
        t = self.rng.choice(["number", "string", "boolean"])
        name = self.new_var()
        if t == "number":
            expr = self.gen_num_expr(self.max_depth)
        elif t == "string":
            expr = self.gen_string_expr(self.max_depth)
        else:
            expr = self.gen_bool_expr(self.max_depth)
        return f"const {name}: {t} = {expr};"

    def gen_assertion(self) -> str:
        # Generate a tautological assertion that should always hold.
        # (We use concrete equality on identical expressions.)
        choice = self.rng.randint(0, 2)
        if choice == 0:
            expr = self.gen_num_expr(self.max_depth - 1)
            return f"console.assert({expr} === {expr});"
        if choice == 1:
            expr = self.gen_bool_expr(self.max_depth - 1)
            return f"console.assert({expr} === {expr});"
        expr = self.gen_string_expr(self.max_depth - 1)
        return f"console.assert({expr} === {expr});"

    def gen_program(self, n_stmts: int = 5) -> str:
        stmts = []
        # Optionally emit a function declaration at the top.
        if self.rng.random() < 0.3:
            stmts.append(self.gen_function_decl())
        # Optionally emit a class declaration.
        if self.rng.random() < 0.2:
            stmts.append(self.gen_class_decl())
        for _ in range(n_stmts):
            r = self.rng.random()
            if r < 0.25:
                stmts.append(self.gen_declaration())
            elif r < 0.45:
                stmts.append(self.gen_array_stmt())
            elif r < 0.60:
                stmts.append(self.gen_if_stmt())
            elif r < 0.75:
                stmts.append(self.gen_for_stmt())
            else:
                stmts.append(self.gen_assertion())
        return "\n".join(stmts) + "\n"

    def gen_function_decl(self) -> str:
        name = self.new_var()
        t = self.rng.choice(["number", "string", "boolean"])
        if t == "number":
            body_expr = self.gen_num_expr(self.max_depth - 1)
        elif t == "string":
            body_expr = self.gen_string_expr(self.max_depth - 1)
        else:
            body_expr = self.gen_bool_expr(self.max_depth - 1)
        return f"function {name}(): {t} {{ return {body_expr}; }}"

    def gen_class_decl(self) -> str:
        name = "C" + str(self.var_counter)
        self.var_counter += 1
        # Simple class with a number field and a getter
        return (
            f"class {name} {{ x: number; constructor(v: number) {{ this.x = v; }} "
            f"get(): number {{ return this.x; }} }}"
        )

    def gen_array_stmt(self) -> str:
        name = self.new_var()
        elts = [self.gen_num_expr(self.max_depth - 1) for _ in range(self.rng.randint(2, 5))]
        return f"const {name}: number[] = [{', '.join(elts)}];"

    def gen_if_stmt(self) -> str:
        cond = self.gen_bool_expr(self.max_depth - 1)
        # Simple if with an assertion in the body
        inner = self.gen_assertion()
        return f"if ({cond}) {{ {inner} }}"

    def gen_for_stmt(self) -> str:
        # Bounded for loop with an assertion in the body
        n = self.rng.randint(2, 5)
        inner = self.gen_assertion()
        return f"for (let _i: number = 0; _i < {n}; _i++) {{ {inner} }}"


class Oracle:
    """Uses tsc to determine if a program type-checks."""

    def __init__(self, tsc_path: str = "tsc"):
        self.tsc = tsc_path

    def type_checks(self, path: Path) -> bool:
        try:
            result = subprocess.run(
                [
                    self.tsc,
                    "--noEmit",
                    "--strict",
                    "--target",
                    "ES2022",
                    str(path),
                ],
                capture_output=True,
                timeout=30,
                text=True,
            )
            return result.returncode == 0
        except subprocess.TimeoutExpired:
            return False


class Runner:
    """Runs cbmc on a program under resource bounds."""

    def __init__(self, cbmc_path: Path, timeout_s: int = 30, mem_kb: int = 4_000_000):
        self.cbmc = cbmc_path
        self.timeout_s = timeout_s
        self.mem_kb = mem_kb

    def run(self, path: Path) -> dict:
        """Returns {status, exit_code, stdout_tail, stderr_tail}."""
        # --no-unwinding-assertions + --unwind 10: avoids noise from the
        # float-loop-counter limitation (documented in capability matrix).
        # CBMC needs an explicit loop bound when the loop counter is a
        # float — the default behavior tries to prove termination via
        # the unwinding assertion, which float arithmetic doesn't
        # satisfy cleanly.
        cmd = (
            f"ulimit -v {self.mem_kb} -t {self.timeout_s + 10}; "
            f"{self.cbmc} --no-unwinding-assertions --unwind 10 {path}"
        )
        try:
            t0 = time.monotonic()
            result = subprocess.run(
                ["bash", "-c", cmd],
                capture_output=True,
                timeout=self.timeout_s,
                text=True,
            )
            elapsed = time.monotonic() - t0
            return {
                "status": self._classify(result.returncode, result.stdout),
                "exit_code": result.returncode,
                "elapsed_s": elapsed,
                "stdout_tail": "\n".join(result.stdout.splitlines()[-20:]),
                "stderr_tail": "\n".join(result.stderr.splitlines()[-20:]),
            }
        except subprocess.TimeoutExpired:
            return {
                "status": "TIMEOUT",
                "exit_code": None,
                "elapsed_s": self.timeout_s,
                "stdout_tail": "",
                "stderr_tail": "",
            }

    def _classify(self, code: int, stdout: str) -> str:
        if "VERIFICATION SUCCESSFUL" in stdout:
            return "OK"
        if "VERIFICATION FAILED" in stdout:
            return "ASSERTION_FAILED"
        if code == -11 or code == 139:
            return "SEGFAULT"
        if code == -6 or code == 134:
            return "ABORT"
        if code == 64 or "PARSING ERROR" in stdout:
            return "PARSE_ERROR"
        if code == 0:
            return "OK"
        return f"UNKNOWN({code})"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--iterations", type=int, default=20, help="number of fuzz iterations"
    )
    parser.add_argument("--seed", type=int, default=None, help="base seed")
    parser.add_argument(
        "--cbmc", default=str(DEFAULT_CBMC), help="path to cbmc binary"
    )
    parser.add_argument(
        "--tsc", default="tsc", help="path to tsc binary (oracle)"
    )
    parser.add_argument("--timeout", type=int, default=30, help="cbmc timeout (s)")
    parser.add_argument(
        "--mem-kb", type=int, default=4_000_000, help="ulimit virtual memory (KB)"
    )
    parser.add_argument(
        "--depth", type=int, default=3, help="expression nesting depth"
    )
    parser.add_argument(
        "--stmts", type=int, default=5, help="statements per program"
    )
    parser.add_argument(
        "--out-dir",
        default=str(REPO_ROOT / "fuzz-results"),
        help="directory for crash reproducers",
    )
    parser.add_argument(
        "--verbose", action="store_true", help="print each program as it runs"
    )
    args = parser.parse_args()

    if not Path(args.cbmc).exists():
        print(f"ERROR: cbmc not found at {args.cbmc}", file=sys.stderr)
        sys.exit(1)

    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    oracle = Oracle(args.tsc)
    runner = Runner(Path(args.cbmc), args.timeout, args.mem_kb)

    base_seed = args.seed if args.seed is not None else random.randint(0, 10_000_000)

    stats = {
        "total": 0,
        "not_type_checking": 0,
        "ok": 0,
        "assertion_failed": 0,
        "segfault": 0,
        "abort": 0,
        "timeout": 0,
        "parse_error": 0,
        "unknown": 0,
    }

    print(f"Fuzzer: {args.iterations} iterations, base seed {base_seed}")
    print(f"CBMC: {args.cbmc}")
    print(f"tsc:  {args.tsc}")
    print(f"Timeout: {args.timeout}s, mem limit: {args.mem_kb}KB")
    print()

    for i in range(args.iterations):
        seed = base_seed + i
        stats["total"] += 1
        gen = Generator(seed, args.depth)
        program = gen.gen_program(args.stmts)

        with tempfile.NamedTemporaryFile(
            mode="w", suffix=".ts", delete=False
        ) as f:
            f.write(program)
            ts_path = Path(f.name)

        try:
            if not oracle.type_checks(ts_path):
                stats["not_type_checking"] += 1
                if args.verbose:
                    print(f"[{i:4d}] seed={seed} SKIP (tsc rejected)")
                continue

            result = runner.run(ts_path)
            status = result["status"]

            if status == "OK":
                stats["ok"] += 1
            elif status == "ASSERTION_FAILED":
                stats["assertion_failed"] += 1
            elif status == "SEGFAULT":
                stats["segfault"] += 1
            elif status == "ABORT":
                stats["abort"] += 1
            elif status == "TIMEOUT":
                stats["timeout"] += 1
            elif status == "PARSE_ERROR":
                stats["parse_error"] += 1
            else:
                stats["unknown"] += 1

            if status in {"SEGFAULT", "ABORT", "TIMEOUT", "PARSE_ERROR", "UNKNOWN"} or "UNKNOWN" in status:
                digest = hashlib.sha1(program.encode()).hexdigest()[:8]
                crash_path = out_dir / f"crash_{status}_{digest}_seed{seed}.ts"
                crash_path.write_text(program)
                log_path = crash_path.with_suffix(".log")
                log_path.write_text(
                    f"seed={seed}\nstatus={status}\nexit_code={result['exit_code']}\n"
                    f"elapsed_s={result['elapsed_s']}\n\n"
                    f"--- stdout (tail) ---\n{result['stdout_tail']}\n\n"
                    f"--- stderr (tail) ---\n{result['stderr_tail']}\n"
                )

            if args.verbose or status not in {"OK", "ASSERTION_FAILED"}:
                elapsed = result.get("elapsed_s", 0)
                print(f"[{i:4d}] seed={seed:>10} {status:>20} {elapsed:.2f}s")
        finally:
            ts_path.unlink(missing_ok=True)

    print()
    print("=" * 60)
    print("Summary")
    print("=" * 60)
    for key, val in stats.items():
        print(f"  {key:30} {val:>6}")
    print()
    crashes = stats["segfault"] + stats["abort"] + stats["timeout"] + stats["parse_error"] + stats["unknown"]
    if crashes > 0:
        print(f"Crashes/hangs: {crashes}. Reproducers in {out_dir}/")
        sys.exit(1)
    print("No crashes.")


if __name__ == "__main__":
    main()
