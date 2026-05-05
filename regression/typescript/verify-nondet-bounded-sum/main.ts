const a: number = nondet_number();
const b: number = nondet_number();
const c: number = nondet_number();
__CPROVER_assume(a >= 0 && a <= 10);
__CPROVER_assume(b >= 0 && b <= 10);
__CPROVER_assume(c >= 0 && c <= 10);
const sum: number = a + b + c;
console.assert(sum >= 0);
console.assert(sum <= 30);
