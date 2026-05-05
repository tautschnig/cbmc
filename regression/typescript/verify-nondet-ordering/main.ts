const a: number = nondet_number();
const b: number = nondet_number();
const c: number = nondet_number();
__CPROVER_assume(a < b);
__CPROVER_assume(b < c);
console.assert(a < c);
console.assert(a !== c);
