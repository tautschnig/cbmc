const x: number = nondet_number();
__CPROVER_assume(x >= 1);
__CPROVER_assume(x <= 10);
const doubled: number = x * 2;
console.assert(doubled >= 2);
console.assert(doubled <= 20);
