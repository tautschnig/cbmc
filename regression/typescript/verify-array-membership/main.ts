const allowed: number[] = [1, 2, 3, 4, 5];
const x: number = nondet_number();
__CPROVER_assume(x >= 1 && x <= 5);
// x is in range but we can verify includes works for constants
console.assert(allowed.includes(3) === true);
console.assert(allowed.includes(6) === false);
