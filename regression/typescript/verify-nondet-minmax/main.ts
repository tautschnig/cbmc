const a: number = nondet_number();
const b: number = nondet_number();
__CPROVER_assume(a >= 0 && a <= 100);
__CPROVER_assume(b >= 0 && b <= 100);
const min: number = a < b ? a : b;
const max: number = a > b ? a : b;
console.assert(min <= max);
console.assert(min + max === a + b);
