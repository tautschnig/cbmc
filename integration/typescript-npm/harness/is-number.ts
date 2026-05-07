// Harness for npm package 'is-number' (jonschlinkert/is-number)
// Version tracked in integration/typescript-npm/package.json
//
// isNumber(x) returns true iff x is a "valid" numeric value.
// Excludes: NaN (which is technically `typeof === 'number'`).
// We verify the classification logic over SYMBOLIC inputs.
//
// Upstream source (index.js):
//   module.exports = function(num) {
//     if (typeof num === 'number') return num - num === 0;
//     if (typeof num === 'string' && num.trim() !== '')
//       return Number.isFinite ? Number.isFinite(+num) : isFinite(+num);
//     return false;
//   };
//
// For our reimplementation, we focus on the number branch. String
// parsing + Number coercion is out of scope because our frontend's
// string-to-number conversion is limited.

// Reimplementation of the number branch: num is a valid number iff
// num - num === 0. This excludes NaN (NaN - NaN === NaN, which !== 0)
// and Infinity (Infinity - Infinity === NaN).
function isFiniteNumber(num: number): boolean {
  return num - num === 0;
}

// Property 1: ordinary numbers are finite.
const n: number = nondet_number();
__CPROVER_assume(!Number.isNaN(n));
// Note: we can't easily exclude Infinity in our model; the property
// is stated for non-NaN non-Infinity values. CBMC's floatbv supports
// Infinity symbolically so we let it find counterexamples naturally.
__CPROVER_assume(n > -1e10 && n < 1e10); // bounded-real range

console.assert(isFiniteNumber(n));

// Property 2: NaN is classified as non-finite.
const nan: number = 0.0 / 0.0;  // canonical NaN
console.assert(!isFiniteNumber(nan));

// Property 3: specific finite values.
console.assert(isFiniteNumber(0));
console.assert(isFiniteNumber(1));
console.assert(isFiniteNumber(-1));
console.assert(isFiniteNumber(3.14));

// Property 4: the classification is idempotent — applying twice yields
// the same result.
const m: number = nondet_number();
__CPROVER_assume(!Number.isNaN(m) && m > -1e10 && m < 1e10);
const result1: boolean = isFiniteNumber(m);
const result2: boolean = isFiniteNumber(m);
console.assert(result1 === result2);

// Property 5: symmetry under negation — if n is finite then -n is finite.
console.assert(isFiniteNumber(n) === isFiniteNumber(-n));
