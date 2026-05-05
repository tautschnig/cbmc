const x: number = nondet_number();
__CPROVER_assume(x > 0);
__CPROVER_assume(x < 100);
const y: number = 10 / x;
console.assert(y > 0);
