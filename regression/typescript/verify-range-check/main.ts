function inRange(x: number, lo: number, hi: number): boolean {
  return x >= lo && x <= hi;
}
const v: number = nondet_number();
__CPROVER_assume(v >= 0);
__CPROVER_assume(v <= 100);
console.assert(inRange(v, 0, 100) === true);
console.assert(inRange(v, 50, 100) === true || v < 50);
