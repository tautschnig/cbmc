// Verification primitives for CBMC TypeScript frontend.
// These are not part of ECMAScript or TypeScript — they are
// CBMC-specific extensions for bounded model checking.
//
// nondet_number(): Returns an unconstrained symbolic number value.
// __CPROVER_assume(cond): Constrains the symbolic state.
// console.assert(cond): Creates a CBMC assertion property.
declare function nondet_number(): number;
declare function __CPROVER_assume(cond: boolean): void;

const x: number = nondet_number();
__CPROVER_assume(x > 0 && x < 10);
console.assert(x >= 1);
console.assert(x <= 9);
