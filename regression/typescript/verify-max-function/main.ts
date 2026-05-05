function max(a: number, b: number): number {
  return a > b ? a : b;
}
const x: number = nondet_number();
const y: number = nondet_number();
__CPROVER_assume(x > -1000);
__CPROVER_assume(x < 1000);
__CPROVER_assume(y > -1000);
__CPROVER_assume(y < 1000);
const m: number = max(x, y);
console.assert(m >= x);
console.assert(m >= y);
