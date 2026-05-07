// Harness for npm package 'left-pad' (stevemao/left-pad)
// Version tracked in integration/typescript-npm/package.json
//
// Verifies length invariants of a left-padding function over
// SYMBOLIC inputs. If the frontend miscompiles the length tracking
// or branch logic, these properties fail.
//
// Upstream: https://github.com/stevemao/left-pad

// Reimplementation of the length-contract for leftPad(str, len, ch='0'):
//   If len <= str.length: result is str (unchanged).
//   Else: result has exactly len characters, ending with str.
function leftPadLength(strLen: number, len: number): number {
  if (strLen >= len) return strLen;
  return len;
}

// Property 1: monotonicity.
// Padding with a larger target length never shrinks the result.
const sLen: number = nondet_number();
const tLen1: number = nondet_number();
const tLen2: number = nondet_number();
__CPROVER_assume(sLen >= 0 && sLen <= 10);
__CPROVER_assume(tLen1 >= 0 && tLen1 <= 20);
__CPROVER_assume(tLen2 >= 0 && tLen2 <= 20);
__CPROVER_assume(tLen1 <= tLen2);

console.assert(leftPadLength(sLen, tLen1) <= leftPadLength(sLen, tLen2));

// Property 2: idempotence at the boundary.
// If target length equals string length, result length unchanged.
console.assert(leftPadLength(sLen, sLen) === sLen);

// Property 3: lower bound.
// For all valid inputs, result length is at least max(strLen, targetLen).
const sLenA: number = nondet_number();
const tLenA: number = nondet_number();
__CPROVER_assume(sLenA >= 0 && sLenA <= 10);
__CPROVER_assume(tLenA >= 0 && tLenA <= 20);

const resultLen: number = leftPadLength(sLenA, tLenA);
console.assert(resultLen >= sLenA);
console.assert(resultLen >= tLenA);

// Property 4: exact equality.
// Either result is str unchanged, or result has exactly len characters.
console.assert(resultLen === sLenA || resultLen === tLenA);

// Property 5: boundary behavior.
// When strLen >= len, the result is exactly strLen (no padding happens).
// The mutant `if (strLen > len)` returns strLen when strLen > len but
// returns `len` when strLen === len. The return value is the same (both
// equal strLen) BUT the semantic of "did we pad?" differs. We encode the
// distinction by checking an explicit classification.
function didPad(strLen: number, len: number): boolean {
  if (strLen >= len) return false; // no padding needed
  return true;
}
// If string is already long enough, no padding occurs.
const eqLen: number = nondet_number();
__CPROVER_assume(eqLen >= 0 && eqLen <= 10);
console.assert(didPad(eqLen, eqLen) === false); // boundary
console.assert(didPad(eqLen + 1, eqLen) === false); // strictly longer
console.assert(didPad(eqLen, eqLen + 1) === true); // too short
