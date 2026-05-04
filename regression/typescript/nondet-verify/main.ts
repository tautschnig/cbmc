// Verification with nondet values
const x: number = nondet_number();
__CPROVER_assume(x > 0);
__CPROVER_assume(x < 10);
console.assert(x > 0);
console.assert(x < 10);
