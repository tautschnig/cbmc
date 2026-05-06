const x: number = nondet_number();
const y: number = nondet_number();
__CPROVER_assume(x >= 0 && x <= 10);
__CPROVER_assume(y >= 0 && y <= 10);
__CPROVER_assume(x + y === 10);
console.assert(x + y === 10);
console.assert(x <= 10);
console.assert(y >= 0);
