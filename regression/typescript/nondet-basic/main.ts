// CBMC verification primitives for TypeScript
declare function nondet_number(): number;
declare function __CPROVER_assume(cond: boolean): void;

const x: number = nondet_number();
__CPROVER_assume(x > 0 && x < 10);
console.assert(x >= 1);
console.assert(x <= 9);
