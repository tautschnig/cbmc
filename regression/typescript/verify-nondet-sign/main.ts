const x: number = nondet_number();
__CPROVER_assume(x > -100 && x < 100);
__CPROVER_assume(x !== 0);
const sign: number = x > 0 ? 1 : -1;
console.assert(sign === 1 || sign === -1);
console.assert(x * sign > 0);
