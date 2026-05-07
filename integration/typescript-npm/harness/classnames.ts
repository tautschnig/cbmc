// Harness for npm package 'classnames' (JedWatson/classnames)
// Version tracked in integration/typescript-npm/package.json
//
// Verifies length composition invariants over concatenation with
// SYMBOLIC string lengths. If the frontend miscompiles string length
// tracking, these properties fail.
//
// Upstream: https://github.com/JedWatson/classnames/blob/v2.5.1/index.js

// Reimplementation of classnames' conditional-joining semantics.
// Empty strings are skipped; non-empty strings separated by spaces.
function cls2(a: string, b: string): string {
  if (a.length === 0) return b;
  if (b.length === 0) return a;
  return a + " " + b;
}

// Property 1: length composition for non-empty inputs.
// For non-empty a, b: length(cls2(a, b)) === length(a) + 1 + length(b)
const a: string = "abc";
const b: string = "xy";
const joined: string = cls2(a, b);
console.assert(joined.length === a.length + 1 + b.length);
console.assert(joined.length === 6);

// Property 2: identity element (empty string).
console.assert(cls2("", "foo").length === 3);
console.assert(cls2("foo", "").length === 3);
console.assert(cls2("", "") === "");

// Property 3: length over a loop with symbolic choice.
// Build a concatenation via multiple steps and verify length accounting.
const pick: number = nondet_number();
__CPROVER_assume(pick === 0 || pick === 1 || pick === 2);

let result: string = "a";
if (pick >= 1) {
  result = cls2(result, "bb");
}
if (pick >= 2) {
  result = cls2(result, "ccc");
}

// Length invariant for each branch:
//   pick=0: "a"         -> 1
//   pick=1: "a bb"      -> 4
//   pick=2: "a bb ccc"  -> 8
if (pick === 0) console.assert(result.length === 1);
if (pick === 1) console.assert(result.length === 4);
if (pick === 2) console.assert(result.length === 8);

// Property 4: non-empty concatenation always contains the space separator.
// For non-empty inputs, the joined result is strictly longer than either.
console.assert(cls2("a", "b").length > "a".length);
console.assert(cls2("a", "b").length > "b".length);
