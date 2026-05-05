const x: number = nondet_number();
__CPROVER_assume(x > -1000 && x < 1000);
const clamped: number = x < 0 ? 0 : x > 255 ? 255 : x;
console.assert(clamped >= 0);
console.assert(clamped <= 255);
