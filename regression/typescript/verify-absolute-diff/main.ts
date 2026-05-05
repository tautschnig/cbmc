function absDiff(a: number, b: number): number {
  return a > b ? a - b : b - a;
}
const x: number = nondet_number();
const y: number = nondet_number();
__CPROVER_assume(x > 0 && x < 100);
__CPROVER_assume(y > 0 && y < 100);
const d: number = absDiff(x, y);
console.assert(d >= 0);
console.assert(d < 100);
