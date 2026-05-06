const x: number = nondet_number();
__CPROVER_assume(x > -100 && x < 100);
const clamped: number = x < 0 ? 0 : (x > 10 ? 10 : x);
console.assert(clamped >= 0);
console.assert(clamped <= 10);
