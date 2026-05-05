const a: number = nondet_number();
const b: number = nondet_number();
__CPROVER_assume(a > 0);
__CPROVER_assume(b > 0);
__CPROVER_assume(a < 100);
__CPROVER_assume(b < 100);
// Sort two elements
const min: number = a < b ? a : b;
const max: number = a < b ? b : a;
console.assert(min <= max);
console.assert(min === a || min === b);
console.assert(max === a || max === b);
