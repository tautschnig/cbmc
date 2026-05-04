let x: number = nondet_number();
__CPROVER_assume(x > 0);
__CPROVER_assume(x < 100);
let count: number = 0;
while (x > 1) {
  x = x / 2;
  count++;
}
console.assert(count >= 0);
console.assert(count < 100);
