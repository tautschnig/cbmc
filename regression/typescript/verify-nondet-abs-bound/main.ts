const x: number = nondet_number();
__CPROVER_assume(x >= -50 && x <= 50);
const abs_x: number = x >= 0 ? x : -x;
console.assert(abs_x >= 0);
console.assert(abs_x <= 50);
