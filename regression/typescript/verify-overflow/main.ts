const x: number = nondet_number();
__CPROVER_assume(x > 0);
__CPROVER_assume(x < 1000);
const doubled: number = x * 2;
console.assert(doubled < 2000);
console.assert(doubled > 0);
