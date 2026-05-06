function clamp(x: number, lo: number, hi: number): number {
  if (x < lo) return lo;
  if (x > hi) return hi;
  return x;
}
const n: number = nondet_number();
__CPROVER_assume(n > -1000 && n < 1000);
const r: number = clamp(n, 0, 100);
console.assert(r >= 0 && r <= 100);
