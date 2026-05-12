#!/usr/bin/env python3
"""
Random-program fuzzer for the CBMC Python front-end.

Generates small Python programs (within a bounded grammar)
and runs `cbmc program.py` on each, flagging:

- Segmentation faults / crashes.
- Invariant violations (backtrace prints).
- Solver errors (dec_solve index-set-empty, etc.).
- Timeouts beyond the configured budget.

Not a correctness tool — the generated programs don't carry
oracle assertions. This is a stability / crash-finding
harness.

Usage:
    scripts/python_fuzzer.py                    # default 100 runs
    scripts/python_fuzzer.py --count 500
    scripts/python_fuzzer.py --seed 42
    scripts/python_fuzzer.py --cbmc /path/to/cbmc --timeout 30
"""

import argparse
import os
import random
import subprocess
import sys
import tempfile
from pathlib import Path

# ---------------------------------------------------------
# Program generator
# ---------------------------------------------------------

NAMES = ["x", "y", "z", "a", "b", "c", "i", "j", "k", "n", "m"]
INT_LITS = ["0", "1", "2", "-1", "100", "-100"]
FLOAT_LITS = ["0.0", "1.5", "-2.5", "3.14"]
STR_LITS = ['""', '"hello"', '"a"', '"long string"']
BIN_OPS = ["+", "-", "*", "//", "%"]
CMP_OPS = ["==", "!=", "<", "<=", ">", ">="]


class Gen:
    def __init__(self, rng: random.Random, max_depth: int = 4):
        self.rng = rng
        self.max_depth = max_depth
        self.vars: list[str] = []
        self.str_vars: list[str] = []
        self.list_vars: list[str] = []
        self.dict_vars: list[str] = []

    def fresh_name(self) -> str:
        n = self.rng.choice(NAMES)
        if n not in self.vars:
            self.vars.append(n)
        return n

    def expr(self, depth: int = 0) -> str:
        if depth >= self.max_depth or not self.vars:
            return self._leaf()
        choice = self.rng.randint(0, 7)
        if choice == 0:
            return self._leaf()
        if choice == 1:
            return f"({self.expr(depth + 1)} {self.rng.choice(BIN_OPS)} "\
                   f"{self.expr(depth + 1)})"
        if choice == 2:
            return f"({self.expr(depth + 1)} {self.rng.choice(CMP_OPS)} "\
                   f"{self.expr(depth + 1)})"
        if choice == 3:
            return f"abs({self.expr(depth + 1)})"
        if choice == 4:
            # list literal
            n = self.rng.randint(0, 3)
            elems = ", ".join(self.expr(depth + 1) for _ in range(n))
            return f"[{elems}]"
        if choice == 5 and self.list_vars:
            # list index
            lv = self.rng.choice(self.list_vars)
            return f"{lv}[0]"
        if choice == 6:
            # string literal usage
            return self.rng.choice(STR_LITS)
        return f"({self.expr(depth + 1)} if "\
               f"{self.expr(depth + 1)} else {self.expr(depth + 1)})"

    def _leaf(self) -> str:
        if self.vars and self.rng.random() < 0.5:
            return self.rng.choice(self.vars)
        return self.rng.choice(INT_LITS)

    def stmt(self, depth: int = 0) -> list[str]:
        if depth >= self.max_depth:
            return self._simple_stmt()
        choice = self.rng.randint(0, 8)
        if choice <= 3:
            return self._simple_stmt()
        if choice == 4:
            return self._if_stmt(depth)
        if choice == 5:
            return self._while_stmt(depth)
        if choice == 6:
            return self._try_stmt(depth)
        if choice == 7:
            return self._list_stmt()
        return self._simple_stmt()

    def _list_stmt(self) -> list[str]:
        n = self.fresh_name()
        self.list_vars.append(n)
        count = self.rng.randint(0, 3)
        items = ", ".join(self.rng.choice(INT_LITS) for _ in range(count))
        return [f"{n} = [{items}]"]

    def _try_stmt(self, depth: int) -> list[str]:
        body = self._block(depth + 1)
        lines = ["try:"]
        for s in body:
            lines.append("    " + s)
        lines.append("except Exception:")
        lines.append("    pass")
        if self.rng.random() < 0.3:
            lines.append("finally:")
            lines.append("    pass")
        return lines

    def _simple_stmt(self) -> list[str]:
        choice = self.rng.randint(0, 4)
        if choice == 0:
            n = self.fresh_name()
            return [f"{n} = {self.expr()}"]
        if choice == 1 and self.vars:
            n = self.rng.choice(self.vars)
            return [f"{n} = {self.expr()}"]
        if choice == 2 and self.vars:
            return [f"assert {self.expr()} == {self.expr()}"]
        if choice == 3 and self.vars:
            return [f"assert {self.expr()}"]
        return [f"x = {self.rng.choice(INT_LITS)}"]

    def _if_stmt(self, depth: int) -> list[str]:
        cond = self.expr(depth + 1)
        body = self._block(depth + 1)
        lines = [f"if {cond}:"]
        for s in body:
            lines.append("    " + s)
        if self.rng.random() < 0.4:
            lines.append("else:")
            for s in self._block(depth + 1):
                lines.append("    " + s)
        return lines

    def _while_stmt(self, depth: int) -> list[str]:
        ctr = self.fresh_name()
        body = self._block(depth + 1)
        lines = [f"{ctr} = 0", f"while {ctr} < 3:"]
        for s in body:
            lines.append("    " + s)
        lines.append(f"    {ctr} = {ctr} + 1")
        return lines

    def _block(self, depth: int, count: int = 2) -> list[str]:
        out: list[str] = []
        for _ in range(count):
            out.extend(self.stmt(depth))
        if not out:
            out = ["x = 0"]
        return out

    def program(self) -> str:
        lines: list[str] = []
        # Seed a few names so exprs have variables.
        for _ in range(self.rng.randint(1, 3)):
            n = self.fresh_name()
            lines.append(f"{n} = {self.rng.choice(INT_LITS)}")
        for _ in range(self.rng.randint(3, 6)):
            lines.extend(self.stmt())
        return "\n".join(lines) + "\n"


# ---------------------------------------------------------
# Runner
# ---------------------------------------------------------


def run_once(cbmc: str, code: str, timeout: int) -> tuple[str, str, int]:
    """Run cbmc on the generated program. Returns
    (verdict, detail, runtime_seconds)."""
    import time
    with tempfile.NamedTemporaryFile(
        suffix=".py", mode="w", delete=False
    ) as tf:
        tf.write(code)
        path = tf.name
    start = time.monotonic()
    try:
        result = subprocess.run(
            [cbmc, path, "--no-unwinding-assertions", "--unwind", "3"],
            capture_output=True,
            text=True,
            timeout=timeout,
        )
        elapsed = int(time.monotonic() - start)
        combined = result.stdout + "\n" + result.stderr
        if "Invariant check failed" in combined:
            return ("INVARIANT", combined, elapsed)
        if result.returncode == 139 or "Segmentation fault" in combined:
            return ("SEGV", combined, elapsed)
        if "dec_solve: current index set is empty" in combined:
            return ("SOLVER_ERROR", combined, elapsed)
        if "VERIFICATION ERROR" in combined:
            return ("VERIFIC_ERR", combined, elapsed)
        return ("OK", "", elapsed)
    except subprocess.TimeoutExpired:
        elapsed = timeout
        return ("TIMEOUT", f"exceeded {timeout}s", elapsed)
    finally:
        try:
            os.unlink(path)
        except FileNotFoundError:
            pass


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--count", type=int, default=100)
    ap.add_argument("--seed", type=int, default=None)
    ap.add_argument("--cbmc", default=None,
                    help="path to cbmc (default: ../build/bin/cbmc)")
    ap.add_argument("--timeout", type=int, default=20)
    ap.add_argument("--max-depth", type=int, default=4)
    ap.add_argument("--save-failures", default=None,
                    help="directory to save failing inputs")
    args = ap.parse_args()

    if args.cbmc is None:
        here = Path(__file__).resolve().parent
        args.cbmc = str(here.parent / "build" / "bin" / "cbmc")
    if not Path(args.cbmc).exists():
        print(f"cbmc not found: {args.cbmc}", file=sys.stderr)
        return 1

    rng = random.Random(args.seed)
    counts: dict[str, int] = {
        "OK": 0,
        "INVARIANT": 0,
        "SEGV": 0,
        "SOLVER_ERROR": 0,
        "VERIFIC_ERR": 0,
        "TIMEOUT": 0,
    }
    failures: list[tuple[int, str, str, str]] = []

    for i in range(args.count):
        code = Gen(rng, args.max_depth).program()
        verdict, detail, _elapsed = run_once(args.cbmc, code, args.timeout)
        counts[verdict] = counts.get(verdict, 0) + 1
        if verdict not in ("OK", "TIMEOUT"):
            failures.append((i, verdict, code, detail))
            print(f"[{i:04d}] {verdict}")
        elif (i + 1) % 25 == 0:
            print(f"[{i:04d}] progress: {counts}")

    print()
    print("=== Fuzzer summary ===")
    for k, v in counts.items():
        if v:
            print(f"  {k}: {v}")

    if failures and args.save_failures:
        out = Path(args.save_failures)
        out.mkdir(parents=True, exist_ok=True)
        for i, verdict, code, detail in failures:
            (out / f"{i:04d}_{verdict}.py").write_text(code)
            (out / f"{i:04d}_{verdict}.log").write_text(detail)
        print(f"Saved {len(failures)} failures to {out}")

    return 0 if not failures else 2


if __name__ == "__main__":
    sys.exit(main())
