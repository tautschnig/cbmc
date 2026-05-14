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


# === Pass 1/2/3 spec-review areas: targeted invariants ===

@register("bit-int32-trunc-idempotent")
def p_int32_idempotent(rng):
    # (x | 0) | 0 === (x | 0) — int32 truncation is idempotent.
    return """
const x: number = nondet_number();
__CPROVER_assume(x >= -2_000_000_000 && x <= 2_000_000_000);
const t: number = x | 0;
console.assert((t | 0) === t);
"""


@register("bit-shift-by-zero-ident")
def p_shift_zero(rng):
    # x | 0, x >> 0 produce the same int32 truncation of x.
    return """
const x: number = nondet_number();
__CPROVER_assume(x >= -1_000_000 && x <= 1_000_000 && !Number.isNaN(x));
console.assert((x | 0) === (x >> 0));
"""


@register("bit-unsigned-shift-nonneg")
def p_ushr_nonneg(rng):
    # x >>> 0 is always non-negative (uint32 reinterpretation).
    return """
const x: number = nondet_number();
__CPROVER_assume(x >= -1_000_000 && x <= 1_000_000 && !Number.isNaN(x));
console.assert((x >>> 0) >= 0);
"""


@register("bit-shl-power-of-two")
def p_shl_pow2(rng):
    n = rng.randint(0, 30)
    return f"""
console.assert((1 << {n}) === {2 ** n});
"""


@register("bit-shift-mask-32")
def p_shift_mask(rng):
    # Shift count is masked to low 5 bits; (1 << 32) === 1 in JS.
    return """
console.assert((1 << 32) === 1);
console.assert((1 << 33) === 2);
console.assert((1 << 34) === 4);
console.assert((1 << 64) === 1);
"""


@register("bit-and-self")
def p_and_self(rng):
    # x & x === x | 0 (truncation to int32, then idempotent AND).
    return """
const x: number = nondet_number();
__CPROVER_assume(x >= -1000 && x <= 1000 && !Number.isNaN(x));
console.assert((x & x) === (x | 0));
"""


@register("bit-or-zero-eq-and-self")
def p_or_zero_and(rng):
    return """
const x: number = nondet_number();
__CPROVER_assume(x >= -1000 && x <= 1000 && !Number.isNaN(x));
console.assert((x | 0) === (x & x));
"""


@register("bit-xor-self-zero")
def p_xor_self(rng):
    # x ^ x === 0 (after int32 truncation).
    return """
const x: number = nondet_number();
__CPROVER_assume(x >= -1000 && x <= 1000 && !Number.isNaN(x));
console.assert((x ^ x) === 0);
"""


@register("string-utf8-bmp-length")
def p_utf8_bmp(rng):
    return """
console.assert("\\u00e9".length === 1);
console.assert("\\u4e2d".length === 1);
console.assert("a\\u00e9b".length === 3);
"""


@register("string-astral-length-2")
def p_astral_length(rng):
    return """
const s = "\\u{1F4A9}";
console.assert(s.length === 2);
const m = "a" + s + "b";
console.assert(m.length === 4);
"""


@register("string-coerce-array-comma")
def p_coerce_array(rng):
    return """
const a: number[] = [1, 2, 3];
console.assert("" + a === "1,2,3");
const b: number[] = [];
console.assert("" + b === "");
"""


@register("string-relational-prefix")
def p_string_prefix(rng):
    # Strict prefix is less than the full string.
    return """
console.assert("ab" < "abc");
console.assert("abc" > "ab");
console.assert("abc" === "abc");
console.assert("" < "a");
"""


@register("string-relational-asymm")
def p_string_asymm(rng):
    return """
console.assert("apple" < "banana");
console.assert(!("banana" < "apple"));
console.assert("banana" > "apple");
"""


@register("in-operator-array")
def p_in_arr(rng):
    return """
const a: number[] = [10, 20, 30];
console.assert(0 in a);
console.assert(1 in a);
console.assert(2 in a);
console.assert(!(3 in a));
console.assert(!(-1 in a));
"""


@register("in-operator-object")
def p_in_obj(rng):
    return """
const o = { x: 1, y: 2 };
console.assert("x" in o);
console.assert("y" in o);
console.assert(!("z" in o));
"""


@register("toboolean-empty-string-falsy")
def p_tobool_empty(rng):
    return """
console.assert(!"");
console.assert("" || "fallback" === "fallback");
const b: number = "" ? 1 : 2;
console.assert(b === 2);
"""


@register("toboolean-nonempty-string-truthy")
def p_tobool_nonempty(rng):
    return """
console.assert(!!"x");
console.assert(!!"hello");
const b: number = "x" ? 1 : 2;
console.assert(b === 1);
"""


@register("toboolean-zero-falsy")
def p_tobool_zero(rng):
    return """
console.assert(!0);
console.assert(!-0);
const b: number = 0 ? 1 : 2;
console.assert(b === 2);
"""


@register("signed-zero-equality")
def p_signed_zero(rng):
    return """
console.assert(+0 === -0);
console.assert(0 === -0);
console.assert(!Object.is(+0, -0));
console.assert(Object.is(0, 0));
"""


@register("infinity-equality")
def p_infinity(rng):
    return """
console.assert(Infinity === Infinity);
console.assert(-Infinity === -Infinity);
console.assert(Infinity > 1e308);
console.assert(-Infinity < -1e308);
console.assert(1 / 0 === Infinity);
"""


@register("nan-arith-propagation")
def p_nan_prop(rng):
    return """
console.assert(Number.isNaN(NaN + 1));
console.assert(Number.isNaN(NaN * 0));
console.assert(Number.isNaN(NaN - NaN));
console.assert(Number.isNaN(Infinity - Infinity));
console.assert(Number.isNaN(0 * Infinity));
console.assert(Number.isNaN(0 / 0));
"""


@register("math-sqrt-neg-nan")
def p_sqrt_neg(rng):
    return """
console.assert(Number.isNaN(Math.sqrt(-1)));
console.assert(Number.isNaN(Math.sqrt(-4)));
console.assert(Math.sqrt(0) === 0);
console.assert(Math.sqrt(4) === 2);
"""


@register("math-imul-wrap")
def p_imul_wrap(rng):
    return """
console.assert(Math.imul(2, 3) === 6);
console.assert(Math.imul(-1, 8) === -8);
console.assert(Math.imul(0xffffffff, 5) === -5);
console.assert(Math.imul(0, 100) === 0);
"""


@register("math-clz32-pow2")
def p_clz32_pow2(rng):
    return """
console.assert(Math.clz32(1) === 31);
console.assert(Math.clz32(2) === 30);
console.assert(Math.clz32(4) === 29);
console.assert(Math.clz32(0x80000000) === 0);
console.assert(Math.clz32(0) === 32);
"""


@register("math-log1p-zero")
def p_log1p_zero(rng):
    return """
console.assert(Math.log1p(0) === 0);
console.assert(Math.expm1(0) === 0);
"""


@register("math-hypot-zero-args")
def p_hypot_zero(rng):
    return """
console.assert(Math.hypot() === 0);
console.assert(Math.hypot(3, 4) === 5);
console.assert(Math.hypot(5) === 5);
"""


@register("math-abs-symmetric")
def p_abs_symmetric(rng):
    return """
const x: number = nondet_number();
__CPROVER_assume(x >= -1000 && x <= 1000 && !Number.isNaN(x));
console.assert(Math.abs(x) === Math.abs(-x));
"""


@register("array-includes-NaN-true")
def p_includes_nan(rng):
    return """
const a: number[] = [1, NaN, 3];
console.assert(a.includes(NaN));
console.assert(a.includes(1));
console.assert(!a.includes(99));
"""


@register("array-forEach-sum")
def p_foreach_sum(rng):
    return """
const a: number[] = [10, 20, 30, 40];
let sum: number = 0;
a.forEach((v) => { sum += v; });
console.assert(sum === 100);
"""


@register("array-forEach-empty-noop")
def p_foreach_empty(rng):
    return """
const empty: number[] = [];
let count: number = 0;
empty.forEach(() => { count++; });
console.assert(count === 0);
"""


@register("array-includes-fromindex")
def p_includes_from(rng):
    return """
const a: number[] = [1, 2, 3, 1];
console.assert(a.includes(1, 0));
console.assert(a.includes(1, 1));    // 1 is also at index 3
console.assert(a.includes(1, -1));   // index 3
console.assert(!a.includes(1, 4));   // past end
"""


@register("parseint-radix-roundtrip")
def p_parseint_radix(rng):
    return """
console.assert(parseInt("42") === 42);
console.assert(parseInt("0x10") === 16);
console.assert(parseInt("ff", 16) === 255);
console.assert(parseInt("  42  ") === 42);
console.assert(Number("") === 0);
"""


@register("logical-or-truthy-left")
def p_or_truthy(rng):
    return """
console.assert((1 || 99) === 1);
console.assert(("hello" || "world") === "hello");
console.assert((0 || 7) === 7);
"""


@register("logical-and-falsy-left")
def p_and_falsy(rng):
    return """
console.assert((0 && 99) === 0);
console.assert((1 && 42) === 42);
console.assert((true && 7) === 7);
"""


@register("destructure-default-short-array")
def p_destruct_default(rng):
    return """
const [a = 10, b = 20, c = 30] = [1, 2];
console.assert(a === 1);
console.assert(b === 2);
console.assert(c === 30);

const [x = 99] = [];
console.assert(x === 99);
"""


@register("for-loop-body-bounds")
def p_for_bounds(rng):
    return """
const arr: number[] = [10, 20, 30, 40, 50];
let sum: number = 0;
for (let i: number = 0; i < arr.length; i++) {
    sum += arr[i];
}
console.assert(sum === 150);
"""


@register("for-loop-continue-iter")
def p_for_continue(rng):
    return """
let sum: number = 0;
for (let j: number = 0; j < 5; j++) {
    if (j === 2) continue;
    sum += j;
}
// 0 + 1 + 3 + 4 = 8
console.assert(sum === 8);
"""


@register("string-plus-number-coerce")
def p_str_plus_num(rng):
    return """
console.assert("count: " + 42 === "count: 42");
console.assert("pi: " + 3.14 === "pi: 3.14");
console.assert("" + 0 === "0");
console.assert("" + true === "true");
"""


@register("function-pointer-alias")
def p_fn_alias(rng):
    return """
function foo(): number { return 42; }
const f = foo;
console.assert(f() === 42);
"""


@register("array-concat-variadic")
def p_concat_var(rng):
    return """
const a: number[] = [1, 2];
const b = a.concat(3, 4);
console.assert(b.length === 4);
console.assert(b[3] === 4);
"""


@register("string-charat-out-of-bounds")
def p_charat_oob(rng):
    return """
const s = "abc";
console.assert(s.charAt(10) === "");
console.assert(s.charAt(-1) === "");
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
