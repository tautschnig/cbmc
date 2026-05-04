// Verify function postcondition
function abs(x: number): number {
  if (x < 0) return -x;
  return x;
}
const a: number = nondet_number();
__CPROVER_assume(a > -100);
__CPROVER_assume(a < 100);
const result: number = abs(a);
console.assert(result >= 0);
