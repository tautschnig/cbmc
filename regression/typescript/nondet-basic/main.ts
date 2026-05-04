// Verification primitives for CBMC TypeScript frontend.
// These are not part of ECMAScript or TypeScript — they are
// CBMC-specific extensions for bounded model checking.
declare function nondet_number(): number;
declare function __CPROVER_assume(cond: boolean): void;

const x: number = nondet_number();
__CPROVER_assume(x > 0 && x < 10);
// ES2024 sec-ecmascript-language-types-number-type: number is float
// x > 0 does NOT imply x >= 1 (x could be 0.5)
console.assert(x > 0);
console.assert(x < 10);
