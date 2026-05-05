const a: number = nondet_number();
const b: number = nondet_number();
__CPROVER_assume(a >= 0 && a <= 100);
__CPROVER_assume(b >= 0 && b <= 100);
const max: number = a > b ? a : b;
console.assert(max >= a);
console.assert(max >= b);
console.assert(max === a || max === b);
