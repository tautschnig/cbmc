const x: number = nondet_number();
__CPROVER_assume(x > -50);
__CPROVER_assume(x < 50);
const a: number = x >= 0 ? x : -x;
console.assert(a >= 0);
