const x: number = nondet_number();
const y: number = nondet_number();
__CPROVER_assume(x > 0);
__CPROVER_assume(y > 0);
__CPROVER_assume(x + y < 100);
console.assert(x < 100);
console.assert(y < 100);
