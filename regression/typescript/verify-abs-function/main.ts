function abs(x: number): number { return x < 0 ? -x : x; }
const n: number = nondet_number();
__CPROVER_assume(n > -100 && n < 100);
__CPROVER_assume(n !== 0);
console.assert(abs(n) > 0);
console.assert(abs(n) === abs(-n));
