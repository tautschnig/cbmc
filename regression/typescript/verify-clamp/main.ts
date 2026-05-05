function clamp(x: number, min: number, max: number): number {
  if (x < min) return min;
  if (x > max) return max;
  return x;
}
const v: number = nondet_number();
__CPROVER_assume(v > -1000);
__CPROVER_assume(v < 1000);
const result: number = clamp(v, 0, 100);
console.assert(result >= 0);
console.assert(result <= 100);
