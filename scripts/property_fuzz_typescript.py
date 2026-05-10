#!/usr/bin/env python3
"""
Property-based fuzzer for the CBMC TypeScript frontend.

Unlike fuzz_typescript.py which generates tautological assertions
(x === x) and checks that CBMC doesn't crash, this fuzzer generates
programs that encode specific SEMANTIC invariants the ES2024 spec
requires and CBMC should be able to verify.

Examples of invariants:
- JSON.parse(JSON.stringify(x)) === x for primitives
- (a + b).length === a.length + b.length for strings
- Array.concat preserves all elements
- Object.keys.length === Object.values.length
- Math.abs(x) >= 0
- Math.max(a, b) >= a && Math.max(a, b) >= b
- x.repeat(n).length === x.length * n (for small n)
- Commutativity: a + b === b + a for numbers

When verification FAILS, it means our frontend is unsound for that
invariant — a real bug. When it SUCCEEDS, the frontend correctly
models the operation.

Usage:
    python3 scripts/property_fuzz_typescript.py --iterations 500
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


# Each property is a tuple:
#   (name, template) where template takes an rng and returns a TS program.
# Programs use __CPROVER_assume for bounded inputs and console.assert
# for the property; verification succeeds ⇔ property holds for all
# admissible inputs.

PROPERTIES = []


def register(name):
    def deco(f):
        PROPERTIES.append((name, f))
        return f
    return deco


@register("number-add-commutative")
def p_add_commutative(rng):
    return """
const a: number = nondet_number();
const b: number = nondet_number();
__CPROVER_assume(a >= -100 && a <= 100 && !Number.isNaN(a));
__CPROVER_assume(b >= -100 && b <= 100 && !Number.isNaN(b));
console.assert(a + b === b + a);
"""


@register("number-mult-commutative")
def p_mult_commutative(rng):
    return """
const a: number = nondet_number();
const b: number = nondet_number();
__CPROVER_assume(a >= -10 && a <= 10 && !Number.isNaN(a));
__CPROVER_assume(b >= -10 && b <= 10 && !Number.isNaN(b));
console.assert(a * b === b * a);
"""


@register("math-abs-nonneg")
def p_math_abs(rng):
    return """
const x: number = nondet_number();
__CPROVER_assume(x >= -1000 && x <= 1000);
console.assert(Math.abs(x) >= 0);
"""


@register("math-max-ge-both")
def p_math_max(rng):
    return """
const a: number = nondet_number();
const b: number = nondet_number();
__CPROVER_assume(a >= -100 && a <= 100);
__CPROVER_assume(b >= -100 && b <= 100);
const m = Math.max(a, b);
console.assert(m >= a);
console.assert(m >= b);
"""


@register("string-concat-length")
def p_concat_length(rng):
    return """
const a: string = "hello";
const b: string = "world";
const c: string = a + b;
console.assert(c.length === a.length + b.length);
"""


@register("string-repeat-length")
def p_repeat_length(rng):
    n = rng.randint(0, 5)
    return f"""
const s: string = "ab";
const r: string = s.repeat({n});
console.assert(r.length === 2 * {n});
"""


@register("string-symbolic-repeat-length")
def p_sym_repeat_length(rng):
    return """
const n: number = nondet_number();
__CPROVER_assume(n === 0 || n === 1 || n === 2 || n === 3);
const s: string = "x";
const r: string = s.repeat(n);
console.assert(r.length === n);
"""


@register("json-roundtrip-primitive-number")
def p_json_roundtrip_num(rng):
    return """
const x: number = 42;
const s: string = JSON.stringify(x);
const y: number = JSON.parse(s);
console.assert(y === x);
"""


@register("json-roundtrip-primitive-bool")
def p_json_roundtrip_bool(rng):
    return """
const x: boolean = true;
const s: string = JSON.stringify(x);
const y: boolean = JSON.parse(s);
console.assert(y === x);
"""


@register("json-roundtrip-flat-object")
def p_json_roundtrip_obj(rng):
    return """
interface O { a: number; b: number; }
const x: O = { a: 10, b: 20 };
const s: string = JSON.stringify(x);
const y: O = JSON.parse(s);
console.assert(y.a === 10);
console.assert(y.b === 20);
"""


@register("array-concat-preserves-length")
def p_concat_preserves(rng):
    return """
const a: number[] = [1, 2, 3];
const b: number[] = [4, 5];
const c: number[] = a.concat(b);
console.assert(c.length === a.length + b.length);
"""


@register("array-map-preserves-length")
def p_map_preserves(rng):
    return """
const a: number[] = [1, 2, 3, 4];
const b: number[] = a.map((x: number) => x * 2);
console.assert(b.length === a.length);
"""


@register("array-reduce-sum")
def p_reduce_sum(rng):
    return """
const a: number[] = [1, 2, 3, 4];
const s: number = a.reduce((acc: number, x: number) => acc + x, 0);
console.assert(s === 10);
"""


@register("object-keys-values-same-length")
def p_keys_values(rng):
    return """
const o = { a: 1, b: 2, c: 3 };
const ks: string[] = Object.keys(o);
const vs: number[] = Object.values(o);
console.assert(ks.length === vs.length);
console.assert(ks.length === 3);
"""


@register("boolean-xor-identity")
def p_bool_xor(rng):
    return """
const a: boolean = nondet_boolean();
const b: boolean = nondet_boolean();
// (a !== b) === ((a || b) && !(a && b))
console.assert((a !== b) === ((a || b) && !(a && b)));
"""


@register("optional-chain-default")
def p_opt_chain(rng):
    return """
const o: { x?: number } = { x: 5 };
const a: number = o?.x ?? 99;
console.assert(a === 5);

const o2: { x?: number } = {};
const b: number = o2?.x ?? 99;
console.assert(b === 99);
"""


@register("object-is-nan-equals")
def p_obj_is_nan(rng):
    return """
// Object.is(NaN, NaN) must be true (our model matches this).
// Note: NaN === NaN is technically false per ES2024, but our model
// represents NaN / null / undefined all as the same IEEE NaN, so
// NaN === NaN evaluates to true. This is a documented limitation
// of the null-as-NaN sentinel model; the property check here only
// verifies the Object.is behaviour.
console.assert(Object.is(NaN, NaN));
"""


@register("array-includes-matches-indexOf")
def p_includes_indexof(rng):
    return """
const a: number[] = [1, 2, 3, 4];
console.assert(a.includes(3) === (a.indexOf(3) >= 0));
console.assert(a.includes(99) === (a.indexOf(99) >= 0));
"""


@register("string-slice-nonneg-length")
def p_slice_nonneg(rng):
    return """
const s: string = "hello";
const t: string = s.slice(1, 4);
console.assert(t.length >= 0);
console.assert(t.length <= s.length);
"""


@register("array-reverse-preserves-length")
def p_reverse_len(rng):
    return """
const a: number[] = [1, 2, 3, 4, 5];
const b: number[] = a.reverse();
console.assert(b.length === 5);
"""


class Runner:
    def __init__(self, cbmc_path: Path, timeout_s: int = 30,
                 mem_kb: int = 4_000_000):
        self.cbmc = cbmc_path
        self.timeout_s = timeout_s
        self.mem_kb = mem_kb

    def run(self, path: Path) -> dict:
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
                "elapsed_s": elapsed,
                "stdout_tail": "\n".join(result.stdout.splitlines()[-20:]),
                "stderr_tail": "\n".join(result.stderr.splitlines()[-20:]),
            }
        except subprocess.TimeoutExpired:
            return {
                "status": "TIMEOUT",
                "elapsed_s": self.timeout_s,
                "stdout_tail": "",
                "stderr_tail": "",
            }

    def _classify(self, code: int, stdout: str) -> str:
        if "VERIFICATION SUCCESSFUL" in stdout:
            return "OK"
        if "VERIFICATION FAILED" in stdout:
            return "PROPERTY_VIOLATED"
        if code in (-11, 139):
            return "SEGFAULT"
        if code in (-6, 134):
            return "ABORT"
        if code == 64 or "PARSING ERROR" in stdout:
            return "PARSE_ERROR"
        if code == 0:
            return "OK"
        return f"UNKNOWN({code})"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--iterations", type=int, default=200,
        help="total iterations (spread across properties)"
    )
    parser.add_argument("--seed", type=int, default=None)
    parser.add_argument("--cbmc", default=str(DEFAULT_CBMC))
    parser.add_argument("--timeout", type=int, default=30)
    parser.add_argument("--mem-kb", type=int, default=4_000_000)
    parser.add_argument(
        "--out-dir",
        default=str(REPO_ROOT / "fuzz-results-property"),
    )
    parser.add_argument("--verbose", action="store_true")
    parser.add_argument(
        "--property", default=None,
        help="run only one named property"
    )
    args = parser.parse_args()

    if not Path(args.cbmc).exists():
        print(f"ERROR: cbmc not found at {args.cbmc}", file=sys.stderr)
        sys.exit(1)

    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    runner = Runner(Path(args.cbmc), args.timeout, args.mem_kb)
    base_seed = args.seed if args.seed is not None else random.randint(
        0, 10_000_000)

    active_properties = PROPERTIES
    if args.property:
        active_properties = [p for p in PROPERTIES if p[0] == args.property]
        if not active_properties:
            names = [p[0] for p in PROPERTIES]
            print(f"ERROR: property '{args.property}' not found. "
                  f"Available: {', '.join(names)}", file=sys.stderr)
            sys.exit(1)

    stats = {
        "total": 0, "ok": 0, "violated": 0, "timeout": 0,
        "crashes": 0, "parse_error": 0, "unknown": 0,
    }
    violations = {}  # property name → count

    print(f"Property-based fuzzer: {args.iterations} iterations, "
          f"base seed {base_seed}")
    print(f"Active properties: {len(active_properties)}")
    print(f"Timeout: {args.timeout}s, mem limit: {args.mem_kb}KB")
    print()

    for i in range(args.iterations):
        if i > 0 and i % 20 == 0:
            print(
                f"[progress] {i}/{args.iterations}  "
                f"ok={stats['ok']}  violated={stats['violated']}  "
                f"timeouts={stats['timeout']}  crashes={stats['crashes']}",
                flush=True,
            )
        seed = base_seed + i
        rng = random.Random(seed)
        name, template = rng.choice(active_properties)
        program = template(rng)
        stats["total"] += 1

        with tempfile.NamedTemporaryFile(
            mode="w", suffix=".ts", delete=False
        ) as f:
            f.write(program)
            ts_path = Path(f.name)

        try:
            result = runner.run(ts_path)
            status = result["status"]

            if status == "OK":
                stats["ok"] += 1
            elif status == "PROPERTY_VIOLATED":
                stats["violated"] += 1
                violations[name] = violations.get(name, 0) + 1
                # Save reproducer
                digest = hashlib.sha1(program.encode()).hexdigest()[:8]
                rp = out_dir / f"violated_{name}_{digest}_seed{seed}.ts"
                rp.write_text(program)
                lp = rp.with_suffix(".log")
                lp.write_text(
                    f"seed={seed}\nproperty={name}\nstatus={status}\n\n"
                    f"--- stdout ---\n{result['stdout_tail']}\n\n"
                    f"--- stderr ---\n{result['stderr_tail']}\n"
                )
            elif status == "TIMEOUT":
                stats["timeout"] += 1
            elif status in ("SEGFAULT", "ABORT"):
                stats["crashes"] += 1
            elif status == "PARSE_ERROR":
                stats["parse_error"] += 1
            else:
                stats["unknown"] += 1

            if args.verbose or status != "OK":
                elapsed = result.get("elapsed_s", 0)
                print(f"[{i:4d}] seed={seed:>10} {name:>35} "
                      f"{status:>18} {elapsed:.2f}s")
        finally:
            ts_path.unlink(missing_ok=True)

    print()
    print("=" * 60)
    print("Summary")
    print("=" * 60)
    for key, val in stats.items():
        print(f"  {key:20} {val:>6}")
    print()
    if violations:
        print("Property violations (frontend may be unsound):")
        for name, count in sorted(violations.items(),
                                  key=lambda kv: -kv[1]):
            print(f"  {name}: {count}")
        print(f"\nReproducers in {out_dir}/")
        sys.exit(1)
    print("All properties hold.")


if __name__ == "__main__":
    main()
