function abs(x: number): number {
  if (x < 0) return -x;
  return x;
}
const a: number = nondet_number();
__CPROVER_assume(a > -1000);
__CPROVER_assume(a < 1000);
const r: number = abs(a);
console.assert(r >= 0);
