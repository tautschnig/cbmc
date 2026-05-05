// 300th regression test
const x: number = nondet_number();
const y: number = nondet_number();
__CPROVER_assume(x >= 1 && x <= 10);
__CPROVER_assume(y >= 1 && y <= 10);
const sum: number = x + y;
const product: number = x * y;
console.assert(sum >= 2);
console.assert(sum <= 20);
console.assert(product >= 1);
console.assert(product <= 100);
