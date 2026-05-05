const x: number = nondet_number();
const y: number = nondet_number();
__CPROVER_assume(x > -1000 && x < 1000);
__CPROVER_assume(y > 0 && y < 1000);
const result: number = x / y;
